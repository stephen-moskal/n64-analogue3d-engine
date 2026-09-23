#include "profiler.h"
#include <string.h>

bool     g_prof_on = false;
uint32_t g_prof_ticks[PROF_SLOT_COUNT];
uint16_t g_prof_calls[PROF_SLOT_COUNT];

static ProfilerFrame pf;
static bool header_sent = false;

static const struct { const char *name; int depth; } slot_info[PROF_SLOT_COUNT] = {
    [PROF_FRAME]           = {"frame",         0},
    [PROF_WAIT_DISPLAY]    = {"wait_display",  1},
    [PROF_LIMITER]         = {"limiter",       1},
    [PROF_UPDATE]          = {"update",        1},
    [PROF_INPUT]           = {"input",         2},
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
    [PROF_OVERLAY]         = {"overlay",       1},
    [PROF_AUDIO]           = {"audio",         1},
};

void profiler_init(void) {
    memset(&pf, 0, sizeof(pf));
    memset(g_prof_ticks, 0, sizeof(g_prof_ticks));
    memset(g_prof_calls, 0, sizeof(g_prof_calls));
    g_prof_on = ENGINE_PROFILE ? true : false;
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

    for (int s = 0; s < PROF_SLOT_COUNT; s++) {
        uint32_t us = (uint32_t)TICKS_TO_US((uint64_t)g_prof_ticks[s]);
        pf.last_us[s] = us;
        pf.calls[s]   = g_prof_calls[s];
        if (pf.frame_index == 0) pf.avg_us[s] = (float)us;
        else                     pf.avg_us[s] += ((float)us - pf.avg_us[s]) * (1.0f / 32.0f);
        if (us > pf.peak_us[s]) pf.peak_us[s] = us;
    }
    pf.frame_index++;
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
}

float profiler_cpu_ms(void) {
    float us = pf.avg_us[PROF_FRAME] - pf.avg_us[PROF_WAIT_DISPLAY] - pf.avg_us[PROF_LIMITER];
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
