#include "demo_scene.h"
#include <string.h>
#include <stdio.h>
#include "../render/cube.h"
#include "../render/mesh.h"
#include "../render/mesh_defs.h"
#include "../render/floor.h"
#include "../render/texture.h"
#include "../input/input.h"
#include "../input/action.h"
#include "../ui/text.h"
#include "../ui/menu.h"
#include "../ui/menu_view.h"
#include "../ui/ui_hud.h"
#include "../ui/ui_draw.h"
#include "../ui/textbox.h"
#include "../dialog/dialog.h"
#include "../debug/debug_menu.h"
#include "../debug/engine_debug.h"
#include "../engine/engine.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
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

// --- Physics ball: a scene object with a body and a collider (launch_ball) ---
static int ball_object = -1;    // object index, -1 until the first launch

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
static MenuView start_menu_view;        // cached text; kept across scene resets
static bool     start_menu_view_ready;

// ============================================================
// HUD: two panels of cached text (ui_hud.h) in the current UI style
// ============================================================

#define HUD_REFRESH       10      // frames between HUD updates (~6 Hz)
#define HUD_RENDER_BUDGET  2      // text lines re-rendered per frame at most
#define HUD_GAUGE_W      120      // CPU budget gauge under the FPS line

static HudPanel hud_top, hud_bottom;
static bool     hud_ready;
static int      hl_title, hl_obj, hl_geom, hl_fps, hl_sel, hl_cam, hl_pos;
static float    hud_cpu_frac;
static const UiStyle *ui_style_cur = &ui_style_debug;

static void hud_setup(void) {
    if (hud_ready) return;
    hud_panel_init(&hud_top, 20, 10, 280, 14, true, HUD_REFRESH, HUD_RENDER_BUDGET);
    hl_title = hud_panel_line(&hud_top, 20, 20, 280, FONT_UI_VAR, ALIGN_CENTER);
    // Bottom band: three readouts on the left, three right-aligned
    hud_panel_init(&hud_bottom, 0, 186, 320, 38, true, HUD_REFRESH, HUD_RENDER_BUDGET);
    hl_obj  = hud_panel_line(&hud_bottom, 4, 196, 172, FONT_UI_MONO, ALIGN_LEFT);
    hl_geom = hud_panel_line(&hud_bottom, 4, 208, 172, FONT_UI_MONO, ALIGN_LEFT);
    hl_fps  = hud_panel_line(&hud_bottom, 4, 220, 172, FONT_UI_MONO, ALIGN_LEFT);
    hl_sel  = hud_panel_line(&hud_bottom, 176, 196, 140, FONT_UI_MONO, ALIGN_RIGHT);
    hl_cam  = hud_panel_line(&hud_bottom, 176, 208, 140, FONT_UI_MONO, ALIGN_RIGHT);
    hl_pos  = hud_panel_line(&hud_bottom, 176, 220, 140, FONT_UI_MONO, ALIGN_RIGHT);
    hud_ready = true;
}

// --- Menu tab/item indices (must match main.c order) ---
#define TAB_SETTINGS       0
#define TAB_SOUND          1

#define ITEM_BG_COLOR      0
#define ITEM_DEBUG_TEXT     1
#define ITEM_CAMERA_MODE   2
#define ITEM_CAMERA_COL    3
#define ITEM_FRAME_RATE    4
#define ITEM_RESET_SCENE   5
#define ITEM_UI_STYLE      6

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
// Applied only when they change (roadmap D23); -1 forces the next update to apply
static int last_pt_lights = -1, last_pt_color = -1, last_pt_int = -1, last_pt_rad = -1;
static int environ_disabled_for = -1;          // atmosphere preset the Environ disabled states match
static int last_binding[ACTION_COUNT];

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
#define BOUNCE_SOUND_MIN_SPEED   60.0f    // slower impacts are silent (rolling, resting)
#define BOUNCE_SOUND_FULL_SPEED  400.0f   // impact speed of a full-volume bounce
#define FIXED_Y_SPEED     5.0f

static int last_fps_option = 1;
static int last_ui_style = -1;       // -1: apply the UI Style item on the next check
static int last_sound_master = -1;  // -1: apply the Sound tab on the next check
static int last_sfx_vol = -1;
static int last_bgm_vol = -1;
static float ball_prev_vy = 0.0f;   // bounce detection for the collision sound

