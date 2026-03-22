#include "demo_scene.h"
#include "../render/cube.h"
#include "../render/mesh.h"
#include "../render/mesh_defs.h"
#include "../render/floor.h"
#include "../render/texture.h"
#include "../input/input.h"
#include "../input/action.h"
#include "../ui/text.h"
#include "../ui/menu.h"
#include "../render/billboard.h"
#include "../render/shadow.h"
#include "../render/particle.h"
#include "../render/atmosphere.h"
#include "../audio/audio.h"
#include "../audio/sound_bank.h"
#include "../physics/physics.h"

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

// Static BillboardData storage (same pattern as ObjectData)
#define MAX_BILLBOARD_DATA 8
static BillboardData billboard_data[MAX_BILLBOARD_DATA];
static int billboard_data_count = 0;

static BillboardData *alloc_billboard_data(void) {
    if (billboard_data_count >= MAX_BILLBOARD_DATA) return NULL;
    return &billboard_data[billboard_data_count++];
}

// Billboard texture slots (cube uses 0-5)
#define TEX_BILLBOARD_MARKER  6
#define TEX_BILLBOARD_TREE    7

// Number of mesh objects (first N objects are selectable; billboards are not)
static int selectable_object_count = 0;

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

// ============================================================
// Particle effect definitions (data-driven, reusable)
// ============================================================

static const ParticleEmitterDef fire_effect = {
    .burst_count   = 30,
    .spawn_rate    = 0.0f,
    .lifetime_min  = 0.6f,
    .lifetime_max  = 1.5f,
    .velocity_min  = {-40.0f,  60.0f, -40.0f},
    .velocity_max  = { 40.0f, 180.0f,  40.0f},
    .gravity       = { 0.0f, -50.0f, 0.0f},
    .drag          = 0.3f,
    .color_start   = {255, 200, 60, 255},    // Bright yellow-orange
    .color_end     = {180, 40, 10, 0},        // Dark red, fade out
    .scale_start   = 8.0f,
    .scale_end     = 3.0f,
    .spawn_shape   = PARTICLE_SPAWN_SPHERE,
    .spawn_radius  = 15.0f,
    .blend_mode    = PARTICLE_BLEND_ADDITIVE,
};

static const ParticleEmitterDef magic_effect = {
    .burst_count   = 25,
    .spawn_rate    = 0.0f,
    .lifetime_min  = 0.8f,
    .lifetime_max  = 2.0f,
    .velocity_min  = {-60.0f,  30.0f, -60.0f},
    .velocity_max  = { 60.0f, 120.0f,  60.0f},
    .gravity       = { 0.0f,  20.0f, 0.0f},     // Anti-gravity, float up
    .drag          = 0.5f,
    .color_start   = {140, 180, 255, 255},   // Light blue
    .color_end     = {100, 40, 200, 0},       // Purple, fade out
    .scale_start   = 5.0f,
    .scale_end     = 12.0f,
    .spawn_shape   = PARTICLE_SPAWN_SPHERE,
    .spawn_radius  = 20.0f,
    .blend_mode    = PARTICLE_BLEND_ADDITIVE,
};

static const ParticleEmitterDef torch_flame = {
    .burst_count   = 0,
    .spawn_rate    = 20.0f,
    .lifetime_min  = 0.3f,
    .lifetime_max  = 0.8f,
    .velocity_min  = {-15.0f,  30.0f, -15.0f},
    .velocity_max  = { 15.0f,  80.0f,  15.0f},
    .gravity       = { 0.0f,  20.0f, 0.0f},
    .drag          = 0.4f,
    .color_start   = {255, 180, 60, 200},
    .color_end     = {200, 60, 10, 0},
    .scale_start   = 6.0f,
    .scale_end     = 2.0f,
    .spawn_shape   = PARTICLE_SPAWN_SPHERE,
    .spawn_radius  = 8.0f,
    .blend_mode    = PARTICLE_BLEND_ADDITIVE,
};

static int emitter_fire = -1;
static int emitter_magic = -1;
static int emitter_torch_l = -1;
static int emitter_torch_r = -1;

