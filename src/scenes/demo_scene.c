#include "demo_scene.h"
#include "../render/cube.h"
#include "../render/floor.h"
#include "../render/texture.h"
#include "../input/input.h"
#include "../ui/text.h"
#include "../ui/menu.h"

// Collider handles
static int cube_collider = -1;
static int ground_collider = -1;

// Cube position (static for now, but follow mode tracks this)
static vec3_t cube_position = {0.0f, 0.0f, 0.0f};

// Raycast result (for HUD display)
static bool ray_hit = false;
static CollisionResult ray_result;

// Track current camera mode to detect menu changes
static int last_camera_mode = 0;
static int last_camera_col = 0;

// External references (owned by main.c)
extern Menu start_menu;
extern int engine_target_fps;

// Text configs for debug HUD
static const TextBoxConfig title_text = {
    .x       = 20.0f,
    .y       = 20.0f,
    .width   = 280,
    .font_id = FONT_DEBUG_VAR,
    .color   = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .align   = ALIGN_CENTER,
};

static const TextBoxConfig fps_text = {
    .x       = 4.0f,
    .y       = 232.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x00, 0xFF, 0x00, 0xFF),
};

static const TextBoxConfig stats_text = {
    .x       = 4.0f,
    .y       = 220.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0xFF, 0xFF, 0x00, 0xFF),
};

static const TextBoxConfig mode_text = {
    .x       = 4.0f,
    .y       = 208.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x80, 0xC0, 0xFF, 0xFF),
};

static const TextBoxConfig pos_text = {
    .x       = 4.0f,
    .y       = 196.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x80, 0xC0, 0xFF, 0xFF),
};

// --- Menu item indices (must match main.c order) ---
#define MENU_ITEM_BG_COLOR      0
#define MENU_ITEM_DEBUG_TEXT    1
#define MENU_ITEM_CAMERA_MODE  2
#define MENU_ITEM_CAMERA_COL   3
#define MENU_ITEM_FRAME_RATE   4

// Background color options
static const color_t bg_colors[] = {
    {0x10, 0x10, 0x30, 0xFF},  // Dark Blue (default)
    {0x00, 0x00, 0x00, 0xFF},  // Black
    {0x30, 0x10, 0x10, 0xFF},  // Dark Red
    {0x10, 0x30, 0x10, 0xFF},  // Dark Green
    {0x20, 0x10, 0x30, 0xFF},  // Dark Purple
    {0x60, 0x80, 0xD0, 0xFF},  // Light Blue
    {0xFF, 0xFF, 0xFF, 0xFF},  // White
};

// Camera mode names (for HUD)
static const char *camera_mode_names[] = {"ORBITAL", "FIXED", "FOLLOW"};

// Movement speed for fixed camera (larger scale than orbital since it's direct translation)
#define FIXED_MOVE_SPEED  150.0f
#define FIXED_Y_SPEED     5.0f

// Track frame rate menu option
static int last_fps_option = 2;  // Default: Unlimited

// Smoothed FPS display (update every ~0.5s to avoid flickering)
static float hud_fps = 0.0f;
static uint32_t hud_fps_ticks = 0;

// --- Camera mode application ---

static void apply_camera_mode(Scene *scene, int mode_idx) {
    switch (mode_idx) {
    case 0: // Orbital
        camera_set_mode(&scene->camera, CAMERA_MODE_ORBITAL);
        break;
    case 1: // Fixed — elevated side view looking at origin
        camera_set_fixed(&scene->camera,
            (vec3_t){250.0f, 200.0f, 250.0f},
            (vec3_t){0.0f, 0.0f, 0.0f});
        break;
    case 2: // Follow — track cube position with offset
        camera_set_follow_target(&scene->camera, &cube_position,
            (vec3_t){0.0f, 200.0f, -350.0f});
        break;
    }
}

// --- Scene callbacks ---