// Sound tab → sound module volumes, when a value changes (the caches start at
// -1, so demo_init applies the tab before any music starts). Master Off fades
// everything out, and silent music stops decoding (docs/AUDIO.md).
static void apply_sound_settings(void) {
    int master  = menu_get_value(&start_menu, TAB_SOUND, ITEM_SOUND_MASTER);  // 0 = On
    int sfx_idx = menu_get_value(&start_menu, TAB_SOUND, ITEM_SFX_VOL);        // 0..10
    int bgm_idx = menu_get_value(&start_menu, TAB_SOUND, ITEM_BGM_VOL);        // 0..10
    if (master == last_sound_master && sfx_idx == last_sfx_vol && bgm_idx == last_bgm_vol) return;
    snd_set_volume(SND_VOL_MASTER, master == 0 ? 1.0f : 0.0f, 0.25f);
    snd_set_volume(SND_VOL_SFX, sfx_idx / 10.0f, 0.0f);
    snd_set_volume(SND_VOL_MUSIC, bgm_idx / 10.0f, 0.0f);
    last_sound_master = master;
    last_sfx_vol = sfx_idx;
    last_bgm_vol = bgm_idx;
}
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
// Helper: spawn a scene object with mesh. Mesh objects are selectable and
// cast shadows (SceneObject.flags); billboards do neither.
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
        .flags = SCENE_OBJ_SELECTABLE | SCENE_OBJ_CASTS_SHADOW,
        .data = data,
        .on_update = object_update,
        .on_draw = object_draw,
    };

    return scene_add_object(scene, &obj);
}