// --- Physics ball state ---
static PhysicsWorld physics_world;
static int ball_body_handle = -1;
static int ball_object_index = -1;
static bool ball_spawned = false;
static int platform_aabb_collider = -1;

// Ball spawn position (above platform)
#define BALL_SPAWN_X       0.0f
#define BALL_SPAWN_Y      50.0f
#define BALL_SPAWN_Z    -300.0f
#define BALL_RELAUNCH_VY  400.0f

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

// --- Menu tab/item indices (must match main.c order) ---
#define TAB_SETTINGS       0
#define TAB_SOUND          1

#define ITEM_BG_COLOR      0
#define ITEM_DEBUG_TEXT     1
#define ITEM_CAMERA_MODE   2
#define ITEM_CAMERA_COL    3
#define ITEM_FRAME_RATE    4
#define ITEM_RESET_SCENE   5

#define ITEM_SOUND_MASTER  0
#define ITEM_SFX_VOL       1
#define ITEM_BGM_VOL       2

// --- Lighting tab indices ---
#define TAB_LIGHTING       2

#define ITEM_SUN_DIR       0
#define ITEM_SUN_COLOR     1
#define ITEM_BRIGHTNESS    2
#define ITEM_AMBIENT       3
#define ITEM_SHADOWS       4
#define ITEM_SHADOW_DK     5
#define ITEM_PT_LIGHTS     6
#define ITEM_PT_COLOR      7
#define ITEM_PT_INTENSITY  8
#define ITEM_PT_RADIUS     9

// --- Environ tab indices ---
#define TAB_ENVIRON        3

#define ITEM_ATMO_PRESET   0
#define ITEM_FOG_TOGGLE    1
#define ITEM_FOG_NEAR      2
#define ITEM_FOG_FAR       3
#define ITEM_FOG_COLOR     4
#define ITEM_SKY_TOGGLE    5

// --- Controls tab indices (item order matches GameAction enum) ---
#define TAB_CONTROLS       4

// --- Lighting preset tables ---

static const float sun_dir_presets[][3] = {
    { 0.577f,  0.577f,  0.577f},   // Front (original default)
    {-0.707f,  0.707f,  0.000f},   // Side (from left, 45 deg)
    { 0.000f,  1.000f,  0.000f},   // Top (directly overhead)
    { 0.866f,  0.200f,  0.458f},   // Sunset (low angle)
    {-0.866f,  0.200f, -0.458f},   // Dawn (opposite sunset)
};

static const float sun_color_presets[][3] = {
    {0.85f, 0.80f, 0.70f},   // Warm (original)
    {0.70f, 0.80f, 0.90f},   // Cool (blue-ish)
    {0.85f, 0.85f, 0.85f},   // Neutral (white)
    {1.00f, 0.75f, 0.40f},   // Golden (sunset)
};

static const float brightness_presets[] = { 0.20f, 0.40f, 0.60f, 0.80f, 1.00f };
static const float ambient_presets[]    = { 0.10f, 0.20f, 0.30f, 0.40f, 0.50f };
static const float shadow_dark_presets[] = { 0.3f, 0.6f, 0.9f };

// --- Atmosphere preset tables ---

static const color_t fog_color_presets[] = {
    {0x80, 0x80, 0x90, 0xFF},  // Grey
    {0x60, 0x80, 0xD0, 0xFF},  // Blue
    {0xC0, 0xC0, 0xC0, 0xFF},  // White
    {0xD0, 0x80, 0x40, 0xFF},  // Warm
    {0x50, 0x30, 0x60, 0xFF},  // Purple
    {0x10, 0x10, 0x20, 0xFF},  // Dark
};
static const float fog_near_values[] = {50, 100, 150, 200, 300, 400};
static const float fog_far_values[]  = {400, 600, 800, 1000, 1200, 1400};

// --- Point light tuning tables ---

static const float ptlight_color_presets[][3] = {
    {1.0f, 0.7f, 0.3f},   // Warm (torch)
    {0.5f, 0.7f, 1.0f},   // Cool (blue)
    {1.0f, 0.3f, 0.2f},   // Red
    {0.3f, 1.0f, 0.3f},   // Green
    {0.3f, 0.3f, 1.0f},   // Blue
    {1.0f, 1.0f, 1.0f},   // White
};
static const float ptlight_intensity_values[] = {0.4f, 0.8f, 1.2f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 16.0f, 24.0f};
static const float ptlight_radius_values[]    = {100.0f, 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f, 1200.0f, 1500.0f, 2000.0f};

