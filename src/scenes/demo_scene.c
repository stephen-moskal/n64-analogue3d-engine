#include "demo_scene.h"
#include "../render/cube.h"
#include "../render/mesh.h"
#include "../render/mesh_defs.h"
#include "../render/floor.h"
#include "../render/texture.h"
#include "../input/input.h"
#include "../ui/text.h"
#include "../ui/menu.h"
#include "../audio/audio.h"
#include "../audio/sound_bank.h"

// ============================================================
// Object Data — stored in SceneObject.data for mesh objects
// ============================================================

typedef struct {
    const Mesh *mesh;
    const char *name;           // Display name for HUD
    bool auto_rotate;
    float rotate_speed_x;
    float rotate_speed_y;
} ObjectData;

// Static ObjectData storage (avoids heap allocation per object)
#define MAX_OBJECT_DATA 8
static ObjectData object_data[MAX_OBJECT_DATA];
static int object_data_count = 0;

static ObjectData *alloc_object_data(void) {
    if (object_data_count >= MAX_OBJECT_DATA) return NULL;
    return &object_data[object_data_count++];
}

// ============================================================
// Interaction mode — object selection & manipulation
// ============================================================

typedef enum { MODE_NORMAL, MODE_OBJECT_SELECT, MODE_OBJECT_TRANSFORM } InteractionMode;
typedef enum { TRANSFORM_MOVE, TRANSFORM_ROTATE, TRANSFORM_SCALE } TransformMode;

static InteractionMode interaction_mode = MODE_NORMAL;
static TransformMode transform_mode = TRANSFORM_MOVE;
static int selected_object = -1;

static const char *transform_mode_names[] = {"MOVE", "ROT", "SCALE"};

// Movement speeds for object manipulation
#define OBJ_MOVE_SPEED    3.0f
#define OBJ_MOVE_Y_SPEED  2.0f
#define OBJ_ROTATE_SPEED  0.03f
#define OBJ_SCALE_SPEED   0.5f

// ============================================================
// Collider handles
// ============================================================

static int ground_collider = -1;

// Per-object collider handles (indexed same as scene objects)
#define MAX_OBJ_COLLIDERS 8
static int obj_colliders[MAX_OBJ_COLLIDERS];
static int obj_collider_count = 0;

// ============================================================
// Raycast result (for HUD display)
// ============================================================

static bool ray_hit = false;
static CollisionResult ray_result;

// Track current camera mode to detect menu changes
static int last_camera_mode = 0;
static int last_camera_col = 0;

// External references (owned by main.c)
extern Menu start_menu;
extern int engine_target_fps;

// ============================================================
// HUD text configs — new layout
// ============================================================

// Title (top center)
static const TextBoxConfig title_text = {
    .x       = 20.0f,
    .y       = 20.0f,
    .width   = 280,
    .font_id = FONT_DEBUG_VAR,
    .color   = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .align   = ALIGN_CENTER,
};

// Left side — object stats (above geometry stats)
static const TextBoxConfig obj_stats_text = {
    .x       = 4.0f,
    .y       = 196.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0xC0, 0xC0, 0xFF, 0xFF),
};

// Left side — geometry stats
static const TextBoxConfig geom_stats_text = {
    .x       = 4.0f,
    .y       = 208.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0xFF, 0xFF, 0x00, 0xFF),
};

// Left side — FPS
static const TextBoxConfig fps_text = {
    .x       = 4.0f,
    .y       = 220.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x00, 0xFF, 0x00, 0xFF),
};

// Right side — selection info (only in object mode)
static const TextBoxConfig sel_text = {
    .x       = 4.0f,
    .y       = 196.0f,
    .width   = 312,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0xFF, 0x80, 0x40, 0xFF),
    .align   = ALIGN_RIGHT,
};

// Right side — camera mode
static const TextBoxConfig cam_text = {
    .x       = 4.0f,
    .y       = 208.0f,
    .width   = 312,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x80, 0xC0, 0xFF, 0xFF),
    .align   = ALIGN_RIGHT,
};

