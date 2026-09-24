#include "frametime.h"
#include <string.h>
#include <stdlib.h>

#ifdef N64
#include <libdragon.h>
#else
#include <stdio.h>
#define debugf(...) printf(__VA_ARGS__)
#endif

static uint32_t ring_frame[FRAMETIME_WINDOW];
static uint32_t ring_cpu[FRAMETIME_WINDOW];
static int      ring_head  = 0;
static int      ring_count = 0;
static int      header_sent = 0;
static int      present_header_sent = 0;

// Presented frames: written by the vblank interrupt, read by frametime_get
static volatile uint8_t  ring_present[FRAMETIME_WINDOW];
static volatile uint16_t ring_torn[FRAMETIME_WINDOW];     // half-line of a torn flip, 0 = clean
static volatile int     present_head  = 0;
static volatile int     present_count = 0;

#ifdef N64
#define PRESENT_LOCK()   disable_interrupts()
#define PRESENT_UNLOCK() enable_interrupts()
#else
#define PRESENT_LOCK()   ((void)0)
#define PRESENT_UNLOCK() ((void)0)
#endif

void frametime_record_present(int vblanks, int torn_halfline) {
    if (vblanks < 1) vblanks = 1;
    if (vblanks > 255) vblanks = 255;
    if (torn_halfline < 0) torn_halfline = 0;
    ring_present[present_head] = (uint8_t)vblanks;
    ring_torn[present_head] = (uint16_t)torn_halfline;
    present_head = (present_head + 1) % FRAMETIME_WINDOW;
    if (present_count < FRAMETIME_WINDOW) present_count++;
}

void frametime_record(uint32_t frame_us, uint32_t cpu_us) {
    ring_frame[ring_head] = frame_us;
    ring_cpu[ring_head]   = cpu_us;
    ring_head = (ring_head + 1) % FRAMETIME_WINDOW;
    if (ring_count < FRAMETIME_WINDOW) ring_count++;
}

void frametime_reset(void) {
    ring_head = 0;
    ring_count = 0;
    PRESENT_LOCK();
    present_head = 0;
    present_count = 0;
    PRESENT_UNLOCK();
}

static int cmp_desc(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x < y) - (x > y);
}

// Presented-frame statistics against the target interval (vblanks per frame)
static void present_stats(FrameTimeStats *out, float budget_ms) {
    uint8_t snap[FRAMETIME_WINDOW];
    uint16_t torn[FRAMETIME_WINDOW];
    PRESENT_LOCK();
    int n = present_count;
    for (int i = 0; i < n; i++) { snap[i] = ring_present[i]; torn[i] = ring_torn[i]; }
    PRESENT_UNLOCK();
    int target = budget_ms > 25.0f ? 2 : 1;          // 30 or 60 FPS
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) {
        int v = snap[i];
        sum += v;
        int b = v > FRAMETIME_PRESENT_BUCKETS ? FRAMETIME_PRESENT_BUCKETS : v;
        out->present_hist[b - 1]++;
        if (v > target) out->late++;
        if (torn[i]) {
            out->torn++;
            if (torn[i] > out->torn_worst_halfline) out->torn_worst_halfline = torn[i];
        }
    }
    out->presents = n;
    out->present_avg_vblanks = n ? (float)sum / n : 0.0f;
}

void frametime_get(FrameTimeStats *out, float budget_ms) {
    memset(out, 0, sizeof(*out));
    present_stats(out, budget_ms);
    int n = ring_count;
    out->count = n;
    if (n == 0) return;

    uint32_t sorted[FRAMETIME_WINDOW];
    uint64_t sum = 0, cpu_sum = 0;
    uint32_t mn = UINT32_MAX, mx = 0, cpu_mx = 0;
    uint32_t budget_us = (uint32_t)(budget_ms * 1100.0f);   // budget + 10 %

    for (int i = 0; i < n; i++) {
        uint32_t f = ring_frame[i];
        sorted[i] = f;
        sum += f;
        cpu_sum += ring_cpu[i];
        if (f < mn) mn = f;
        if (f > mx) mx = f;
        if (ring_cpu[i] > cpu_mx) cpu_mx = ring_cpu[i];
        if (ring_cpu[i] > budget_us) out->over_budget++;
        int b = (int)(f / FRAMETIME_BUCKET_US);
        if (b >= FRAMETIME_BUCKETS) b = FRAMETIME_BUCKETS - 1;
        out->histogram[b]++;
    }
    qsort(sorted, n, sizeof(uint32_t), cmp_desc);

    out->avg_ms     = (float)sum / n / 1000.0f;
    out->min_ms     = mn / 1000.0f;
    out->max_ms     = mx / 1000.0f;
    out->fps        = out->avg_ms > 0.0f ? 1000.0f / out->avg_ms : 0.0f;
    out->cpu_avg_ms = (float)cpu_sum / n / 1000.0f;
    out->cpu_max_ms = cpu_mx / 1000.0f;

    // p99: value exceeded by only 1 % of frames (index into descending order)
    int p99_idx = n / 100;
    out->p99_ms = sorted[p99_idx] / 1000.0f;

    // 1 % low: mean of the slowest ceil(n/100) frames
    int k = (n + 99) / 100;
    uint64_t slow = 0;
    for (int i = 0; i < k; i++) slow += sorted[i];
    float slow_ms = (float)slow / k / 1000.0f;
    out->low1_fps = slow_ms > 0.0f ? 1000.0f / slow_ms : 0.0f;
}

void frametime_dump_csv(uint32_t frame_index, float budget_ms) {
    FrameTimeStats s;
    frametime_get(&s, budget_ms);
    if (!header_sent) {
        debugf("FT_HDR,frame,count,fps,avg_ms,min_ms,max_ms,p99_ms,low1_fps,cpu_avg_ms,cpu_max_ms,over_budget,hist_1.5ms_buckets...\n");
        header_sent = 1;
    }
    debugf("FT,%lu,%d,%.1f,%.2f,%.2f,%.2f,%.2f,%.1f,%.2f,%.2f,%d",
           (unsigned long)frame_index, s.count, s.fps, s.avg_ms, s.min_ms, s.max_ms,
           s.p99_ms, s.low1_fps, s.cpu_avg_ms, s.cpu_max_ms, s.over_budget);
    for (int b = 0; b < FRAMETIME_BUCKETS; b++) debugf(",%u", s.histogram[b]);
    debugf("\n");
    if (!present_header_sent) {
        debugf("FTP_HDR,frame,presents,vb1,vb2,vb3,vb4plus,late,avg_vblanks,torn,torn_worst_halfline\n");
        present_header_sent = 1;
    }
    debugf("FTP,%lu,%d,%u,%u,%u,%u,%d,%.3f,%d,%d\n", (unsigned long)frame_index, s.presents,
           s.present_hist[0], s.present_hist[1], s.present_hist[2], s.present_hist[3],
           s.late, s.present_avg_vblanks, s.torn, s.torn_worst_halfline);
}