// Track last lighting menu values to detect changes
static int last_sun_dir = 0;
static int last_sun_color = 0;
static int last_brightness = 4;
static int last_ambient = 1;
static int last_shadows = 0;
static int last_shadow_dark = 1;
// Track atmosphere menu values
static int last_atmo_preset = 0;
static int last_fog_toggle = 0;
static int last_fog_near = 3;
static int last_fog_far = 4;
static int last_fog_color = 0;
static int last_sky_toggle = 0;

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
static int last_sound_master = 1;  // Off
static int last_sfx_vol = 8;      // 80%
static int last_bgm_vol = 6;      // 60%
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
// Helper: spawn a billboard object
// ============================================================

static int spawn_billboard(Scene *scene, int tex_slot, BillboardMode mode,
                           float width, float height, vec3_t pos,
                           uint8_t r, uint8_t g, uint8_t b) {
    BillboardData *data = alloc_billboard_data();
    if (!data) return -1;

    data->texture_slot = tex_slot;
    data->mode = mode;
    data->width = width;
    data->height = height;
    data->color[0] = r;
    data->color[1] = g;
    data->color[2] = b;

    SceneObject obj = {
        .position = pos,
        .rotation = {0, 0, 0},
        .scale = {1, 1, 1},
        .active = true,
        .visible = true,
        .collider_handle = -1,
        .data = data,
        .on_update = NULL,
        .on_draw = billboard_draw,
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

    // Platform AABB for physics raycasts (flat top surface at Y=-85)
    // Platform pos=(0,-92.5,-300), scale=(80,30,60), mesh half-sizes=(2,0.25,1)
    // World extents: X=±160, Y=±7.5, Z=±60 from center
    platform_aabb_collider = collision_add_aabb(&scene->collision,
        (vec3_t){-160, -100, -360}, (vec3_t){160, -85, -240},
        COLLISION_LAYER_ENV, COLLISION_LAYER_ENV, NULL);
    collision_set_static(&scene->collision, platform_aabb_collider, true);

    // --- Selectable count: only mesh objects above are selectable ---
    selectable_object_count = scene->object_count;

    // --- Billboard objects (decorative, not selectable) ---
    billboard_init();
    texture_load_slot(TEX_BILLBOARD_MARKER, "rom:/marker.sprite");
    texture_load_slot(TEX_BILLBOARD_TREE, "rom:/tree.sprite");

    // Marker billboard (spherical — floating above pyramid)
    spawn_billboard(scene, TEX_BILLBOARD_MARKER, BILLBOARD_SPHERICAL,
        50.0f, 50.0f, (vec3_t){0, 120, 350}, 255, 255, 255);

    // Tree billboards (cylindrical — only rotate around Y axis)
    spawn_billboard(scene, TEX_BILLBOARD_TREE, BILLBOARD_CYLINDRICAL,
        120.0f, 160.0f, (vec3_t){350, -20, -150}, 255, 255, 255);
    spawn_billboard(scene, TEX_BILLBOARD_TREE, BILLBOARD_CYLINDRICAL,
        100.0f, 130.0f, (vec3_t){-350, -30, 200}, 255, 255, 255);

    // Camera collision OFF by default
    last_camera_mode = 0;
    last_camera_col = 0;
    last_fps_option = 1;
    last_sound_master = 1;  // Off
    last_sfx_vol = 8;
    last_bgm_vol = 6;

    // Lighting defaults
    last_sun_dir = 0;
    last_sun_color = 0;
    last_brightness = 4;
    last_ambient = 1;
    last_shadows = 0;
    last_shadow_dark = 1;
    last_atmo_preset = 0;
    last_fog_toggle = 0;
    last_fog_near = 3;
    last_fog_far = 4;
    last_fog_color = 0;
    last_sky_toggle = 0;
    hud_fps = 0.0f;
    hud_fps_ticks = 0;
    interaction_mode = MODE_NORMAL;
    transform_mode = TRANSFORM_MOVE;
    selected_object = -1;

    // Initialize physics world (uses scene collision for raycasts)
    physics_world_init(&physics_world, &scene->collision);
    ball_body_handle = -1;
    ball_object_index = -1;
    ball_spawned = false;

    // Particle emitters (on top of pillars)
    particle_init();
    emitter_fire = particle_emitter_create(&fire_effect,
        (vec3_t){-250.0f, 100.0f, 0.0f}, 40);
    emitter_magic = particle_emitter_create(&magic_effect,
        (vec3_t){250.0f, 100.0f, 0.0f}, 40);
    emitter_torch_l = -1;
    emitter_torch_r = -1;

    // Set initial disabled states for menu items
    // Point light sub-options disabled by default (Pt Lights = Off)
    menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_COLOR, true);
    menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_INTENSITY, true);
    menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_RADIUS, true);

    // Start background music
    snd_play_bgm(BGM_DEMO);
}