// Right side — camera position
static const TextBoxConfig pos_text = {
    .x       = 4.0f,
    .y       = 220.0f,
    .width   = 312,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x80, 0xC0, 0xFF, 0xFF),
    .align   = ALIGN_RIGHT,
};

// --- Menu item indices (must match main.c order) ---
#define MENU_ITEM_BG_COLOR      0
#define MENU_ITEM_DEBUG_TEXT    1
#define MENU_ITEM_CAMERA_MODE  2
#define MENU_ITEM_CAMERA_COL   3
#define MENU_ITEM_FRAME_RATE   4
#define MENU_ITEM_SOUND        5

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

static const char *camera_mode_names[] = {"ORBITAL", "FIXED", "FOLLOW"};

#define FIXED_MOVE_SPEED  150.0f
#define FIXED_Y_SPEED     5.0f

static int last_fps_option = 1;
static int last_sound_option = 0;
static float hud_fps = 0.0f;
static uint32_t hud_fps_ticks = 0;

// Scene pointer (set in demo_init, used by object_draw for selection check)
static Scene *current_scene = NULL;

// ============================================================
// Generic SceneObject callbacks
// ============================================================

static void object_update(SceneObject *obj, float dt) {
    ObjectData *data = (ObjectData *)obj->data;
    if (data->auto_rotate) {
        obj->rotation.y += data->rotate_speed_y * dt;
        obj->rotation.x += data->rotate_speed_x * dt;
    }
}

static void object_draw(SceneObject *obj, const Camera *cam, const LightConfig *light) {
    ObjectData *data = (ObjectData *)obj->data;
    mat4_t model;
    mat4_from_srt(&model, &obj->scale,
                  obj->rotation.x, obj->rotation.y, obj->rotation.z,
                  &obj->position);

    // Check if this is the selected object — draw brighter
    bool is_selected = (interaction_mode != MODE_NORMAL && selected_object >= 0 &&
                        current_scene && &current_scene->objects[selected_object] == obj);

    if (is_selected) {
        // Stack-local copy with boosted ambient
        LightConfig boosted = *light;
        boosted.ambient[0] += 0.35f;
        boosted.ambient[1] += 0.35f;
        boosted.ambient[2] += 0.35f;
        if (boosted.ambient[0] > 1.0f) boosted.ambient[0] = 1.0f;
        if (boosted.ambient[1] > 1.0f) boosted.ambient[1] = 1.0f;
        if (boosted.ambient[2] > 1.0f) boosted.ambient[2] = 1.0f;
        mesh_draw(data->mesh, &model, cam, &boosted);
    } else {
        mesh_draw(data->mesh, &model, cam, light);
    }
}

// ============================================================
// Helper: spawn a scene object with mesh
// ============================================================

static int spawn_object(Scene *scene, const char *name, const Mesh *mesh,
                        vec3_t pos, vec3_t scale, bool auto_rotate,
                        float rot_speed_x, float rot_speed_y) {
    ObjectData *data = alloc_object_data();
    if (!data) return -1;

    data->mesh = mesh;
    data->name = name;
    data->auto_rotate = auto_rotate;
    data->rotate_speed_x = rot_speed_x;
    data->rotate_speed_y = rot_speed_y;

    SceneObject obj = {
        .position = pos,
        .rotation = {0, 0, 0},
        .scale = scale,
        .active = true,
        .visible = true,
        .collider_handle = -1,
        .data = data,
        .on_update = object_update,
        .on_draw = object_draw,
    };

    return scene_add_object(scene, &obj);
}

// ============================================================
// Camera mode application
// ============================================================

