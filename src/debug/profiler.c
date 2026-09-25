#include "profiler.h"
#include <string.h>
#include <rspq_profile.h>   // not included by libdragon.h; RSPQ_PROFILE comes from rspq_constants.h

bool     g_prof_on = false;
uint32_t g_prof_ticks[PROF_SLOT_COUNT];
uint16_t g_prof_calls[PROF_SLOT_COUNT];

static ProfilerFrame pf;
static RspProfile rsp;
static RdpCounters rdpc;
#if RSPQ_PROFILE
static int rsp_frames = 0;
#endif
static bool header_sent = false;

// libdragon's time accounting (src/accounting.c): the CPU ticks spent in its
// spin-waits on the RSP (a full command buffer waiting for the RSP, the audio
// mixer's high-priority sync, syncpoints). Internal to libdragon, so declared
// here; the category is ACCT_CAT_RSPQ in libdragon's accounting_internal.h.
extern uint64_t acct_get_ticks(int category);
#define ACCT_CAT_RSPQ 4
static uint64_t rspq_ticks_last;

static const struct { const char *name; int depth; } slot_info[PROF_SLOT_COUNT] = {
    [PROF_FRAME]           = {"frame",         0},
    [PROF_WAIT_DISPLAY]    = {"wait_display",  1},
    [PROF_PACE]            = {"pace",          1},
    [PROF_WAIT_INPUT]      = {"wait_input",    1},
    [PROF_INPUT]           = {"input",         1},
    [PROF_UPDATE]          = {"update",        1},
    [PROF_PHYSICS]         = {"physics",       2},
    [PROF_PARTICLE_UPDATE] = {"particle_upd",  2},
    [PROF_SCENE_SYS]       = {"scene_sys",     2},
    [PROF_DRAW]            = {"draw",          1},
    [PROF_SKY]             = {"sky",           2},
    [PROF_FLOOR]           = {"floor",         2},
    [PROF_SHADOWS]         = {"shadows",       2},
    [PROF_OBJECTS]         = {"objects",       2},
    [PROF_MESH_CULL]       = {"mesh_cull",     3},
    [PROF_MESH_LIGHT]      = {"mesh_light",    3},
    [PROF_MESH_TRIS]       = {"mesh_tris",     3},
    [PROF_PARTICLE_DRAW]   = {"particle_draw", 2},
    [PROF_HUD]             = {"hud",           2},
    [PROF_MENU]            = {"menu",          2},
    [PROF_DIALOG]          = {"dialog",        2},
    [PROF_OVERLAY]         = {"overlay",       1},
    [PROF_AUDIO]           = {"audio",         1},
    [PROF_RSP_WAIT]        = {"rsp_wait",      1},
};

void profiler_init(void) {
    memset(&pf, 0, sizeof(pf));
    memset(g_prof_ticks, 0, sizeof(g_prof_ticks));
    memset(g_prof_calls, 0, sizeof(g_prof_calls));
    g_prof_on = ENGINE_PROFILE ? true : false;
    rspq_ticks_last = acct_get_ticks(ACCT_CAT_RSPQ);   // count from here, not from boot

    memset(&rsp, 0, sizeof(rsp));
#if RSPQ_PROFILE
    rsp.available = true;
    rspq_profile_start();
    debugf("[profiler] RSP/RDP profiling available (libdragon built with RSPQ_PROFILE=1)\n");
#endif
}

