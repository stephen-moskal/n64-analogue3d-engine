#include <libdragon.h>

#include "input/input.h"
#include "ui/text.h"
#include "ui/menu.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

// Fixed timestep: game logic always ticks at 30Hz
#define LOGIC_HZ       30
#define LOGIC_DT       (1.0f / LOGIC_HZ)
#define MAX_FRAME_DT   0.1f   // Cap to prevent spiral of death

// Global menu (accessible by scenes via extern)
Menu start_menu;

// Frame rate target (set by scenes via extern, 0 = unlimited)
int engine_target_fps = 0;

// Menu options
static const char *bg_options[] = {
    "Dark Blue", "Black", "Dark Red", "Dark Green", "Dark Purple",
    "Light Blue", "White",
};
static const char *toggle_options[] = {"On", "Off"};
static const char *camera_mode_options[] = {"Orbital", "Fixed", "Follow"};
static const char *camera_col_options[] = {"Off", "On"};
static const char *fps_options[] = {"30", "60", "Unlimited"};

int main(void) {
    // Initialize debug output
    debug_init_isviewer();
    debug_init_usblog();

    // Initialize display (320x240, 16-bit color, triple buffered)
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Initialize RDP command queue
    rdpq_init();
    // rdpq_debug_start();  // Uncomment for RDP validation debugging

    // Initialize DFS (required before sprite_load)
    dfs_init(DFS_DEFAULT_LOCATION);

    // Initialize subsystems
    input_init();
    text_init();

    // Initialize menu (global overlay, persists across scenes)
    menu_init(&start_menu, "Start Menu");
    menu_add_item(&start_menu, "BG Color", bg_options, 7, 0);
    menu_add_item(&start_menu, "Debug Text", toggle_options, 2, 0);
    menu_add_item(&start_menu, "Camera", camera_mode_options, 3, 0);
    menu_add_item(&start_menu, "Cam Collide", camera_col_options, 2, 0);
    menu_add_item(&start_menu, "Frame Rate", fps_options, 3, 2);  // Default: Unlimited

    // Allocate Z-buffer (shared across all scenes)
    surface_t zbuf = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize scene manager and load demo scene
    SceneManager scene_mgr;
    scene_manager_init(&scene_mgr);
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);

    debugf("SMozN64 Dev Engine (Fixed Timestep: %dHz logic)\n", LOGIC_HZ);

    // Main game loop — fixed timestep accumulator
    float accumulator = 0.0f;
    uint32_t last_ticks = TICKS_READ();

    while (1) {
        // Measure real elapsed time since last frame
        uint32_t now = TICKS_READ();
        float real_dt = (float)TICKS_DISTANCE(last_ticks, now) / (float)TICKS_PER_SECOND;
        last_ticks = now;

        if (real_dt > MAX_FRAME_DT) real_dt = MAX_FRAME_DT;
        if (real_dt <= 0.0f) real_dt = LOGIC_DT;
        accumulator += real_dt;

        // Run game logic in fixed-size steps (30Hz)
        while (accumulator >= LOGIC_DT) {
            scene_manager_update(&scene_mgr, LOGIC_DT);
            accumulator -= LOGIC_DT;
        }

        // Render at display rate
        surface_t *fb = display_get();
        rdpq_attach(fb, &zbuf);
        scene_manager_draw(&scene_mgr);
        rdpq_detach_show();

        // Frame rate limiting (busy-wait until target frame time)
        if (engine_target_fps > 0) {
            int32_t target_ticks = TICKS_PER_SECOND / engine_target_fps;
            while (TICKS_DISTANCE(now, TICKS_READ()) < target_ticks) {
                // spin
            }
        }
    }

    // Cleanup (unreachable in normal operation)
    surface_free(&zbuf);
    text_cleanup();

    return 0;
}