static void demo_init(Scene *scene) {
    // Load cube textures (slots 0-5)
    texture_init();

    // Initialize cube geometry
    cube_init();

    // Camera: orbital mode, default config
    camera_init(&scene->camera, &CAMERA_DEFAULT);
    scene->camera.collision_radius = 10.0f;         // Overlap pushout vs sphere colliders
    scene->camera.min_y = FLOOR_Y + 10.0f;          // Hard Y clamp above floor

    // Add colliders with proper layers:
    // Cube: DEFAULT layer only (not ENV, so camera collision ignores it)
    cube_collider = collision_add_sphere(&scene->collision,
        (vec3_t){0, 0, 0}, 138.56f,
        COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);

    // Ground: DEFAULT + ENV layers (camera raycast tests against ENV)
    // Extends ±1500 in X/Z to cover camera at MAX_DISTANCE=1000
    ground_collider = collision_add_aabb(&scene->collision,
        (vec3_t){-1500, -180, -1500}, (vec3_t){1500, -100, 1500},
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
    collision_set_static(&scene->collision, ground_collider, true);

    // Camera collision OFF by default (menu can toggle it)
    last_camera_mode = 0;
    last_camera_col = 0;
    last_fps_option = 2;
    hud_fps = 0.0f;
    hud_fps_ticks = 0;
}

static void demo_update(Scene *scene, float dt) {
    // Input
    InputState input_state;
    input_update(&input_state);

    // Menu input
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start) {
        if (start_menu.is_open) menu_close(&start_menu, true);
        else menu_open(&start_menu);
    }

    // L/R shoulder buttons: cycle camera mode
    if (!start_menu.is_open) {
        if (pressed.l) {
            int mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
            mode = (mode + 2) % 3;  // Previous (wrap around)
            start_menu.items[MENU_ITEM_CAMERA_MODE].selected = mode;
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }
        if (pressed.r) {
            int mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
            mode = (mode + 1) % 3;  // Next
            start_menu.items[MENU_ITEM_CAMERA_MODE].selected = mode;
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }
    }

    if (start_menu.is_open) {
        menu_update(&start_menu);
    } else if (input_state.has_input) {
        switch (scene->camera.mode) {
        case CAMERA_MODE_ORBITAL:
            camera_orbit(&scene->camera, input_state.orbit_azimuth,
                                         input_state.orbit_elevation);
            camera_zoom(&scene->camera, input_state.zoom_delta);
            camera_shift_target_y(&scene->camera, input_state.target_y_delta);
            break;

        case CAMERA_MODE_FIXED:
            // Analog stick: translate camera on XZ plane
            scene->camera.fixed_position.x += input_state.orbit_azimuth * FIXED_MOVE_SPEED;
            scene->camera.fixed_position.z += input_state.orbit_elevation * FIXED_MOVE_SPEED;
            // C-up/C-down: move camera Y
            scene->camera.fixed_position.y -= input_state.zoom_delta * FIXED_Y_SPEED;
            // C-left/C-right: shift look-at target Y
            scene->camera.fixed_target.y += input_state.target_y_delta;
            scene->camera.dirty = true;
            break;

        case CAMERA_MODE_FOLLOW:
            // Analog stick: adjust follow offset XZ
            scene->camera.follow_offset.x += input_state.orbit_azimuth * FIXED_MOVE_SPEED;
            scene->camera.follow_offset.z += input_state.orbit_elevation * FIXED_MOVE_SPEED;
            // C-up/C-down: adjust follow offset Y
            scene->camera.follow_offset.y -= input_state.zoom_delta * FIXED_Y_SPEED;
            scene->camera.dirty = true;
            break;
        }
    }

    // Check for camera mode change from menu
    int cam_mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
    if (cam_mode != last_camera_mode) {
        apply_camera_mode(scene, cam_mode);
        last_camera_mode = cam_mode;
    }

    // Check for camera collision toggle from menu
    int cam_col = menu_get_value(&start_menu, MENU_ITEM_CAMERA_COL);
    if (cam_col != last_camera_col) {
        if (cam_col == 1) {
            // On — only collide with ENV layer (avoids cube sphere)
            camera_set_collision(&scene->camera, &scene->collision,
                                 COLLISION_LAYER_ENV);
        } else {
            // Off
            camera_set_collision(&scene->camera, NULL, 0);
        }
        last_camera_col = cam_col;
    }

    // Check for frame rate change from menu
    int fps_opt = menu_get_value(&start_menu, MENU_ITEM_FRAME_RATE);
    if (fps_opt != last_fps_option) {
        switch (fps_opt) {
        case 0: engine_target_fps = 30; break;
        case 1: engine_target_fps = 60; break;
        case 2: engine_target_fps = 0;  break;  // Unlimited
        }
        last_fps_option = fps_opt;
    }

    scene->camera.dirty = true;

    // Auto-rotate cube
    cube_update(dt);

    // Update collider positions (collision_test_all is called by scene system)
    collision_update_sphere(&scene->collision, cube_collider, cube_position);

    // Update background color from menu
    scene->bg_color = bg_colors[menu_get_value(&start_menu, MENU_ITEM_BG_COLOR)];
}