#if RSPQ_PROFILE
static void rsp_collect(void) {
    rspq_profile_data_t d;
    rspq_profile_get_data(&d);
    if (d.frame_count == 0 || d.total_ticks == 0) return;

    const float ticks_per_ms = (float)RCP_FREQUENCY / 1000.0f;
    float frames = (float)d.frame_count;
    rsp.frame_ms     = (float)d.total_ticks / frames / ticks_per_ms;
    rsp.rdp_busy_ms  = (float)d.rdp_busy_ticks / frames / ticks_per_ms;
    rsp.rdp_busy_pct = 100.0f * (float)d.rdp_busy_ticks / (float)d.total_ticks;

    rsp.slot_count = 0;
    for (int i = 0; i < RSPQ_PROFILE_SLOT_COUNT && rsp.slot_count < RSP_MAX_SLOTS; i++) {
        if (!d.slots[i].name) continue;
        int k = rsp.slot_count++;
        rsp.slot_name[k]  = d.slots[i].name;
        rsp.slot_ms[k]    = (float)d.slots[i].total_ticks / frames / ticks_per_ms;
        rsp.slot_pct[k]   = 100.0f * (float)d.slots[i].total_ticks / (float)d.total_ticks;
        rsp.slot_calls[k] = (float)d.slots[i].sample_count / frames;
    }
    rsp.valid = true;
    rspq_profile_reset();
}
#endif

const RspProfile *profiler_rsp_get(void) { return &rsp; }
const RdpCounters *profiler_rdp_get(void) { return &rdpc; }

// Read the 24-bit RDP cycle counters, then reset them for the next frame.
// Writing only the counter-reset bits leaves the rest of DP_STATUS alone.
static void rdp_counters_sample(float frame_ms) {
    uint32_t clk  = *DP_CLOCK     & 0xFFFFFF;
    uint32_t busy = *DP_BUSY      & 0xFFFFFF;
    uint32_t pipe = *DP_PIPE_BUSY & 0xFFFFFF;
    uint32_t tmem = *DP_TMEM_BUSY & 0xFFFFFF;
    *DP_STATUS = DP_WSTATUS_RESET_CLOCK_COUNTER | DP_WSTATUS_RESET_CMD_COUNTER |
                 DP_WSTATUS_RESET_PIPE_COUNTER  | DP_WSTATUS_RESET_TMEM_COUNTER;
    if (clk == 0) return;   // emulator without counters, or first frame

    if (frame_ms <= 0.0f) return;
    // Scale by the measured clock count instead of an assumed frequency: on the
    // Analogue 3D DP_CLOCK advances 1.5x faster than the 62.5 MHz RCP clock
    // (~93.75 MHz). Ratios are exact either way; ms are shares of the loop time.
    rdpc.clock_rate_mhz = clk / (frame_ms * 1000.0f);
    float c = frame_ms;
    float b = frame_ms * busy / clk, p = frame_ms * pipe / clk, t = frame_ms * tmem / clk;
    if (!rdpc.available) {
        rdpc.clock_ms = c; rdpc.busy_ms = b; rdpc.pipe_ms = p; rdpc.tmem_ms = t;
        rdpc.available = true;
    } else {
        const float k = 1.0f / 32.0f;
        rdpc.clock_ms += (c - rdpc.clock_ms) * k;
        rdpc.busy_ms  += (b - rdpc.busy_ms)  * k;
        rdpc.pipe_ms  += (p - rdpc.pipe_ms)  * k;
        rdpc.tmem_ms  += (t - rdpc.tmem_ms)  * k;
    }
    if (b > rdpc.busy_peak_ms) rdpc.busy_peak_ms = b;
    rdpc.busy_pct = rdpc.clock_ms > 0.0f ? 100.0f * rdpc.busy_ms / rdpc.clock_ms : 0.0f;
}

void profiler_rsp_dump_csv(uint32_t frame_index) {
    if (rdpc.available) {
        debugf("RDP,%lu,counter_mhz=%.2f,frame_us=%.0f,busy_us=%.0f,pipe_us=%.0f,tmem_us=%.0f,busy_pct=%.1f,busy_peak_us=%.0f\n",
               (unsigned long)frame_index, rdpc.clock_rate_mhz, rdpc.clock_ms * 1000.0f, rdpc.busy_ms * 1000.0f,
               rdpc.pipe_ms * 1000.0f, rdpc.tmem_ms * 1000.0f, rdpc.busy_pct,
               rdpc.busy_peak_ms * 1000.0f);
    } else {
        debugf("RDP,%lu,unavailable\n", (unsigned long)frame_index);
    }
    if (!rsp.available || !rsp.valid) {
        debugf("RSP,%lu,unavailable\n", (unsigned long)frame_index);
        return;
    }
    debugf("RSP,%lu,frame_us=%.0f,rdp_busy_us=%.0f,rdp_busy_pct=%.1f",
           (unsigned long)frame_index, rsp.frame_ms * 1000.0f,
           rsp.rdp_busy_ms * 1000.0f, rsp.rdp_busy_pct);
    for (int i = 0; i < rsp.slot_count; i++) {
        debugf(",%s=%.0f", rsp.slot_name[i], rsp.slot_ms[i] * 1000.0f);
    }
    debugf("\n");
}