// Sphere collider on the default layer, centred on the object: the scene
// moves it with the object and removes it with the object
static void attach_sphere_collider(Scene *scene, int idx, float radius) {
    const SceneObject *obj = scene_get_object(scene, idx);
    if (!obj) return;
    scene_object_set_collider(scene, idx, collision_add_sphere(&scene->collision,
        obj->position, radius, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL));
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

// ============================================================
// Ball (B button, and the dialog's "spawn_ball" event)
// ============================================================

static void launch_ball(Scene *scene) {
    vec3_t spawn_pos = {BALL_SPAWN_X, BALL_SPAWN_Y, BALL_SPAWN_Z};
    SceneObject *ball = scene_get_object(scene, ball_object);
    if (!ball) {
        // First time: a unit sphere scaled to the body's radius, above the
        // platform. The scene moves it with its body, and its collider with
        // it; the collider is on the default layer only, so the body's ground
        // raycasts (ENV) never hit the ball itself.
        float r = PHYSICS_DEF_BALL.radius;
        int body = physics_body_add(&scene->physics, &PHYSICS_DEF_BALL, spawn_pos);
        int idx = body >= 0 ? spawn_object(scene, "Ball", mesh_defs_get_sphere(),
                                           spawn_pos, (vec3_t){r, r, r}, false, 0, 0) : -1;
        if (idx < 0) {
            physics_body_remove(&scene->physics, body);
            return;
        }
        scene_object_set_body(scene, idx, body);
        attach_sphere_collider(scene, idx, r);
        ball_object = idx;
    } else {
        // Afterwards: re-launch with an upward impulse
        PhysicsBody *body = physics_body_get(&scene->physics, ball->body_handle);
        if (body) {
            body->position = spawn_pos;
            body->velocity = VEC3_ZERO;
            physics_body_apply_impulse(body,
                (vec3_t){0, BALL_RELAUNCH_VY, 0});
        }
    }
}

static void burst_particles(void) {
    particle_emitter_burst(emitter_fire);
    particle_emitter_burst(emitter_magic);
}

// ============================================================
// Dialog (docs/DIALOG.md): assets/dialog/demo.json, started from
// Debug > Dialog. The hooks below are the game's side of the format:
// events, conditions and variables the conversation refers to by name.
// ============================================================

static DialogBank  *dialog_bank;
static DialogRunner dialog_runner;
static TextBox      dialog_box;

static void dialog_on_event(const char *event, void *ctx) {
    Scene *scene = ctx;
    if (strcmp(event, "spawn_ball") == 0) {
        launch_ball(scene);
        snd_play(SFX_MODE_CHANGE);
    } else if (strcmp(event, "burst") == 0) {
        burst_particles();
        snd_play(SFX_MODE_CHANGE);
    } else if (strncmp(event, "sound:", 6) == 0) {
        static const struct { const char *name; SoundId id; } sounds[] = {
            {"select", SFX_MENU_SELECT}, {"open", SFX_MENU_OPEN}, {"close", SFX_MENU_CLOSE},
        };
        for (unsigned i = 0; i < sizeof(sounds) / sizeof(sounds[0]); i++)
            if (strcmp(event + 6, sounds[i].name) == 0) snd_play(sounds[i].id);
    } else {
        ENGINE_LOG("[dialog] unhandled event '%s'\n", event);
    }
}

// Conditions: a name, or !name for its opposite
static bool dialog_on_check(const char *cond, void *ctx) {
    (void)ctx;
    bool negate = cond[0] == '!';
    if (negate) cond++;
    bool value = false;
    if (strcmp(cond, "ball_spawned") == 0) value = ball_object >= 0;
    else ENGINE_LOG("[dialog] unknown condition '%s'\n", cond);
    return value != negate;
}

static bool dialog_on_variable(const char *name, char *out, int len, void *ctx) {
    (void)ctx;
    if (strcmp(name, "player") == 0) { snprintf(out, len, "Traveller"); return true; }
    if (strcmp(name, "fps") == 0)    { snprintf(out, len, "%.0f", display_get_fps()); return true; }
    if (strcmp(name, "style") == 0)  { snprintf(out, len, "%s", ui_style_cur->name); return true; }
    return false;
}

static void dialog_on_blip(void *ctx) {
    (void)ctx;
    snd_play(SFX_MENU_NAV);
}

static void dialog_open(Scene *scene, const char *conversation) {
    DialogHooks hooks = { dialog_on_event, dialog_on_check, dialog_on_variable, scene };
    if (!dialog_bank || !dialog_start(&dialog_runner, dialog_bank, conversation, &hooks)) {
        ENGINE_LOG("[dialog] cannot start '%s'\n", conversation);
        return;
    }
    textbox_open(&dialog_box, &dialog_runner);
    snd_play(SFX_MENU_OPEN);
}

// Buttons -> UiInput: A/B through the action map, D-pad or stick for choices
static UiInput dialog_input(void) {
    static int stick_prev;
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
    int stick = in.stick_y > 40 ? 1 : in.stick_y < -40 ? -1 : 0;
    UiInput ui = {
        .confirm = action_pressed(ACTION_CONFIRM),
        .cancel  = action_pressed(ACTION_CANCEL),
        .up      = pressed.d_up   || (stick == 1 && stick_prev != 1),
        .down    = pressed.d_down || (stick == -1 && stick_prev != -1),
    };
    stick_prev = stick;
    return ui;
}

static void demo_init(Scene *scene) {
    // Store scene pointer for object_draw selection check
    current_scene = scene;

    // Reset object data pool
    object_data_count = 0;

    // Textures (cube faces + billboards) are declared in Scene.texture_paths
    // below and loaded/freed by scene_init/scene_cleanup (fixes the reset leak D1).
    billboard_data_count = 0;

    // Initialize cube mesh
    cube_init();

    // Initialize shape library
    mesh_defs_init();

    // Camera: orbital mode, default config
    camera_init(&scene->camera, &CAMERA_DEFAULT);
    scene->camera.collision_radius = 10.0f;
    scene->camera.min_y = FLOOR_Y + 10.0f;

    // --- Spawn objects ---
    // Each gets a collider that the scene moves with it (SceneObject.collider_handle)

    // 0: Cube (textured, auto-rotating)
    int idx = spawn_object(scene, "Cube", cube_get_mesh(),
        (vec3_t){0, 0, 0}, (vec3_t){80, 80, 80},
        true, 0.15f, 0.3f);
    attach_sphere_collider(scene, idx, 138.56f);

    // 1: Pillar Left
    idx = spawn_object(scene, "Pillar L", mesh_defs_get_pillar(),
        (vec3_t){-250, 0, 0}, (vec3_t){40, 100, 40},
        false, 0, 0);
    attach_sphere_collider(scene, idx, 110.0f);

    // 2: Pillar Right
    idx = spawn_object(scene, "Pillar R", mesh_defs_get_pillar(),
        (vec3_t){250, 0, 0}, (vec3_t){40, 100, 40},
        false, 0, 0);
    attach_sphere_collider(scene, idx, 110.0f);

    // 3: Platform (behind cube) — unit box Y [-0.25,0.25], scale Y=30 → world Y extent ±7.5.
    //    Its collider is its box, on the ENV layer the ball's ground raycasts
    //    use (top surface at Y=-85), so the ball lands on it wherever it is
    //    moved. Mesh half-sizes (2,0.25,1) × scale: X=±160, Y=±7.5, Z=±60.
    idx = spawn_object(scene, "Platform", mesh_defs_get_platform(),
        (vec3_t){0, -92.5f, -300}, (vec3_t){80, 30, 60},
        false, 0, 0);
    if (idx >= 0) {
        int box = collision_add_aabb(&scene->collision,
            (vec3_t){-160, -100, -360}, (vec3_t){160, -85, -240},
            COLLISION_LAYER_ENV, COLLISION_LAYER_ENV, NULL);
        collision_set_static(&scene->collision, box, true);
        scene_object_set_collider(scene, idx, box);
    }

    // 4: Pyramid (in front of cube)
    idx = spawn_object(scene, "Pyramid", mesh_defs_get_pyramid(),
        (vec3_t){0, 0, 350}, (vec3_t){60, 80, 60},
        true, 0, 0.1f);
    attach_sphere_collider(scene, idx, 90.0f);

    // 5: Sphere (static) — curved surface for checking lighting / back-face
    //    culling fixes (ROADMAP_v2 S2); the physics ball only exists after B.
    idx = spawn_object(scene, "Sphere", mesh_defs_get_sphere(),
        (vec3_t){280, -50, 280}, (vec3_t){50, 50, 50},
        false, 0, 0);
    attach_sphere_collider(scene, idx, 55.0f);

    // Ground collider (static, covers floor area; not an object)
    int ground = collision_add_aabb(&scene->collision,
        (vec3_t){-1500, -180, -1500}, (vec3_t){1500, -100, 1500},
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
    collision_set_static(&scene->collision, ground, true);

    // --- Billboard objects (decorative, not selectable) ---
    billboard_init();

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
    last_ui_style = -1;
    last_sound_master = -1;
    last_sfx_vol = -1;
    last_bgm_vol = -1;
    ball_prev_vy = 0.0f;

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
    last_pt_lights = last_pt_color = last_pt_int = last_pt_rad = -1;
    environ_disabled_for = -1;
    for (int i = 0; i < ACTION_COUNT; i++) last_binding[i] = -1;
    hud_fps = 0.0f;
    hud_fps_ticks = 0;
    interaction_mode = MODE_NORMAL;
    transform_mode = TRANSFORM_MOVE;
    selected_object = -1;

    // The ball is spawned by the first B press (scene_init emptied the
    // scene's physics world)
    ball_object = -1;

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

    // Background music: the Sound tab first, so a muted start never decodes
    apply_sound_settings();
    snd_music_play(BGM_DEMO, 1.0f);

    // Dialog bank; the text box lays itself out on its first draw
    dialog_bank = dialog_bank_load("rom:/dialog/demo.dlg");
    textbox_init(&dialog_box);
    dialog_box.on_blip = dialog_on_blip;
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

    // The scene moves the object's collider; a body moves with the hand
    // (held still: demo_update makes it kinematic while it is transformed)
    PhysicsBody *body = physics_body_get(&scene->physics, obj->body_handle);
    if (body) {
        body->position = obj->position;
        body->velocity = VEC3_ZERO;
    }
}

// ============================================================
// Scene update
// ============================================================

static void demo_update(Scene *scene, float dt) {
    // Poll input through action mapping layer
    PROF_BEGIN(PROF_INPUT);
    action_update();
    InputState input_state;
    input_update(&input_state);
    PROF_END(PROF_INPUT);

    // Menu input (START is fixed — always toggles menu)
    joypad_buttons_t raw_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    bool in_dialog = textbox_active(&dialog_box);
    if (raw_pressed.start && !in_dialog) {
        if (start_menu.is_open) {
            menu_close(&start_menu, true);
            snd_play(SFX_MENU_CLOSE);
        } else {
            menu_open(&start_menu);
            snd_play(SFX_MENU_OPEN);
        }
    }

    // --- Check for scene reset (only after menu closes with Apply) ---
    if (!start_menu.is_open) {
        int reset_val = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_RESET_SCENE);
        if (reset_val == 1) {
            menu_set_value(&start_menu, TAB_SETTINGS, ITEM_RESET_SCENE, 0);
            scene->reset_requested = true;
            return;
        }
    }

    // --- Dialog: modal while open (game and camera input wait) ---

    if (!start_menu.is_open && !in_dialog && debug_consume_dialog_request()) {
        dialog_open(scene, "intro");
        in_dialog = textbox_active(&dialog_box);
    } else if (in_dialog) {
        PROF_BEGIN(PROF_DIALOG);
        UiInput ui = dialog_input();
        textbox_update(&dialog_box, &ui, dt);
        PROF_END(PROF_DIALOG);
    }
    debug_menu_set_shortcuts(!in_dialog);

    // --- Interaction mode handling ---

    if (!start_menu.is_open && !in_dialog) {
        // Cancel button: spawn/re-launch physics ball (normal mode)
        if (interaction_mode == MODE_NORMAL && action_pressed(ACTION_CANCEL)) {
            launch_ball(scene);
            burst_particles();
            snd_play(SFX_MODE_CHANGE);
        }

        // Select mode: toggle object select
        if (action_pressed(ACTION_SELECT_MODE)) {
            if (interaction_mode == MODE_NORMAL) {
                interaction_mode = MODE_OBJECT_SELECT;
                if (selected_object < 0)
                    selected_object = scene_find_object(scene, -1, 1, SCENE_OBJ_SELECTABLE);
                snd_play(SFX_OBJ_SELECT);
            } else {
                interaction_mode = MODE_NORMAL;
                selected_object = -1;
                snd_play(SFX_OBJ_DESELECT);
            }
        }

        if (interaction_mode == MODE_OBJECT_SELECT) {
            // Cycle prev/next through the selectable objects
            int step = action_pressed(ACTION_CYCLE_NEXT) - action_pressed(ACTION_CYCLE_PREV);
            int next = step ? scene_find_object(scene, selected_object, step, SCENE_OBJ_SELECTABLE) : -1;
            if (next >= 0) {
                selected_object = next;
                snd_play(SFX_MENU_NAV);
            }
            // Confirm: enter transform mode
            if (action_pressed(ACTION_CONFIRM)) {
                interaction_mode = MODE_OBJECT_TRANSFORM;
                transform_mode = TRANSFORM_MOVE;
                snd_play(SFX_MODE_CHANGE);
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
                snd_play(SFX_MODE_CHANGE);
            }
            // Cancel: back to select
            if (action_pressed(ACTION_CANCEL)) {
                interaction_mode = MODE_OBJECT_SELECT;
                snd_play(SFX_OBJ_DESELECT);
            }
            // Manipulate object with analog/C-buttons
            handle_object_manipulation(scene, &input_state);
        }
    }

    // A body being transformed by hand is held in place (kinematic); it falls
    // again when the transform mode ends
    for (int i = 0; i < scene->object_count; i++) {
        PhysicsBody *body = physics_body_get(&scene->physics, scene->objects[i].body_handle);
        if (body) body->kinematic = interaction_mode == MODE_OBJECT_TRANSFORM && i == selected_object;
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
                snd_play(SFX_MENU_NAV);
            }
        }
        if (was_open && !start_menu.is_open) {
            snd_play(SFX_MENU_SELECT);
        }
    } else if (interaction_mode != MODE_OBJECT_TRANSFORM && !in_dialog) {
        // Camera mode cycling
        if (action_pressed(ACTION_CAM_MODE_PREV)) {
            int mode = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
            mode = (mode + 2) % 3;
            menu_set_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE, mode);
            apply_camera_mode(scene, mode);
            last_camera_mode = mode;
        }
        if (action_pressed(ACTION_CAM_MODE_NEXT)) {
            int mode = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
            mode = (mode + 1) % 3;
            menu_set_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE, mode);
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
        case 0: engine_set_fps_limit(30); break;
        case 1: engine_set_fps_limit(0);  break;  // the display rate (60)
        }
        last_fps_option = fps_opt;
    }

    // Sound tab (Master, SFX and BGM volumes), on change
    apply_sound_settings();

    // UI style: menu and HUD restyle live, even while the menu is open
    int ui_style_idx = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_UI_STYLE);
    if (ui_style_idx != last_ui_style && ui_style_idx >= 0 && ui_style_idx < UI_STYLE_COUNT) {
        ui_style_cur = ui_styles[ui_style_idx];
        if (start_menu_view_ready) menu_view_set_style(&start_menu_view, ui_style_cur);
        last_ui_style = ui_style_idx;
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
        menu_set_value(&start_menu, TAB_ENVIRON, ITEM_FOG_TOGGLE, 1);
        menu_set_value(&start_menu, TAB_ENVIRON, ITEM_SKY_TOGGLE, 1);
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

    // Disabled state for Environ sub-options (custom mode only), on change
    if (atmo_preset != environ_disabled_for) {
        bool is_custom = (atmo_preset == 0);
        menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_TOGGLE, !is_custom);
        menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_NEAR, !is_custom);
        menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_FAR, !is_custom);
        menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_FOG_COLOR, !is_custom);
        menu_item_set_disabled(&start_menu, TAB_ENVIRON, ITEM_SKY_TOGGLE, !is_custom);
        environ_disabled_for = atmo_preset;
    }

    last_atmo_preset = atmo_preset;
    last_fog_toggle = fog_toggle;
    last_fog_near = fog_near_idx;
    last_fog_far = fog_far_idx;
    last_fog_color = fog_color_idx;
    last_sky_toggle = sky_toggle;

    // --- Point lights and torch emitters, applied when their items change (D23) ---
    // (Atmosphere presets and the lighting block never touch point lights;
    // lighting_init on Reset Scene does, and demo_init resets the caches.)
    int pt_lights_idx = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_LIGHTS);
    int pt_color_idx  = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_COLOR);
    int pt_int_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_INTENSITY);
    int pt_rad_idx    = menu_get_value(&start_menu, TAB_LIGHTING, ITEM_PT_RADIUS);
    if (pt_lights_idx != last_pt_lights || pt_color_idx != last_pt_color ||
        pt_int_idx != last_pt_int || pt_rad_idx != last_pt_rad) {
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

        last_pt_lights = pt_lights_idx;
        last_pt_color = pt_color_idx;
        last_pt_int = pt_int_idx;
        last_pt_rad = pt_rad_idx;
    }

    // --- Apply control rebindings from Controls tab, on change (D23) ---
    // Menu item indices match GameAction enum; option indices match PhysicalButton enum
    for (int i = 0; i < ACTION_COUNT; i++) {
        int btn_idx = menu_get_value(&start_menu, TAB_CONTROLS, i);
        if (btn_idx != last_binding[i]) {
            action_set_binding((GameAction)i, (PhysicalButton)btn_idx);
            last_binding[i] = btn_idx;
        }
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

    // Ball bounce: falling velocity turned upward since the last update (the
    // scene steps physics and moves the ball after this function). The sound
    // is heard from the ball's position and scales with the impact speed.
    const SceneObject *ball = scene_get_object(scene, ball_object);
    const PhysicsBody *ball_body = ball ? physics_body_get(&scene->physics, ball->body_handle) : NULL;
    if (ball_body && ball_body->kinematic) {
        ball_prev_vy = 0.0f;                      // held by hand: no bounce
    } else if (ball_body) {
        float vy = ball_body->velocity.y;
        if (ball_prev_vy < -BOUNCE_SOUND_MIN_SPEED && vy >= 0.0f) {
            float gain = -ball_prev_vy / BOUNCE_SOUND_FULL_SPEED;
            snd_play_at(SFX_COLLISION, ball_body->position, gain > 1.0f ? 1.0f : gain);
        }
        ball_prev_vy = vy;
    }

    // The listener is the camera (positional sounds pan with its right vector)
    {
        vec3_t up = {0.0f, 1.0f, 0.0f};
        vec3_t right = vec3_cross(&scene->camera.view_dir, &up);
        float len = vec3_length(&right);
        if (len > 1e-4f) right = vec3_scale(&right, 1.0f / len);
        else right = (vec3_t){1.0f, 0.0f, 0.0f};
        snd_set_listener(scene->camera.position, right);
    }

    // Update particles
    PROF_BEGIN(PROF_PARTICLE_UPDATE);
    particle_update(dt);
    PROF_END(PROF_PARTICLE_UPDATE);
}