// ============================================================
// Object manipulation
// ============================================================

static void handle_object_manipulation(Scene *scene, const InputState *input) {
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
    // Poll input through action mapping layer
    action_update();
    InputState input_state;
    input_update(&input_state);

    // Menu input (START is fixed — always toggles menu)
    joypad_buttons_t raw_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (raw_pressed.start) {
        if (start_menu.is_open) {
            menu_close(&start_menu, true);
            snd_play_sfx(SFX_MENU_CLOSE);
        } else {
            menu_open(&start_menu);
            snd_play_sfx(SFX_MENU_OPEN);
        }
    }

    // --- Check for scene reset (only after menu closes with Apply) ---
    if (!start_menu.is_open) {
        int reset_val = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_RESET_SCENE);
        if (reset_val == 1) {
            start_menu.tabs[TAB_SETTINGS].items[ITEM_RESET_SCENE].selected = 0;
            scene->reset_requested = true;
            return;
        }
    }

    // --- Interaction mode handling ---

    if (!start_menu.is_open) {
        // Cancel button: spawn/re-launch physics ball (normal mode)
        if (interaction_mode == MODE_NORMAL && action_pressed(ACTION_CANCEL)) {
            if (!ball_spawned) {
                // First press: spawn ball above platform
                vec3_t spawn_pos = {BALL_SPAWN_X, BALL_SPAWN_Y, BALL_SPAWN_Z};
                ball_body_handle = physics_body_add(&physics_world,
                    &PHYSICS_DEF_BALL, spawn_pos);
                if (ball_body_handle >= 0) {
                    ball_object_index = spawn_object(scene, "Ball",
                        mesh_defs_get_sphere(), spawn_pos,
                        (vec3_t){20, 20, 20}, false, 0, 0);
                    ball_spawned = true;
                }
            } else {
                // Subsequent presses: re-launch with upward impulse
                PhysicsBody *body = physics_body_get(&physics_world, ball_body_handle);
                if (body) {
                    body->position = (vec3_t){BALL_SPAWN_X, BALL_SPAWN_Y, BALL_SPAWN_Z};
                    body->velocity = VEC3_ZERO;
                    physics_body_apply_impulse(body,
                        (vec3_t){0, BALL_RELAUNCH_VY, 0});
                }
            }
            particle_emitter_burst(emitter_fire);
            particle_emitter_burst(emitter_magic);
            snd_play_sfx(SFX_MODE_CHANGE);
        }

        // Select mode: toggle object select
        if (action_pressed(ACTION_SELECT_MODE)) {
            if (interaction_mode == MODE_NORMAL) {
                interaction_mode = MODE_OBJECT_SELECT;
                if (selected_object < 0 && selectable_object_count > 0)
                    selected_object = 0;
                snd_play_sfx(SFX_OBJ_SELECT);
            } else {
                interaction_mode = MODE_NORMAL;
                selected_object = -1;
                snd_play_sfx(SFX_OBJ_DESELECT);
            }
        }

        if (interaction_mode == MODE_OBJECT_SELECT) {
            // Cycle prev/next: cycle selected object
            if (action_pressed(ACTION_CYCLE_PREV) && selectable_object_count > 0) {
                selected_object--;
                if (selected_object < 0) selected_object = selectable_object_count - 1;
                snd_play_sfx(SFX_MENU_NAV);
            }
            if (action_pressed(ACTION_CYCLE_NEXT) && selectable_object_count > 0) {
                selected_object = (selected_object + 1) % selectable_object_count;
                snd_play_sfx(SFX_MENU_NAV);
            }
            // Confirm: enter transform mode
            if (action_pressed(ACTION_CONFIRM)) {
                interaction_mode = MODE_OBJECT_TRANSFORM;
                transform_mode = TRANSFORM_MOVE;
                snd_play_sfx(SFX_MODE_CHANGE);
            }
            // Cancel: exit to normal
            if (action_pressed(ACTION_CANCEL)) {
                interaction_mode = MODE_NORMAL;
                selected_object = -1;
            }
        } else if (interaction_mode == MODE_OBJECT_TRANSFORM) {
            // Confirm: cycle transform mode
            if (action_pressed(ACTION_CONFIRM)) {
                transform_mode = (transform_mode + 1) % 3;
                snd_play_sfx(SFX_MODE_CHANGE);
            }
            // Cancel: back to select
            if (action_pressed(ACTION_CANCEL)) {
                interaction_mode = MODE_OBJECT_SELECT;
                snd_play_sfx(SFX_OBJ_DESELECT);
            }
            // Manipulate object with analog/C-buttons
            handle_object_manipulation(scene, &input_state);
        }
    }

    // --- Camera controls (only when not transforming objects) ---

    if (start_menu.is_open) {
        int old_tab = start_menu.active_tab;
        int old_cursor = start_menu.tabs[start_menu.active_tab].cursor;
        bool was_open = start_menu.is_open;
        menu_update(&start_menu);
        if (start_menu.is_open) {
            int new_cursor = start_menu.tabs[start_menu.active_tab].cursor;
            if (new_cursor != old_cursor || start_menu.active_tab != old_tab) {
                snd_play_sfx(SFX_MENU_NAV);
            }
        }
        if (was_open && !start_menu.is_open) {
            snd_play_sfx(SFX_MENU_SELECT);
        }
    } else if (interaction_mode != MODE_OBJECT_TRANSFORM) {
        // Camera mode cycling
        if (action_pressed(ACTION_CAM_MODE_PREV)) {
            int mode = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
            mode = (mode + 2) % 3;
            start_menu.tabs[TAB_SETTINGS].items[ITEM_CAMERA_MODE].selected = mode;
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }
        if (action_pressed(ACTION_CAM_MODE_NEXT)) {
            int mode = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
            mode = (mode + 1) % 3;
            start_menu.tabs[TAB_SETTINGS].items[ITEM_CAMERA_MODE].selected = mode;
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
    int cam_mode = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
    if (cam_mode != last_camera_mode) {
        apply_camera_mode(scene, cam_mode);
        last_camera_mode = cam_mode;
    }

    // Check for camera collision toggle from menu
    int cam_col = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_COL);
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
    int fps_opt = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_FRAME_RATE);
    if (fps_opt != last_fps_option) {
        switch (fps_opt) {
        case 0: engine_target_fps = 30; break;
        case 1: engine_target_fps = 0;  break;  // 60fps: no limiter needed (VSync caps it)
        }
        last_fps_option = fps_opt;
    }

    // Check for sound settings from Sound tab
    int snd_master = menu_get_value(&start_menu, TAB_SOUND, ITEM_SOUND_MASTER);
    int sfx_vol_idx = menu_get_value(&start_menu, TAB_SOUND, ITEM_SFX_VOL);
    int bgm_vol_idx = menu_get_value(&start_menu, TAB_SOUND, ITEM_BGM_VOL);

    if (snd_master != last_sound_master || sfx_vol_idx != last_sfx_vol ||
        bgm_vol_idx != last_bgm_vol) {
        if (snd_master == 0) {
            // Master On — apply volume sliders (index 0-10 → 0-128)
            snd_set_sfx_volume(sfx_vol_idx * 13);
            snd_set_bgm_volume(bgm_vol_idx * 13);
        } else {
            // Master Off — mute everything
            snd_set_sfx_volume(0);
            snd_set_bgm_volume(0);
        }
        last_sound_master = snd_master;
        last_sfx_vol = sfx_vol_idx;
        last_bgm_vol = bgm_vol_idx;
    }

    // --- Apply lighting settings from Lighting tab ---

    int sun_dir_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_SUN_DIR);
    int sun_color_idx  = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_SUN_COLOR);
    int brightness_idx = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_BRIGHTNESS);
    int ambient_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_AMBIENT);
    int shadow_idx     = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_SHADOWS);
    int shadow_dk_idx  = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_SHADOW_DK);
    if (sun_dir_idx != last_sun_dir || sun_color_idx != last_sun_color ||
        brightness_idx != last_brightness || ambient_idx != last_ambient ||
        shadow_idx != last_shadows || shadow_dk_idx != last_shadow_dark) {

        LightConfig *lc = &scene->lighting;

        // Sun direction
        lc->direction[0] = sun_dir_presets[sun_dir_idx][0];
        lc->direction[1] = sun_dir_presets[sun_dir_idx][1];
        lc->direction[2] = sun_dir_presets[sun_dir_idx][2];

        // Sun color + brightness
        lc->sun_color[0] = sun_color_presets[sun_color_idx][0];
        lc->sun_color[1] = sun_color_presets[sun_color_idx][1];
        lc->sun_color[2] = sun_color_presets[sun_color_idx][2];
        lc->sun_intensity = brightness_presets[brightness_idx];

        // Ambient (slightly blue tint)
        float amb = ambient_presets[ambient_idx];
        lc->ambient[0] = amb;
        lc->ambient[1] = amb;
        lc->ambient[2] = amb * 1.1f;
        if (lc->ambient[2] > 1.0f) lc->ambient[2] = 1.0f;

        // Shadows
        lc->shadow.mode = (ShadowMode)shadow_idx;
        lc->shadow.darkness = shadow_dark_presets[shadow_dk_idx];
        lc->shadow.floor_y = FLOOR_Y;
        lc->shadow.blob_radius = 80.0f;

        last_sun_dir = sun_dir_idx;
        last_sun_color = sun_color_idx;
        last_brightness = brightness_idx;
        last_ambient = ambient_idx;
        last_shadows = shadow_idx;
        last_shadow_dark = shadow_dk_idx;
    }

    scene->camera.dirty = true;

    // --- Apply atmosphere settings from Environ tab ---

    int atmo_preset  = menu_get_value(&start_menu, TAB_ENVIRON, ITEM_ATMO_PRESET);
    int fog_toggle   = menu_get_value(&start_menu, TAB_ENVIRON, ITEM_FOG_TOGGLE);
    int fog_near_idx = menu_get_value(&start_menu, TAB_ENVIRON, ITEM_FOG_NEAR);
    int fog_far_idx  = menu_get_value(&start_menu, TAB_ENVIRON, ITEM_FOG_FAR);
    int fog_color_idx= menu_get_value(&start_menu, TAB_ENVIRON, ITEM_FOG_COLOR);
    int sky_toggle   = menu_get_value(&start_menu, TAB_ENVIRON, ITEM_SKY_TOGGLE);

    if (atmo_preset != last_atmo_preset && atmo_preset > 0) {
        // Named preset selected: apply all atmosphere + lighting settings
        AtmospherePresetID id = (AtmospherePresetID)(atmo_preset - 1);
        atmosphere_apply_preset(id);
        const AtmospherePreset *p = atmosphere_get_preset(id);
        scene->bg_color = p->bg_color;

        // Apply preset lighting hints to scene
        LightConfig *lc = &scene->lighting;
        lc->sun_intensity = p->lighting.sun_intensity;
        lc->ambient[0] = p->lighting.ambient[0];
        lc->ambient[1] = p->lighting.ambient[1];
        lc->ambient[2] = p->lighting.ambient[2];
        lc->sun_color[0] = p->lighting.sun_color[0];
        lc->sun_color[1] = p->lighting.sun_color[1];
        lc->sun_color[2] = p->lighting.sun_color[2];

        // Sync menu toggles to reflect preset state
        start_menu.tabs[TAB_ENVIRON].items[ITEM_FOG_TOGGLE].selected = 1;
        start_menu.tabs[TAB_ENVIRON].items[ITEM_SKY_TOGGLE].selected = 1;
        fog_toggle = 1;
        sky_toggle = 1;
    } else if (atmo_preset == 0 &&
               (atmo_preset != last_atmo_preset || fog_toggle != last_fog_toggle ||
                fog_near_idx != last_fog_near || fog_far_idx != last_fog_far ||
                fog_color_idx != last_fog_color || sky_toggle != last_sky_toggle)) {
        // Custom mode: apply individual settings
        atmosphere_set_fog_enabled(fog_toggle == 1);
        atmosphere_set_fog_near(fog_near_values[fog_near_idx]);
        atmosphere_set_fog_far(fog_far_values[fog_far_idx]);
        atmosphere_set_fog_color(fog_color_presets[fog_color_idx]);
        atmosphere_set_sky_enabled(sky_toggle == 1);
    }

    // Update disabled state for Environ sub-options
    bool is_custom = (atmo_preset == 0);
    menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_TOGGLE, !is_custom);
    menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_NEAR, !is_custom);
    menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_FAR, !is_custom);
    menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_COLOR, !is_custom);
    menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_SKY_TOGGLE, !is_custom);

    last_atmo_preset = atmo_preset;
    last_fog_toggle = fog_toggle;
    last_fog_near = fog_near_idx;
    last_fog_far = fog_far_idx;
    last_fog_color = fog_color_idx;
    last_sky_toggle = sky_toggle;

    // --- Apply point lights unconditionally (survives atmosphere preset changes) ---
    {
        int pt_lights_idx = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_LIGHTS);
        int pt_color_idx  = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_COLOR);
        int pt_int_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_INTENSITY);
        int pt_rad_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_RADIUS);

        LightConfig *lc = &scene->lighting;

        if (pt_lights_idx == 1) {
            float pt_r = ptlight_color_presets[pt_color_idx][0];
            float pt_g = ptlight_color_presets[pt_color_idx][1];
            float pt_b = ptlight_color_presets[pt_color_idx][2];
            float pt_intensity = ptlight_intensity_values[pt_int_idx];
            float pt_radius = ptlight_radius_values[pt_rad_idx];

            lc->point_light_count = 2;
            lc->point_lights[0] = (PointLight){
                .position  = {-250.0f, 50.0f, 30.0f},
                .color     = {pt_r, pt_g, pt_b},
                .intensity = pt_intensity,
                .radius    = pt_radius,
                .active    = true,
            };
            lc->point_lights[1] = (PointLight){
                .position  = {250.0f, 50.0f, 30.0f},
                .color     = {pt_r, pt_g, pt_b},
                .intensity = pt_intensity,
                .radius    = pt_radius,
                .active    = true,
            };
        } else {
            lc->point_light_count = 0;
        }

        // Torch emitter lifecycle — state-based (survives preset changes)
        bool want_torches = (pt_lights_idx == 1);
        bool have_torches = (emitter_torch_l >= 0);

        if (want_torches && !have_torches) {
            emitter_torch_l = particle_emitter_create(&torch_flame,
                (vec3_t){-250.0f, 100.0f, 0.0f}, 16);
            emitter_torch_r = particle_emitter_create(&torch_flame,
                (vec3_t){250.0f, 100.0f, 0.0f}, 16);
            if (emitter_torch_l >= 0) particle_emitter_set_active(emitter_torch_l, true);
            if (emitter_torch_r >= 0) particle_emitter_set_active(emitter_torch_r, true);
        } else if (!want_torches && have_torches) {
            if (emitter_torch_l >= 0) particle_emitter_destroy(emitter_torch_l);
            if (emitter_torch_r >= 0) particle_emitter_destroy(emitter_torch_r);
            emitter_torch_l = -1;
            emitter_torch_r = -1;
        }

        // Disabled state for point light sub-options
        bool pt_on = (pt_lights_idx == 1);
        menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_COLOR, !pt_on);
        menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_INTENSITY, !pt_on);
        menu_item_set_disabled(&start_menu, TAB_LIGHTING, ITEM_PT_RADIUS, !pt_on);
    }

    // --- Apply control rebindings from Controls tab ---
    // Menu item indices match GameAction enum; option indices match PhysicalButton enum
    for (int i = 0; i < ACTION_COUNT; i++) {
        int btn_idx = menu_get_value(&start_menu, TAB_CONTROLS, i);
        action_set_binding((GameAction)i, (PhysicalButton)btn_idx);
    }

    // Background color: atmosphere preset overrides Settings tab
    if (atmo_preset > 0) {
        // Preset already set bg_color above
    } else if (atmosphere_get_fog_enabled()) {
        // Custom fog: match bg to fog color for seamless blending
        scene->bg_color = fog_color_presets[fog_color_idx];
    } else {
        scene->bg_color = bg_colors[menu_get_value(&start_menu, TAB_SETTINGS, ITEM_BG_COLOR)];
    }

    // Update physics (semi-fixed timestep)
    physics_world_update(&physics_world, dt);

    // Sync ball position from physics body to scene object
    if (ball_spawned && ball_body_handle >= 0 && ball_object_index >= 0) {
        PhysicsBody *body = physics_body_get(&physics_world, ball_body_handle);
        SceneObject *ball_obj = scene_get_object(scene, ball_object_index);
        if (body && ball_obj) {
            ball_obj->position = body->position;
        }
    }

    // Update particles
    particle_update(dt);
}

