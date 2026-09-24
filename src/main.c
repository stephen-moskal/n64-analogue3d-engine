// The demo game: the Start menu, the scenes and the switches between them.
// The engine core (src/engine/engine.c) brings the hardware up and runs the
// frame loop.

#include <libdragon.h>

#include "engine/engine.h"
#include "ui/menu.h"
#include "ui/settings.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"
#include "scenes/benchmark_scene.h"
#include "debug/debug_menu.h"
#include "debug/testbed.h"

// Global menu (accessible by scenes via extern): the game's options
// (ui/settings.c) and the Debug tab
Menu start_menu;

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

    // The Start menu (a global overlay, kept across scenes): the game's
    // options, tabs 0-4 (ui/settings.c), then the Debug tab
    menu_init(&start_menu, "Start Menu");
    settings_init(&start_menu);

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
#if defined(ENGINE_BOOT_VALIDATOR) && ENGINE_BOOT_VALIDATOR
    // make BENCH=1 BENCH_VALIDATOR=1: Debug > RDP Check on; the Debug tab starts
    // the validator at the first frame boundary, as when toggled by hand
    menu_set_value(&start_menu, tab_d, DBG_ITEM_RDP_CHECK, 1);
#endif
    debug_menu_set_active_scene(1);
    scene_manager_switch(&scene_mgr, benchmark_scene_get(), TRANSITION_CUT, 0);
#else
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);
#endif

    engine_run(&(EngineApp){ .scenes = &scene_mgr, .menu = &start_menu, .on_frame = app_frame });
    return 0;
}