static void demo_draw(Scene *scene) {
    // Reset texture stats each render frame (not in update, which runs at 30Hz)
    texture_stats_reset();

    // Draw the checkered floor
    floor_draw(&scene->camera, &scene->lighting);

    // Draw the cube
    cube_draw(&scene->camera, &scene->lighting);

    // Raycast from camera (camera is updated by scene system before draw)
    Ray cam_ray;
    cam_ray.origin = scene->camera.position;
    cam_ray.direction = scene->camera.view_dir;
    cam_ray.max_distance = 1000.0f;
    ray_hit = collision_raycast(&scene->collision, &cam_ray,
                                COLLISION_LAYER_ALL, &ray_result);

    // Debug text overlay
    bool show_debug = (menu_get_value(&start_menu, MENU_ITEM_DEBUG_TEXT) == 0);
    if (show_debug) {
        text_draw(&title_text, "SMozN64 Dev Engine");

        const TextureStats *ts = texture_stats_get();
        text_draw_fmt(&stats_text, "T:%d U:%d COL:%d RAY:%.0f",
            ts->triangle_count, ts->upload_count,
            scene->collision.result_count,
            ray_hit ? ray_result.distance : -1.0f);
        // Update displayed FPS every ~0.5s to avoid flickering
        uint32_t now = TICKS_READ();
        if (hud_fps_ticks == 0 || TICKS_DISTANCE(hud_fps_ticks, now) > (int32_t)(TICKS_PER_SECOND / 2)) {
            hud_fps = display_get_fps();
            hud_fps_ticks = now;
        }
        text_draw_fmt(&fps_text, "FPS: %.0f", hud_fps);

        // Show camera mode + position
        int cam_mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
        text_draw_fmt(&mode_text, "CAM:%s%s",
            camera_mode_names[cam_mode],
            scene->camera.collision_enabled ? " COL" : "");
        text_draw_fmt(&pos_text, "XYZ:%.0f,%.0f,%.0f",
            scene->camera.position.x,
            scene->camera.position.y,
            scene->camera.position.z);
    }

    // Menu overlay
    if (start_menu.is_open) {
        menu_draw(&start_menu);
    }
}

static void demo_cleanup(Scene *scene) {
    (void)scene;
    cube_cleanup();
    cube_collider = -1;
    ground_collider = -1;
}

// --- Scene instance ---

static Scene demo_scene = {
    .name = "Demo Scene",
    .object_count = 0,
    .texture_count = 0,   // We load textures manually in on_init
    .world_offset = {0, 0, 0},
    .bg_color = {0x10, 0x10, 0x30, 0xFF},
    .on_init = demo_init,
    .on_update = demo_update,
    .on_draw = demo_draw,
    .on_cleanup = demo_cleanup,
    .loaded = false,
};

Scene *demo_scene_get(void) {
    return &demo_scene;
}
