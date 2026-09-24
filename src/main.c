#include <libdragon.h>

#include "input/input.h"
#include "input/action.h"
#include "ui/text.h"
#include "ui/menu.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"
#include "scenes/benchmark_scene.h"
#include "audio/audio.h"
#include "render/atmosphere.h"
#include "debug/engine_debug.h"
#include "debug/debug_menu.h"
#include "debug/stats.h"
#include "debug/profiler.h"
#include "debug/memstats.h"
#include "debug/frametime.h"
#include "debug/overlay.h"
#include "debug/rdp_debug.h"
#include "debug/testbed.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

#define MAX_FRAME_DT   0.1f   // Cap to prevent spiral of death

// Global menu (accessible by scenes via extern)
Menu start_menu;

// Frame rate target (set by scenes via extern, 0 = no limiter)
int engine_target_fps = 0;

// Menu options — Settings tab
static const char *bg_options[] = {
    "Dark Blue", "Black", "Dark Red", "Dark Green", "Dark Purple",
    "Light Blue", "White",
};
static const char *toggle_options[] = {"On", "Off"};
static const char *camera_mode_options[] = {"Orbital", "Fixed", "Follow"};
static const char *camera_col_options[] = {"Off", "On"};
static const char *fps_options[] = {"30", "60"};
static const char *ui_style_options[] = {"Debug", "Classic", "Minimal"};   // order of ui_styles[]
static const char *reset_options[] = {"---", "Reset!"};

// Menu options — Sound tab
static const char *sound_options[] = {"On", "Off"};
static const char *vol_options[] = {
    "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"
};

// Menu options — Lighting tab
static const char *sun_dir_options[]    = {"Front", "Side", "Top", "Sunset", "Dawn"};
static const char *sun_color_options[]  = {"Warm", "Cool", "Neutral", "Golden"};
static const char *brightness_options[] = {"20%", "40%", "60%", "80%", "100%"};
static const char *ambient_options[]    = {"10%", "20%", "30%", "40%", "50%"};
static const char *shadow_options[]     = {"Off", "Blob", "Projected"};
static const char *shadow_dk_options[]  = {"Light", "Medium", "Dark"};
static const char *ptlight_options[]    = {"Off", "On"};
static const char *ptlight_color_opts[] = {"Warm", "Cool", "Red", "Green", "Blue", "White"};
static const char *ptlight_int_opts[]   = {"0.4", "0.8", "1.2", "2.0", "3.0", "5.0", "8.0", "12", "16", "24"};
static const char *ptlight_rad_opts[]   = {"100", "200", "300", "400", "600", "800", "1000", "1200", "1500", "2000"};

// Menu options — Environ tab
static const char *atmo_preset_options[] = {
    "Custom", "Clear Day", "Overcast", "Foggy", "Dense Fog", "Sunset", "Dusk", "Night"
};
static const char *fog_toggle_options[] = {"Off", "On"};
static const char *fog_near_options[]   = {"50", "100", "150", "200", "300", "400"};
static const char *fog_far_options[]    = {"400", "600", "800", "1000", "1200", "1400"};
static const char *fog_color_options[]  = {"Grey", "Blue", "White", "Warm", "Purple", "Dark"};
static const char *sky_toggle_options[] = {"Off", "On"};

// Menu options — Controls tab (indices match PhysicalButton enum)
static const char *btn_options[] = {
    "A", "B", "Z", "L", "R",
    "D-Up", "D-Down", "D-Left", "D-Right",
    "C-Up", "C-Down", "C-Left", "C-Right"
};

// Fill the audio buffers if this is the point of the frame the sound module
// is set to poll at (SndPollPoint; the audio benchmark compares them)
static inline void audio_poll(SndPollPoint point, float dt) {
    if (snd_get_poll_point() != point) return;
    PROF_BEGIN(PROF_AUDIO);
    snd_update(dt);
    PROF_END(PROF_AUDIO);
}

