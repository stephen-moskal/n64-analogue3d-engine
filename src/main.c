#include <libdragon.h>

#include "input/input.h"
#include "ui/text.h"
#include "ui/menu.h"
#include "scene/scene.h"
#include "scenes/demo_scene.h"
#include "audio/audio.h"
#include "render/atmosphere.h"

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
static const char *ptlight_int_opts[]   = {"0.4", "0.8", "1.2", "1.6", "2.0", "3.0", "5.0", "8.0"};
static const char *ptlight_rad_opts[]   = {"100", "150", "200", "300", "400", "600", "800", "1000"};

// Menu options — Environ tab
static const char *atmo_preset_options[] = {
    "Custom", "Clear Day", "Overcast", "Foggy", "Dense Fog", "Sunset", "Dusk", "Night"
};
static const char *fog_toggle_options[] = {"Off", "On"};
static const char *fog_near_options[]   = {"50", "100", "150", "200", "300", "400"};
static const char *fog_far_options[]    = {"400", "600", "800", "1000", "1200", "1400"};
static const char *fog_color_options[]  = {"Grey", "Blue", "White", "Warm", "Purple", "Dark"};
static const char *sky_toggle_options[] = {"Off", "On"};

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

    // Tab 0: Settings
    int tab_s = menu_add_tab(&start_menu, "Settings");
    menu_add_item(&start_menu, tab_s, "BG Color", bg_options, 7, 5);
    menu_add_item(&start_menu, tab_s, "Debug Text", toggle_options, 2, 0);
    menu_add_item(&start_menu, tab_s, "Camera", camera_mode_options, 3, 0);
    menu_add_item(&start_menu, tab_s, "Cam Collide", camera_col_options, 2, 0);
    menu_add_item(&start_menu, tab_s, "Frame Rate", fps_options, 2, 1);  // Default: 60

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
    menu_add_item(&start_menu, tab_l, "Pt Intensity", ptlight_int_opts, 8, 2);  // Default: 1.2
    menu_add_item(&start_menu, tab_l, "Pt Radius",   ptlight_rad_opts, 8, 2);   // Default: 200

    // Tab 3: Environ
    int tab_e = menu_add_tab(&start_menu, "Environ");
    menu_add_item(&start_menu, tab_e, "Preset",    atmo_preset_options, 8, 0);  // Default: Custom
    menu_add_item(&start_menu, tab_e, "Fog",       fog_toggle_options, 2, 0);   // Default: Off
    menu_add_item(&start_menu, tab_e, "Fog Near",  fog_near_options, 6, 3);     // Default: 200
    menu_add_item(&start_menu, tab_e, "Fog Far",   fog_far_options, 6, 4);      // Default: 1200
    menu_add_item(&start_menu, tab_e, "Fog Color", fog_color_options, 6, 0);    // Default: Grey
    menu_add_item(&start_menu, tab_e, "Sky",       sky_toggle_options, 2, 0);   // Default: Off

    // Initialize audio and atmosphere
    snd_init();
    atmosphere_init();

    // Allocate Z-buffer (shared across all scenes)
    surface_t zbuf = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Initialize scene manager and load demo scene
    SceneManager scene_mgr;
    scene_manager_init(&scene_mgr);
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);

    debugf("SMozN64 Dev Engine\n");

    // Main game loop — variable timestep (logic runs once per render frame)
    uint32_t last_ticks = TICKS_READ();

    while (1) {
        // Measure real elapsed time since last frame
        uint32_t now = TICKS_READ();
        float dt = (float)TICKS_DISTANCE(last_ticks, now) / (float)TICKS_PER_SECOND;
        last_ticks = now;

        if (dt > MAX_FRAME_DT) dt = MAX_FRAME_DT;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        // Update game logic once per frame with actual elapsed time
        scene_manager_update(&scene_mgr, dt);

        // Render
        surface_t *fb = display_get();
        rdpq_attach(fb, &zbuf);
        scene_manager_draw(&scene_mgr);
        rdpq_detach_show();

        // Feed audio mixer
        snd_update();

        // Frame rate limiting (busy-wait until target frame time)
        if (engine_target_fps > 0) {
            uint32_t target_ticks = TICKS_PER_SECOND / engine_target_fps;
            while (TICKS_DISTANCE(now, TICKS_READ()) < (int32_t)target_ticks) {
                // spin
            }
        }
    }

    // Cleanup (unreachable in normal operation)
    surface_free(&zbuf);
    text_cleanup();

    return 0;
}