// ============================================================
// Scene draw — floor only (objects drawn by per-object callbacks)
// ============================================================

static void demo_draw(Scene *scene) {
    texture_stats_reset();

    // Draw sky gradient bands (behind all geometry)
    sky_draw();

    // Draw the checkered floor
    floor_draw(&scene->camera, &scene->lighting);

    // Draw shadows on floor (after floor, before objects)
    if (scene->lighting.shadow.mode != SHADOW_OFF) {
        shadow_begin(&scene->camera, &scene->lighting);

        for (int i = 0; i < selectable_object_count; i++) {
            SceneObject *obj = scene_get_object(scene, i);
            if (!obj || !obj->visible) continue;

            ObjectData *data = (ObjectData *)obj->data;
            if (!data || !data->mesh) continue;

            mat4_t model;
            mat4_from_srt(&model, &obj->scale,
                          obj->rotation.x, obj->rotation.y, obj->rotation.z,
                          &obj->position);

            ShadowCaster caster = {
                .mesh = data->mesh,
                .model = &model,
                .position = obj->position,
                .bound_radius = data->mesh->bound_radius *
                    (obj->scale.x > obj->scale.z ? obj->scale.x : obj->scale.z),
            };

            if (scene->lighting.shadow.mode == SHADOW_BLOB) {
                shadow_draw_blob(&scene->camera, &scene->lighting, &caster);
            } else {
                shadow_draw_projected(&scene->camera, &scene->lighting, &caster);
            }
        }

        shadow_end();
    }
}

// ============================================================
// Post-draw — HUD and overlays (after all 3D geometry)
// ============================================================

static void demo_post_draw(Scene *scene) {
    // Draw particles (after opaque objects, before HUD)
    particle_draw(&scene->camera);

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
    bool show_debug = (menu_get_value(&start_menu, TAB_SETTINGS, ITEM_DEBUG_TEXT) == 0);
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
        int cam_mode_idx = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
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
    particle_cleanup();
    emitter_fire = -1;
    emitter_magic = -1;
    emitter_torch_l = -1;
    emitter_torch_r = -1;
    billboard_cleanup();
    texture_free_slot(TEX_BILLBOARD_MARKER);
    texture_free_slot(TEX_BILLBOARD_TREE);
    cube_cleanup();
    mesh_defs_cleanup();
    ground_collider = -1;
    platform_aabb_collider = -1;
    obj_collider_count = 0;
    object_data_count = 0;
    billboard_data_count = 0;
    selectable_object_count = 0;
    ball_body_handle = -1;
    ball_object_index = -1;
    ball_spawned = false;
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
    .bg_color = {0x60, 0x80, 0xD0, 0xFF},
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