void profiler_set_enabled(bool enabled) {
    g_prof_on = ENGINE_PROFILE && enabled;
}

void profiler_frame_begin(void) {
    memset(g_prof_ticks, 0, sizeof(g_prof_ticks));
    memset(g_prof_calls, 0, sizeof(g_prof_calls));
}

void profiler_frame_end(uint32_t frame_ticks) {
    g_prof_ticks[PROF_FRAME] = frame_ticks;
    g_prof_calls[PROF_FRAME] = 1;

    // The frame's RSP waits (measured whether or not the profiler is on)
    uint64_t rspq = acct_get_ticks(ACCT_CAT_RSPQ);
    g_prof_ticks[PROF_RSP_WAIT] = (uint32_t)(rspq - rspq_ticks_last);
    rspq_ticks_last = rspq;

    for (int s = 0; s < PROF_SLOT_COUNT; s++) {
        uint32_t us = (uint32_t)TICKS_TO_US((uint64_t)g_prof_ticks[s]);
        pf.last_us[s] = us;
        pf.calls[s]   = g_prof_calls[s];
        if (pf.frame_index == 0) pf.avg_us[s] = (float)us;
        else                     pf.avg_us[s] += ((float)us - pf.avg_us[s]) * (1.0f / 32.0f);
        if (us > pf.peak_us[s]) pf.peak_us[s] = us;
    }
    pf.frame_index++;
    rdp_counters_sample(pf.last_us[PROF_FRAME] / 1000.0f);

#if RSPQ_PROFILE
    rspq_profile_next_frame();
    if (++rsp_frames >= RSP_WINDOW_FRAMES) {
        rsp_collect();
        rsp_frames = 0;
    }
#endif
}

const ProfilerFrame *profiler_get(void) { return &pf; }

const char *profiler_slot_name(ProfSlot s) {
    return (s >= 0 && s < PROF_SLOT_COUNT) ? slot_info[s].name : "?";
}

int profiler_slot_depth(ProfSlot s) {
    return (s >= 0 && s < PROF_SLOT_COUNT) ? slot_info[s].depth : 0;
}

void profiler_reset_peaks(void) {
    memset(pf.peak_us, 0, sizeof(pf.peak_us));
    rdpc.busy_peak_ms = 0.0f;
}

float profiler_cpu_ms(void) {
    float us = pf.avg_us[PROF_FRAME] - pf.avg_us[PROF_WAIT_DISPLAY] - pf.avg_us[PROF_PACE]
             - pf.avg_us[PROF_WAIT_INPUT];
    return us > 0.0f ? us / 1000.0f : 0.0f;
}

void profiler_dump_csv(void) {
    if (!header_sent) {
        debugf("PROF_HDR,frame,profiler_on");
        for (int s = 0; s < PROF_SLOT_COUNT; s++) debugf(",%s", slot_info[s].name);
        debugf("\n");
        header_sent = true;
    }
    // Averages in microseconds (one decimal), then peaks
    debugf("PROF_AVG,%lu,%d", (unsigned long)pf.frame_index, g_prof_on ? 1 : 0);
    for (int s = 0; s < PROF_SLOT_COUNT; s++) debugf(",%.1f", pf.avg_us[s]);
    debugf("\n");
    debugf("PROF_PEAK,%lu,%d", (unsigned long)pf.frame_index, g_prof_on ? 1 : 0);
    for (int s = 0; s < PROF_SLOT_COUNT; s++) debugf(",%lu", (unsigned long)pf.peak_us[s]);
    debugf("\n");
}
