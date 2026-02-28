#include <libdragon.h>

#include "input/input.h"
#include "ui/text.h"
#include "ui/menu.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

// Global menu (accessible by scenes via extern)
Menu start_menu;

// Menu options
static const char *bg_options[] = {
    "Dark Blue", "Black", "Dark Red", "Dark Green", "Dark Purple",
    "Light Blue", "White",
};
static const char *toggle_options[] = {"On", "Off"};
static const char *camera_mode_options[] = {"Orbital", "Fixed", "Follow"};
static const char *camera_col_options[] = {"Off", "On"};

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

    // Allocate Z-buffer (shared across all scenes)
    surface_t zbuf = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize scene manager and load demo scene
    SceneManager scene_mgr;
    scene_manager_init(&scene_mgr);
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);

    debugf("SMozN64 Dev Engine (Scene System)\n");

    // Main game loop
    while (1) {
        // Compute delta time
        // (Scenes handle their own FPS display; this dt is for scene_manager)
        float dt = 1.0f / 30.0f;  // Approximate; scenes can compute precise FPS

        // Update scene manager (calls scene update, camera, collision)
        scene_manager_update(&scene_mgr, dt);

        // Begin frame
        surface_t *fb = display_get();
        rdpq_attach(fb, &zbuf);

        // Draw scene + transition overlay
        scene_manager_draw(&scene_mgr);

        // End frame
        rdpq_detach_show();
    }

    // Cleanup (unreachable in normal operation)
    surface_free(&zbuf);
    text_cleanup();

    return 0;
}