// ============================================================
// Scene draw — floor only (objects drawn by per-object callbacks)
// ============================================================

static void demo_draw(Scene *scene) {
    // The sky (when enabled) is drawn by scene_draw() as the background

    // Draw the checkered floor
    PROF_BEGIN(PROF_FLOOR);
    floor_draw(&scene->camera, &scene->lighting);
    PROF_END(PROF_FLOOR);

    // Draw shadows on floor (after floor, before objects)
    PROF_BEGIN(PROF_SHADOWS);
    if (scene->lighting.shadow.mode != SHADOW_OFF) {
        shadow_begin(&scene->camera, &scene->lighting);

        // Every visible shadow caster, including objects added at run time
        // (the ball, D20); only mesh objects carry the flag
        for (int i = 0; i < scene->object_count; i++) {
            const SceneObject *obj = &scene->objects[i];
            if (!obj->visible || !(obj->flags & SCENE_OBJ_CASTS_SHADOW)) continue;

            const ObjectData *data = (const ObjectData *)obj->data;
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
    PROF_END(PROF_SHADOWS);
}

// ============================================================
// Post-draw — HUD and overlays (after all 3D geometry)
// ============================================================

static void demo_post_draw(Scene *scene) {
    // Draw particles (after opaque objects, before HUD)
    PROF_BEGIN(PROF_PARTICLE_DRAW);
    particle_draw(&scene->camera);
    PROF_END(PROF_PARTICLE_DRAW);

    PROF_BEGIN(PROF_HUD);
    // Debug text overlay (HUD work only runs when the HUD is shown; defect D7).
    // The readouts are gathered only when the panels are due (~6 Hz).
    bool show_debug = (menu_get_value(&start_menu, TAB_SETTINGS, ITEM_DEBUG_TEXT) == 0);
    if (show_debug) {
        hud_setup();
        const UiStyle *st = ui_style_cur;
        if (hud_panel_due(&hud_top))
            hud_panel_set(&hud_top, hl_title, st->hud_title, "SMozN64 Dev Engine [" ENGINE_BUILD_NAME "]");

        if (hud_panel_due(&hud_bottom)) {
            int visible_count = 0;
            for (int i = 0; i < scene->object_count; i++) {
                if (scene->objects[i].visible) visible_count++;
            }
            Ray cam_ray;
            cam_ray.origin = scene->camera.position;
            cam_ray.direction = scene->camera.view_dir;
            cam_ray.max_distance = 1000.0f;
            ray_hit = collision_raycast(&scene->collision, &cam_ray,
                                        COLLISION_LAYER_ALL, &ray_result);

            hud_panel_setf(&hud_bottom, hl_obj, st->hud_text, "OBJ:%d/%d VIS:%d",
                           scene->object_count, SCENE_MAX_OBJECTS, visible_count);
            const EngineStats *es = stats_get();       // last complete frame (src/debug/stats.h)
            hud_panel_setf(&hud_bottom, hl_geom, st->hud_text, "T:%lu U:%lu COL:%d RAY:%.0f",
                           (unsigned long)stats_tris_total(es), (unsigned long)es->tex_uploads,
                           scene->collision.result_count, ray_hit ? ray_result.distance : -1.0f);

            // FPS (smoothed over half a second)
            uint32_t now = TICKS_READ();
            if (hud_fps_ticks == 0 || TICKS_DISTANCE(hud_fps_ticks, now) > (int32_t)(TICKS_PER_SECOND / 2)) {
                hud_fps = display_get_fps();
                hud_fps_ticks = now;
            }
            float cpu = profiler_cpu_ms();
            hud_cpu_frac = cpu / engine_frame_budget_ms();
            hud_panel_setf(&hud_bottom, hl_fps, st->hud_accent, "FPS: %.0f CPU:%.1fms", hud_fps, cpu);

            // Right side: selection (object mode only), camera mode and position
            if (interaction_mode != MODE_NORMAL && selected_object >= 0) {
                SceneObject *sel = scene_get_object(scene, selected_object);
                ObjectData *data = sel ? (ObjectData *)sel->data : NULL;
                const char *name = data ? data->name : "???";
                if (interaction_mode == MODE_OBJECT_TRANSFORM)
                    hud_panel_setf(&hud_bottom, hl_sel, st->hud_accent, "SEL:%s [%s]",
                                   name, transform_mode_names[transform_mode]);
                else
                    hud_panel_setf(&hud_bottom, hl_sel, st->hud_accent, "SEL:%s", name);
            } else {
                hud_panel_set(&hud_bottom, hl_sel, st->hud_accent, "");
            }
            int cam_mode_idx = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_CAMERA_MODE);
            hud_panel_setf(&hud_bottom, hl_cam, st->hud_text, "CAM:%s%s",
                           camera_mode_names[cam_mode_idx],
                           scene->camera.collision_enabled ? " COL" : "");
            hud_panel_setf(&hud_bottom, hl_pos, st->hud_text, "XYZ:%.0f,%.0f,%.0f",
                           scene->camera.position.x, scene->camera.position.y,
                           scene->camera.position.z);
        }

        hud_panel_draw(&hud_top, st);
        if (!textbox_active(&dialog_box)) {      // the text box covers the bottom band
            hud_panel_draw(&hud_bottom, st);
            ui_gauge(st->gauge_bg, hud_cpu_frac > 0.9f ? st->gauge_warn : st->gauge_fill,
                     hud_cpu_frac, 4, 226, 4 + HUD_GAUGE_W, 229);
        }
    }

    PROF_END(PROF_HUD);

    // Dialog text box
    if (textbox_active(&dialog_box)) {
        PROF_BEGIN(PROF_DIALOG);
        textbox_draw(&dialog_box, ui_style_cur);
        PROF_END(PROF_DIALOG);
    }

    // Menu overlay
    if (start_menu.is_open) {
        PROF_BEGIN(PROF_MENU);
        if (!start_menu_view_ready) {
            menu_view_init(&start_menu_view, ui_style_cur, true);
            start_menu_view_ready = true;
        }
        menu_draw(&start_menu, &start_menu_view);
        PROF_END(PROF_MENU);
    }
}

// ============================================================
// Cleanup
// ============================================================

static void demo_cleanup(Scene *scene) {
    (void)scene;
    textbox_close(&dialog_box);
    dialog_stop(&dialog_runner);
    dialog_bank_free(dialog_bank);
    dialog_bank = NULL;
    debug_menu_set_shortcuts(true);
    snd_music_stop(0.0f);
    snd_stop_all_sfx();
    particle_cleanup();
    emitter_fire = -1;
    emitter_magic = -1;
    emitter_torch_l = -1;
    emitter_torch_r = -1;
    billboard_cleanup();
    cube_cleanup();
    mesh_defs_cleanup();
    object_data_count = 0;
    billboard_data_count = 0;
    ball_object = -1;
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
    // Loaded by scene_init() before demo_init(), freed by scene_cleanup()
    .texture_paths = {
        "rom:/face_front.sprite", "rom:/face_back.sprite", "rom:/face_top.sprite",
        "rom:/face_bottom.sprite", "rom:/face_right.sprite", "rom:/face_left.sprite",
        "rom:/marker.sprite", "rom:/tree.sprite",
    },
    .texture_slots = {
        TEX_CUBE_FRONT, TEX_CUBE_BACK, TEX_CUBE_TOP,
        TEX_CUBE_BOTTOM, TEX_CUBE_RIGHT, TEX_CUBE_LEFT,
        TEX_BILLBOARD_MARKER, TEX_BILLBOARD_TREE,
    },
    .texture_count = 8,
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
