// The demo game: the Start menu, the scenes and the switches between them.
// The engine core (src/engine/engine.c) brings the hardware up and runs the
// frame loop.

#include <libdragon.h>

#include "engine/engine.h"
#include "input/action.h"
#include "ui/menu.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"
#include "scenes/benchmark_scene.h"
#include "debug/debug_menu.h"
#include "debug/testbed.h"

// Global menu (accessible by scenes via extern)
Menu start_menu;

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

static SceneManager scene_mgr;

// Game logic that runs once per frame after the scene update: scene switches
// requested from the Debug tab, and back to the demo when a benchmark ends
static void app_frame(float dt) {
    (void)dt;
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
}

int main(void) {
    engine_init();

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

    // Scene manager and the first scene
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

    engine_run(&(EngineApp){ .scenes = &scene_mgr, .menu = &start_menu, .on_frame = app_frame });
    return 0;
}