static void apply_camera_mode(Scene *scene, int mode_idx) {
    switch (mode_idx) {
    case 0: // Orbital
        camera_set_mode(&scene->camera, CAMERA_MODE_ORBITAL);
        break;
    case 1: { // Fixed — elevated side view looking at origin
        camera_set_fixed(&scene->camera,
            (vec3_t){250.0f, 200.0f, 250.0f},
            (vec3_t){0.0f, 0.0f, 0.0f});
        break;
    }
    case 2: { // Follow — track first object (cube)
        SceneObject *cube_obj = scene_get_object(scene, 0);
        const vec3_t *follow_pos = cube_obj ? &cube_obj->position : NULL;
        if (follow_pos) {
            camera_set_follow_target(&scene->camera, follow_pos,
                (vec3_t){0.0f, 200.0f, -350.0f});
        }
        break;
    }
    }
}

// ============================================================
// Scene callbacks
// ============================================================

static void demo_init(Scene *scene) {
    // Store scene pointer for object_draw selection check
    current_scene = scene;

    // Reset object data pool
    object_data_count = 0;
    obj_collider_count = 0;
    for (int i = 0; i < MAX_OBJ_COLLIDERS; i++) obj_colliders[i] = -1;

    // Load cube textures
    texture_init();

    // Initialize cube mesh
    cube_init();

    // Initialize shape library
    mesh_defs_init();

    // Camera: orbital mode, default config
    camera_init(&scene->camera, &CAMERA_DEFAULT);
    scene->camera.collision_radius = 10.0f;
    scene->camera.min_y = FLOOR_Y + 10.0f;

    // --- Spawn objects ---

    // 0: Cube (textured, auto-rotating)
    int idx = spawn_object(scene, "Cube", cube_get_mesh(),
        (vec3_t){0, 0, 0}, (vec3_t){80, 80, 80},
        true, 0.15f, 0.3f);
    if (idx >= 0) {
        obj_colliders[obj_collider_count++] = collision_add_sphere(
            &scene->collision, (vec3_t){0, 0, 0}, 138.56f,
            COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }

    // 1: Pillar Left
    idx = spawn_object(scene, "Pillar L", mesh_defs_get_pillar(),
        (vec3_t){-250, 0, 0}, (vec3_t){40, 100, 40},
        false, 0, 0);
    if (idx >= 0) {
        obj_colliders[obj_collider_count++] = collision_add_sphere(
            &scene->collision, (vec3_t){-250, 0, 0}, 110.0f,
            COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }

    // 2: Pillar Right
    idx = spawn_object(scene, "Pillar R", mesh_defs_get_pillar(),
        (vec3_t){250, 0, 0}, (vec3_t){40, 100, 40},
        false, 0, 0);
    if (idx >= 0) {
        obj_colliders[obj_collider_count++] = collision_add_sphere(
            &scene->collision, (vec3_t){250, 0, 0}, 110.0f,
            COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }

    // 3: Platform (behind cube) — unit box Y [-0.25,0.25], scale Y=30 → world Y extent ±7.5
    idx = spawn_object(scene, "Platform", mesh_defs_get_platform(),
        (vec3_t){0, -92.5f, -300}, (vec3_t){80, 30, 60},
        false, 0, 0);
    if (idx >= 0) {
        obj_colliders[obj_collider_count++] = collision_add_sphere(
            &scene->collision, (vec3_t){0, -92.5f, -300}, 100.0f,
            COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }

    // 4: Pyramid (in front of cube)
    idx = spawn_object(scene, "Pyramid", mesh_defs_get_pyramid(),
        (vec3_t){0, 0, 350}, (vec3_t){60, 80, 60},
        true, 0, 0.1f);
    if (idx >= 0) {
        obj_colliders[obj_collider_count++] = collision_add_sphere(
            &scene->collision, (vec3_t){0, 0, 350}, 90.0f,
            COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }

    // Ground collider (static, covers floor area)
    ground_collider = collision_add_aabb(&scene->collision,
        (vec3_t){-1500, -180, -1500}, (vec3_t){1500, -100, 1500},
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
    collision_set_static(&scene->collision, ground_collider, true);

    // Camera collision OFF by default
    last_camera_mode = 0;
    last_camera_col = 0;
    last_fps_option = 1;
    last_sound_option = 0;
    hud_fps = 0.0f;
    hud_fps_ticks = 0;
    interaction_mode = MODE_NORMAL;
    transform_mode = TRANSFORM_MOVE;
    selected_object = -1;

    // Start background music
    snd_play_bgm(BGM_DEMO);
}

// ============================================================
// Object manipulation
// ============================================================

static void handle_object_manipulation(Scene *scene, const InputState *input,
                                        joypad_buttons_t pressed) {
    SceneObject *obj = scene_get_object(scene, selected_object);
    if (!obj) return;

    switch (transform_mode) {
    case TRANSFORM_MOVE:
        // Analog stick: move XZ
        if (input->has_input) {
            obj->position.x += input->orbit_azimuth * OBJ_MOVE_SPEED;
            obj->position.z += input->orbit_elevation * OBJ_MOVE_SPEED;
            // C-up/down: move Y
            obj->position.y -= input->zoom_delta * OBJ_MOVE_Y_SPEED;
        }
        break;

    case TRANSFORM_ROTATE:
        if (input->has_input) {
            // Analog: rotate Y axis
            obj->rotation.y += input->orbit_azimuth * OBJ_ROTATE_SPEED;
            // C-up/down: rotate X axis
            obj->rotation.x -= input->zoom_delta * OBJ_ROTATE_SPEED;
        }
        break;

    case TRANSFORM_SCALE: {
        if (input->has_input) {
            // Analog up/down: uniform scale
            float scale_delta = -input->orbit_elevation * OBJ_SCALE_SPEED;
            obj->scale.x += scale_delta;
            obj->scale.y += scale_delta;
            obj->scale.z += scale_delta;
            // Clamp minimum scale
            if (obj->scale.x < 5.0f) obj->scale.x = 5.0f;
            if (obj->scale.y < 5.0f) obj->scale.y = 5.0f;
            if (obj->scale.z < 5.0f) obj->scale.z = 5.0f;
            // C-up/down: scale Y only
            obj->scale.y -= input->zoom_delta * OBJ_SCALE_SPEED;
            if (obj->scale.y < 5.0f) obj->scale.y = 5.0f;
        }
        break;
    }
    }

    // Update this object's collider position
    if (selected_object < obj_collider_count && obj_colliders[selected_object] >= 0) {
        collision_update_sphere(&scene->collision,
            obj_colliders[selected_object], obj->position);
    }
}

// ============================================================
// Scene update
// ============================================================

static void demo_update(Scene *scene, float dt) {
    InputState input_state;
    input_update(&input_state);

    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // Menu input (START always toggles menu)
    if (pressed.start) {
        if (start_menu.is_open) {
            menu_close(&start_menu, true);
            snd_play_sfx(SFX_MENU_CLOSE);
        } else {
            menu_open(&start_menu);
            snd_play_sfx(SFX_MENU_OPEN);
        }
    }

    // --- Interaction mode handling ---

    if (!start_menu.is_open) {
        // Z trigger: toggle object select mode
        if (pressed.z) {
            if (interaction_mode == MODE_NORMAL) {
                interaction_mode = MODE_OBJECT_SELECT;
                if (selected_object < 0 && scene->object_count > 0)
                    selected_object = 0;
                snd_play_sfx(SFX_OBJ_SELECT);
            } else {
                interaction_mode = MODE_NORMAL;
                selected_object = -1;
                snd_play_sfx(SFX_OBJ_DESELECT);
            }
        }

        if (interaction_mode == MODE_OBJECT_SELECT) {
            // D-pad L/R: cycle selected object
            if (pressed.d_left && scene->object_count > 0) {
                selected_object--;
                if (selected_object < 0) selected_object = scene->object_count - 1;
                snd_play_sfx(SFX_MENU_NAV);
            }
            if (pressed.d_right && scene->object_count > 0) {
                selected_object = (selected_object + 1) % scene->object_count;
                snd_play_sfx(SFX_MENU_NAV);
            }
            // A: enter transform mode
            if (pressed.a) {
                interaction_mode = MODE_OBJECT_TRANSFORM;
                transform_mode = TRANSFORM_MOVE;
                snd_play_sfx(SFX_MODE_CHANGE);
            }
            // B: exit to normal
            if (pressed.b) {
                interaction_mode = MODE_NORMAL;
                selected_object = -1;
            }
        } else if (interaction_mode == MODE_OBJECT_TRANSFORM) {
            // A: cycle transform mode
            if (pressed.a) {
                transform_mode = (transform_mode + 1) % 3;
                snd_play_sfx(SFX_MODE_CHANGE);
            }
            // B: back to select
            if (pressed.b) {
                interaction_mode = MODE_OBJECT_SELECT;
                snd_play_sfx(SFX_OBJ_DESELECT);
            }
            // Manipulate object with analog/C-buttons
            handle_object_manipulation(scene, &input_state, pressed);
        }
    }

    // --- Camera controls (only when not transforming objects) ---

    if (start_menu.is_open) {
        int old_cursor = start_menu.cursor;
        bool was_open = start_menu.is_open;
        menu_update(&start_menu);
        if (start_menu.is_open && start_menu.cursor != old_cursor) {
            snd_play_sfx(SFX_MENU_NAV);
        }
        if (was_open && !start_menu.is_open) {
            snd_play_sfx(SFX_MENU_SELECT);
        }
    } else if (interaction_mode != MODE_OBJECT_TRANSFORM) {
        // L/R shoulder buttons: cycle camera mode
        if (pressed.l) {
            int mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
            mode = (mode + 2) % 3;
            start_menu.items[MENU_ITEM_CAMERA_MODE].selected = mode;
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }
        if (pressed.r) {
            int mode = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
            mode = (mode + 1) % 3;
            start_menu.items[MENU_ITEM_CAMERA_MODE].selected = mode;
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }

        if (input_state.has_input) {
            switch (scene->camera.mode) {
            case CAMERA_MODE_ORBITAL:
                camera_orbit(&scene->camera, input_state.orbit_azimuth,
                                             input_state.orbit_elevation);
                camera_zoom(&scene->camera, input_state.zoom_delta);
                camera_shift_target_y(&scene->camera, input_state.target_y_delta);
                break;

            case CAMERA_MODE_FIXED:
                scene->camera.fixed_position.x += input_state.orbit_azimuth * FIXED_MOVE_SPEED;
                scene->camera.fixed_position.z += input_state.orbit_elevation * FIXED_MOVE_SPEED;
                scene->camera.fixed_position.y -= input_state.zoom_delta * FIXED_Y_SPEED;
                scene->camera.fixed_target.y += input_state.target_y_delta;
                scene->camera.dirty = true;
                break;

            case CAMERA_MODE_FOLLOW:
                scene->camera.follow_offset.x += input_state.orbit_azimuth * FIXED_MOVE_SPEED;
                scene->camera.follow_offset.z += input_state.orbit_elevation * FIXED_MOVE_SPEED;
                scene->camera.follow_offset.y -= input_state.zoom_delta * FIXED_Y_SPEED;
                scene->camera.dirty = true;
                break;
            }
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
            camera_set_collision(&scene->camera, &scene->collision,
                                 COLLISION_LAYER_ENV);
        } else {
            camera_set_collision(&scene->camera, NULL, 0);
        }
        last_camera_col = cam_col;
    }

    // Check for frame rate change from menu
    int fps_opt = menu_get_value(&start_menu, MENU_ITEM_FRAME_RATE);
    if (fps_opt != last_fps_option) {
        switch (fps_opt) {
        case 0: engine_target_fps = 30; break;
        case 1: engine_target_fps = 0;  break;  // 60fps: no limiter needed (VSync caps it)
        }
        last_fps_option = fps_opt;
    }

    // Check for sound toggle from menu
    int snd_opt = menu_get_value(&start_menu, MENU_ITEM_SOUND);
    if (snd_opt != last_sound_option) {
        if (snd_opt == 0) {
            // Sound On
            snd_set_sfx_volume(100);
            snd_set_bgm_volume(80);
        } else {
            // Sound Off
            snd_set_sfx_volume(0);
            snd_set_bgm_volume(0);
        }
        last_sound_option = snd_opt;
    }

    scene->camera.dirty = true;

    // Update background color from menu
    scene->bg_color = bg_colors[menu_get_value(&start_menu, MENU_ITEM_BG_COLOR)];
}

// ============================================================
// Scene draw — floor only (objects drawn by per-object callbacks)
// ============================================================

static void demo_draw(Scene *scene) {
    texture_stats_reset();

    // Draw the checkered floor
    floor_draw(&scene->camera, &scene->lighting);
}

// ============================================================
// Post-draw — HUD and overlays (after all 3D geometry)
// ============================================================

static void demo_post_draw(Scene *scene) {
    // Count visible objects (for HUD)
    int visible_count = 0;
    for (int i = 0; i < scene->object_count; i++) {
        if (scene->objects[i].visible) visible_count++;
    }

    // Raycast from camera
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

        // Left side: object stats
        text_draw_fmt(&obj_stats_text, "OBJ:%d/%d VIS:%d",
            scene->object_count, SCENE_MAX_OBJECTS, visible_count);

        // Left side: geometry stats
        const TextureStats *ts = texture_stats_get();
        text_draw_fmt(&geom_stats_text, "T:%d U:%d COL:%d RAY:%.0f",
            ts->triangle_count, ts->upload_count,
            scene->collision.result_count,
            ray_hit ? ray_result.distance : -1.0f);

        // Left side: FPS (smoothed)
        uint32_t now = TICKS_READ();
        if (hud_fps_ticks == 0 || TICKS_DISTANCE(hud_fps_ticks, now) > (int32_t)(TICKS_PER_SECOND / 2)) {
            hud_fps = display_get_fps();
            hud_fps_ticks = now;
        }
        text_draw_fmt(&fps_text, "FPS: %.0f", hud_fps);

        // Right side: selection info (only in object mode)
        if (interaction_mode != MODE_NORMAL && selected_object >= 0) {
            SceneObject *sel = scene_get_object(scene, selected_object);
            ObjectData *data = sel ? (ObjectData *)sel->data : NULL;
            const char *name = data ? data->name : "???";
            if (interaction_mode == MODE_OBJECT_TRANSFORM) {
                text_draw_fmt(&sel_text, "SEL:%s [%s]",
                    name, transform_mode_names[transform_mode]);
            } else {
                text_draw_fmt(&sel_text, "SEL:%s", name);
            }
        }

        // Right side: camera mode
        int cam_mode_idx = menu_get_value(&start_menu, MENU_ITEM_CAMERA_MODE);
        text_draw_fmt(&cam_text, "CAM:%s%s",
            camera_mode_names[cam_mode_idx],
            scene->camera.collision_enabled ? " COL" : "");

        // Right side: camera position
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

// ============================================================
// Cleanup
// ============================================================

static void demo_cleanup(Scene *scene) {
    (void)scene;
    snd_stop_bgm();
    cube_cleanup();
    mesh_defs_cleanup();
    ground_collider = -1;
    obj_collider_count = 0;
    object_data_count = 0;
    interaction_mode = MODE_NORMAL;
    selected_object = -1;
    current_scene = NULL;
}

// ============================================================
// Scene instance
// ============================================================

static Scene demo_scene = {
    .name = "Demo Scene",
    .object_count = 0,
    .texture_count = 0,
    .world_offset = {0, 0, 0},
    .bg_color = {0x10, 0x10, 0x30, 0xFF},
    .on_init = demo_init,
    .on_update = demo_update,
    .on_draw = demo_draw,
    .on_post_draw = demo_post_draw,
    .on_cleanup = demo_cleanup,
    .loaded = false,
};

Scene *demo_scene_get(void) {
    return &demo_scene;
}
