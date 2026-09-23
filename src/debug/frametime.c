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

void frametime_record(uint32_t frame_us, uint32_t cpu_us) {
    ring_frame[ring_head] = frame_us;
    ring_cpu[ring_head]   = cpu_us;
    ring_head = (ring_head + 1) % FRAMETIME_WINDOW;
    if (ring_count < FRAMETIME_WINDOW) ring_count++;
}

void frametime_reset(void) {
    ring_head = 0;
    ring_count = 0;
}

static int cmp_desc(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x < y) - (x > y);
}

void frametime_get(FrameTimeStats *out, float budget_ms) {
    memset(out, 0, sizeof(*out));
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
}