int main(void) {
    // Initialize debug output
    debug_init_isviewer();
    debug_init_usblog();

    // Initialize display (320x240, 16-bit color, triple buffered)
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Memory stats: RDRAM size, heap, stack high-water mark (paints the stack now)
    memstats_init(FB_COUNT, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize RDP command queue
    rdpq_init();
    // RDP validator (rdpq_debug_start) is toggled from the Debug tab in debug builds.
    // It is off at boot: its CPU cost can push frames past 16.7 ms (defect D18).

    // Initialize DFS (required before sprite_load)
    dfs_init(DFS_DEFAULT_LOCATION);

    // Initialize subsystems
    action_init();
    text_init();

    // Initialize menu (global overlay, persists across scenes)
    menu_init(&start_menu, "Start Menu");

    // Tab 0: Settings
    int tab_s = menu_add_tab(&start_menu, "Settings");
    menu_add_item(&start_menu, tab_s, "BG Color", bg_options, 7, 5);
    menu_add_item(&start_menu, tab_s, "Debug Text", toggle_options, 2, 0);
    menu_add_item(&start_menu, tab_s, "Camera", camera_mode_options, 3, 0);
    menu_add_item(&start_menu, tab_s, "Cam Collide", camera_col_options, 2, 0);
    menu_add_item(&start_menu, tab_s, "Frame Rate", fps_options, 2, 1);  // Default: 60
    menu_add_item(&start_menu, tab_s, "Reset Scene", reset_options, 2, 0);  // Default: ---
    menu_add_item(&start_menu, tab_s, "UI Style", ui_style_options, 3, 0);  // Default: Debug (ui_styles[])

    // Tab 1: Sound
    int tab_a = menu_add_tab(&start_menu, "Sound");
    menu_add_item(&start_menu, tab_a, "Master", sound_options, 2, 1);    // Default: Off
    menu_add_item(&start_menu, tab_a, "SFX Vol", vol_options, 11, 8);    // Default: 80%
    menu_add_item(&start_menu, tab_a, "BGM Vol", vol_options, 11, 6);    // Default: 60%

    // Tab 2: Lighting
    int tab_l = menu_add_tab(&start_menu, "Lighting");
    menu_add_item(&start_menu, tab_l, "Sun Dir",     sun_dir_options, 5, 0);     // Default: Front
    menu_add_item(&start_menu, tab_l, "Sun Color",   sun_color_options, 4, 0);   // Default: Warm
    menu_add_item(&start_menu, tab_l, "Brightness",  brightness_options, 5, 4);  // Default: 100%
    menu_add_item(&start_menu, tab_l, "Ambient",     ambient_options, 5, 1);     // Default: 20%
    menu_add_item(&start_menu, tab_l, "Shadows",     shadow_options, 3, 0);      // Default: Off
    menu_add_item(&start_menu, tab_l, "Shadow Dark", shadow_dk_options, 3, 1);   // Default: Medium
    menu_add_item(&start_menu, tab_l, "Pt Lights",   ptlight_options, 2, 0);     // Default: Off
    menu_add_item(&start_menu, tab_l, "Pt Color",    ptlight_color_opts, 6, 0); // Default: Warm
    menu_add_item(&start_menu, tab_l, "Pt Intensity", ptlight_int_opts, 10, 2);  // Default: 1.2
    menu_add_item(&start_menu, tab_l, "Pt Radius",   ptlight_rad_opts, 10, 2);   // Default: 200

    // Tab 3: Environ
    int tab_e = menu_add_tab(&start_menu, "Environ");
    menu_add_item(&start_menu, tab_e, "Preset",    atmo_preset_options, 8, 0);  // Default: Custom
    menu_add_item(&start_menu, tab_e, "Fog",       fog_toggle_options, 2, 0);   // Default: Off
    menu_add_item(&start_menu, tab_e, "Fog Near",  fog_near_options, 6, 3);     // Default: 200
    menu_add_item(&start_menu, tab_e, "Fog Far",   fog_far_options, 6, 4);      // Default: 1200
    menu_add_item(&start_menu, tab_e, "Fog Color", fog_color_options, 6, 0);    // Default: Grey
    menu_add_item(&start_menu, tab_e, "Sky",       sky_toggle_options, 2, 0);   // Default: Off

    // Tab 4: Controls (indices match GameAction enum; option indices match PhysicalButton enum)
    int tab_c = menu_add_tab(&start_menu, "Controls");
    menu_add_item(&start_menu, tab_c, "Confirm",    btn_options, 13, BTN_A);
    menu_add_item(&start_menu, tab_c, "Cancel",     btn_options, 13, BTN_B);
    menu_add_item(&start_menu, tab_c, "Select",     btn_options, 13, BTN_Z);
    menu_add_item(&start_menu, tab_c, "Cam Next",   btn_options, 13, BTN_R);
    menu_add_item(&start_menu, tab_c, "Cam Prev",   btn_options, 13, BTN_L);
    menu_add_item(&start_menu, tab_c, "Cycle Next", btn_options, 13, BTN_D_RIGHT);
    menu_add_item(&start_menu, tab_c, "Cycle Prev", btn_options, 13, BTN_D_LEFT);
    menu_add_item(&start_menu, tab_c, "Zoom In",    btn_options, 13, BTN_C_UP);
    menu_add_item(&start_menu, tab_c, "Zoom Out",   btn_options, 13, BTN_C_DOWN);
    menu_add_item(&start_menu, tab_c, "Shift Up",   btn_options, 13, BTN_C_RIGHT);
    menu_add_item(&start_menu, tab_c, "Shift Down", btn_options, 13, BTN_C_LEFT);

    // Tab 5: Debug (Phase 1 tooling toggles — see src/debug/debug_menu.h)
    int tab_d = menu_add_tab(&start_menu, "Debug");
    debug_menu_init(&start_menu, tab_d);

    // Initialize audio and atmosphere
    snd_init();
    atmosphere_init();

    // Allocate Z-buffer (shared across all scenes)
    surface_t zbuf = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize scene manager and load demo scene
    SceneManager scene_mgr;
    scene_manager_init(&scene_mgr);
    testbed_init(&scene_mgr, &start_menu);
#if defined(ENGINE_BOOT_BENCHMARK) && ENGINE_BOOT_BENCHMARK
    // Unattended benchmark run: make BENCH=1 [BENCH_KIND=<kind>] ->
    // engine-debug-bench.z64 (the debug build: release compiles debugf out
    // and prints no CSV)
#ifndef ENGINE_BOOT_BENCHMARK_KIND
#define ENGINE_BOOT_BENCHMARK_KIND BENCH_ALL
#endif
    benchmark_scene_configure(ENGINE_BOOT_BENCHMARK_KIND);
    debug_menu_set_active_scene(1);
    scene_manager_switch(&scene_mgr, benchmark_scene_get(), TRANSITION_CUT, 0);
#else
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);
#endif

    debugf("SMozN64 Dev Engine [%s build, %s %s]\n", ENGINE_BUILD_NAME, __DATE__, __TIME__);

    // Main game loop — variable timestep (logic runs once per render frame)
    uint32_t last_ticks = TICKS_READ();

    uint32_t frame_index = 0;
    profiler_init();

    while (1) {
        // Measure real elapsed time since last frame
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

        if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        // Update game logic once per frame with actual elapsed time
        PROF_BEGIN(PROF_UPDATE);
        scene_manager_update(&scene_mgr, dt);
        PROF_END(PROF_UPDATE);

        debug_menu_update();
        testbed_update();
        if (debug_consume_dump_request()) {
            float budget_ms = (engine_target_fps == 30) ? 33.33f : 16.67f;
            stats_dump_csv(frame_index);
            profiler_dump_csv();
            profiler_rsp_dump_csv(frame_index);
            frametime_dump_csv(frame_index, budget_ms);
            memstats_dump_csv(frame_index);
        }
        // Scene switching from the Debug tab, and returning when a benchmark ends
        int req_scene, req_bench;
        if (debug_consume_scene_request(&req_scene, &req_bench)) {
            if (req_scene == 1) {
                benchmark_scene_configure((BenchKind)req_bench);
                scene_manager_switch(&scene_mgr, benchmark_scene_get(), TRANSITION_FADE_BLACK, 3.0f);
            } else {
                scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_FADE_BLACK, 3.0f);
            }
        }
        if (scene_manager_current(&scene_mgr) == benchmark_scene_get() &&
            benchmark_scene_finished() && !scene_manager_is_transitioning(&scene_mgr)) {
            debug_menu_set_active_scene(0);
            scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_FADE_BLACK, 3.0f);
        }
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
        scene_manager_draw(&scene_mgr);
        PROF_END(PROF_DRAW);

        // Debug overlay page (hidden while the menu is open)
        if (!start_menu.is_open) {
            overlay_draw((engine_target_fps == 30) ? 33.33f : 16.67f);
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

        // Frame rate limiting (busy-wait until target frame time)
        if (engine_target_fps > 0) {
            uint32_t t_limit = TICKS_READ();
            uint32_t target_ticks = TICKS_PER_SECOND / engine_target_fps;
            while (TICKS_DISTANCE(now, TICKS_READ()) < (int32_t)target_ticks) {
                // spin
            }
            profiler_record(PROF_LIMITER, TICKS_DISTANCE(t_limit, TICKS_READ()));
        }
    }

    // Cleanup (unreachable in normal operation)
    surface_free(&zbuf);
    text_cleanup();

    return 0;
}
