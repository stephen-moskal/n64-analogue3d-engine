#include "engine.h"
#include "../input/action.h"
#include "../ui/text.h"
#include "../audio/audio.h"
#include "../render/atmosphere.h"
#include "../debug/engine_debug.h"
#include "../debug/debug_menu.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "../debug/memstats.h"
#include "../debug/frametime.h"
#include "../debug/overlay.h"
#include "../debug/rdp_debug.h"
#include "../debug/testbed.h"

int engine_target_fps = 0;

static surface_t zbuf;

float engine_frame_budget_ms(void) {
    return (engine_target_fps == 30) ? 33.33f : 16.67f;
}

surface_t *engine_zbuf(void) { return &zbuf; }

void engine_init(void) {
    // Debug output: ISViewer (emulators) and USB (SummerCart64)
    debug_init_isviewer();
    debug_init_usblog();

    display_init((resolution_t){ .width = ENGINE_SCREEN_W, .height = ENGINE_SCREEN_H, .interlaced = INTERLACE_OFF },
                 DEPTH_16_BPP, ENGINE_FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Memory stats: RDRAM size, heap, stack high-water mark (paints the stack now)
    memstats_init(ENGINE_FB_COUNT, ENGINE_SCREEN_W, ENGINE_SCREEN_H);

    // RDP command queue. The RDP validator (rdpq_debug_start) is toggled from
    // the Debug tab in debug builds; it is off at boot because its CPU cost
    // can push frames past the budget (defect D18).
    rdpq_init();

    dfs_init(DFS_DEFAULT_LOCATION);   // before anything loads from rom:/

    action_init();
    text_init();
    snd_init();
    atmosphere_init();

    zbuf = surface_alloc(FMT_RGBA16, ENGINE_SCREEN_W, ENGINE_SCREEN_H);
}

// Fill the audio buffers if this is the point of the frame the sound module
// is set to poll at (SndPollPoint; the audio benchmark compares them)
static inline void audio_poll(SndPollPoint point, float dt) {
    if (snd_get_poll_point() != point) return;
    PROF_BEGIN(PROF_AUDIO);
    snd_update(dt);
    PROF_END(PROF_AUDIO);
}

void engine_run(const EngineApp *app) {
    debugf("SMozN64 Dev Engine [%s build, %s %s]\n", ENGINE_BUILD_NAME, __DATE__, __TIME__);

    // Variable timestep: logic runs once per rendered frame with the real
    // elapsed time
    uint32_t last_ticks = TICKS_READ();
    uint32_t frame_index = 0;
    profiler_init();

    while (1) {
        uint32_t now = TICKS_READ();
        uint32_t frame_ticks = (uint32_t)TICKS_DISTANCE(last_ticks, now);
        float dt = (float)frame_ticks / (float)TICKS_PER_SECOND;
        last_ticks = now;

        // Publish last frame's counters and timings, start this frame
        if (frame_index > 0) {
            profiler_frame_end(frame_ticks);
            const ProfilerFrame *pf = profiler_get();
            uint32_t f_us = pf->last_us[PROF_FRAME];
            uint32_t idle = pf->last_us[PROF_WAIT_DISPLAY] + pf->last_us[PROF_LIMITER];
            frametime_record(f_us, f_us > idle ? f_us - idle : 0);
        }
        memstats_update();
        profiler_frame_begin();
        profiler_set_enabled(debug_profiler_enabled());
        stats_frame_begin();
        frame_index++;

        if (dt > ENGINE_MAX_DT) dt = ENGINE_MAX_DT;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        PROF_BEGIN(PROF_UPDATE);
        scene_manager_update(app->scenes, dt);
        PROF_END(PROF_UPDATE);

        // Debug tooling (Debug tab, testbed, CSV dumps), then the game's logic
        debug_menu_update();
        testbed_update();
        if (debug_consume_dump_request()) {
            stats_dump_csv(frame_index);
            profiler_dump_csv();
            profiler_rsp_dump_csv(frame_index);
            frametime_dump_csv(frame_index, engine_frame_budget_ms());
            memstats_dump_csv(frame_index);
        }
        if (app->on_frame) app->on_frame(dt);
        if (debug_consume_reset_peaks_request()) {
            profiler_reset_peaks();
            frametime_reset();
            memstats_reset_baseline();
        }

        // Render
        rdp_debug_frame_begin();   // one-frame RDP capture, if requested

        audio_poll(SND_POLL_BEFORE_DISPLAY, dt);

        // Time blocked waiting for a free framebuffer is always measured
        uint32_t t_wait = TICKS_READ();
        surface_t *fb = display_get();
        profiler_record(PROF_WAIT_DISPLAY, TICKS_DISTANCE(t_wait, TICKS_READ()));
        audio_poll(SND_POLL_AFTER_DISPLAY, dt);

        rdpq_attach(fb, &zbuf);
        PROF_BEGIN(PROF_DRAW);
        scene_manager_draw(app->scenes);
        PROF_END(PROF_DRAW);

        // Debug overlay page (hidden while the menu is open)
        if (!app->menu || !app->menu->is_open) {
            overlay_draw(engine_frame_budget_ms());
        }
        const char *tb = testbed_status();
        if (tb) {
            TextBoxConfig tbc = { .x = 12, .y = 30, .font_id = FONT_DEBUG_MONO,
                                  .color = RGBA32(0xFF, 0x80, 0x40, 0xFF) };
            text_draw(&tbc, tb);
        }
        rdpq_detach_show();
        rdp_debug_frame_end();
        audio_poll(SND_POLL_AFTER_PRESENT, dt);

        // Frame rate limiting (busy-wait until the target frame time)
        if (engine_target_fps > 0) {
            uint32_t t_limit = TICKS_READ();
            uint32_t target_ticks = TICKS_PER_SECOND / engine_target_fps;
            while (TICKS_DISTANCE(now, TICKS_READ()) < (int32_t)target_ticks) {
                // spin
            }
            profiler_record(PROF_LIMITER, TICKS_DISTANCE(t_limit, TICKS_READ()));
        }
    }
}
