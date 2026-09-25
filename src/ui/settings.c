#include "settings.h"
#include <string.h>
#include "ui_style.h"
#include "../render/camera.h"
#include "../render/lighting.h"
#include "../render/atmosphere.h"
#include "../engine/util.h"

// The game's options (settings.h). Each option's choices and the values they
// stand for are declared side by side and checked to have the same length;
// the table below ties them to a label, a tab and a default. To add an
// option: docs/EXTENDING.md, "Add a Start menu item".

// Choice names and their values: the same length, and no more choices than
// a menu item holds (extra ones would be dropped silently)
#define CHOICE_TABLE(names, values)                                                    \
    _Static_assert(ARRAY_LEN(names) == ARRAY_LEN(values),                              \
                   #names " and " #values " differ in length");                        \
    _Static_assert(ARRAY_LEN(names) <= MENU_MAX_OPTIONS,                               \
                   #names " has more choices than a menu item holds")

#define CHOICES(names)        .choices = names, .count = ARRAY_LEN(names)
#define VALUES(type_, vals)   .type = type_, .values = vals, .value_count = ARRAY_LEN(vals)

// --- Shared choices ---

static const char *const off_on[] = {"Off", "On"};
static const bool off_on_values[] = {false, true};
CHOICE_TABLE(off_on, off_on_values);

static const char *const on_off[] = {"On", "Off"};
static const bool on_off_values[] = {true, false};
CHOICE_TABLE(on_off, on_off_values);

// --- Settings tab ---

static const char *const bg_names[] = {
    "Dark Blue", "Black", "Dark Red", "Dark Green", "Dark Purple", "Light Blue", "White",
};
static const color_t bg_values[] = {
    {0x10, 0x10, 0x30, 0xFF}, {0x00, 0x00, 0x00, 0xFF}, {0x30, 0x10, 0x10, 0xFF},
    {0x10, 0x30, 0x10, 0xFF}, {0x20, 0x10, 0x30, 0xFF}, {0x60, 0x80, 0xD0, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF},
};
CHOICE_TABLE(bg_names, bg_values);

static const char *const camera_names[] = {"Orbital", "Fixed", "Follow"};
static const int camera_values[] = {CAMERA_MODE_ORBITAL, CAMERA_MODE_FIXED, CAMERA_MODE_FOLLOW};
CHOICE_TABLE(camera_names, camera_values);

static const char *const fps_names[] = {"30", "60"};
static const int fps_values[] = {30, 0};           // 0: no limit, the display's rate
CHOICE_TABLE(fps_names, fps_values);

static const char *const reset_names[] = {"---", "Reset!"};

static const char *const ui_style_names[] = {"Debug", "Classic", "Minimal"};
static const int ui_style_values[] = {0, 1, 2};    // index into ui_styles[]
CHOICE_TABLE(ui_style_names, ui_style_values);
_Static_assert(ARRAY_LEN(ui_style_names) == UI_STYLE_COUNT, "one choice per built-in UI style");

static const char *const latency_names[] = {"Classic", "Low", "Lowest"};
static const int latency_values[] = {LATENCY_CLASSIC, LATENCY_LOW, LATENCY_LOWEST};
CHOICE_TABLE(latency_names, latency_values);

// --- Sound tab ---

static const char *const volume_names[] = {
    "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%",
};
static const float volume_values[] = {
    0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f,
};
CHOICE_TABLE(volume_names, volume_values);

// --- Lighting tab ---

static const char *const sun_dir_names[] = {"Front", "Side", "Top", "Sunset", "Dawn"};
static const float sun_dir_values[][3] = {
    { 0.577f, 0.577f,  0.577f},     // Front
    {-0.707f, 0.707f,  0.000f},     // Side: from the left, 45 degrees
    { 0.000f, 1.000f,  0.000f},     // Top: straight down
    { 0.866f, 0.200f,  0.458f},     // Sunset: low
    {-0.866f, 0.200f, -0.458f},     // Dawn: opposite the sunset
};
CHOICE_TABLE(sun_dir_names, sun_dir_values);

static const char *const sun_color_names[] = {"Warm", "Cool", "Neutral", "Golden"};
static const float sun_color_values[][3] = {
    {0.85f, 0.80f, 0.70f}, {0.70f, 0.80f, 0.90f}, {0.85f, 0.85f, 0.85f}, {1.00f, 0.75f, 0.40f},
};
CHOICE_TABLE(sun_color_names, sun_color_values);

static const char *const brightness_names[] = {"20%", "40%", "60%", "80%", "100%"};
static const float brightness_values[] = {0.20f, 0.40f, 0.60f, 0.80f, 1.00f};
CHOICE_TABLE(brightness_names, brightness_values);

static const char *const ambient_names[] = {"10%", "20%", "30%", "40%", "50%"};
static const float ambient_values[] = {0.10f, 0.20f, 0.30f, 0.40f, 0.50f};
CHOICE_TABLE(ambient_names, ambient_values);

static const char *const shadow_names[] = {"Off", "Blob", "Projected"};
static const int shadow_values[] = {SHADOW_OFF, SHADOW_BLOB, SHADOW_PROJECTED};
CHOICE_TABLE(shadow_names, shadow_values);

static const char *const shadow_dark_names[] = {"Light", "Medium", "Dark"};
static const float shadow_dark_values[] = {0.3f, 0.6f, 0.9f};
CHOICE_TABLE(shadow_dark_names, shadow_dark_values);

static const char *const point_color_names[] = {"Warm", "Cool", "Red", "Green", "Blue", "White"};
static const float point_color_values[][3] = {
    {1.0f, 0.7f, 0.3f}, {0.5f, 0.7f, 1.0f}, {1.0f, 0.3f, 0.2f},
    {0.3f, 1.0f, 0.3f}, {0.3f, 0.3f, 1.0f}, {1.0f, 1.0f, 1.0f},
};
CHOICE_TABLE(point_color_names, point_color_values);

static const char *const point_intensity_names[] = {
    "0.4", "0.8", "1.2", "2.0", "3.0", "5.0", "8.0", "12", "16", "24",
};
static const float point_intensity_values[] = {
    0.4f, 0.8f, 1.2f, 2.0f, 3.0f, 5.0f, 8.0f, 12.0f, 16.0f, 24.0f,
};
CHOICE_TABLE(point_intensity_names, point_intensity_values);

static const char *const point_radius_names[] = {
    "100", "200", "300", "400", "600", "800", "1000", "1200", "1500", "2000",
};
static const float point_radius_values[] = {
    100.0f, 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f, 1200.0f, 1500.0f, 2000.0f,
};
CHOICE_TABLE(point_radius_names, point_radius_values);

// --- Environ tab ---

static const char *const atmosphere_names[] = {
    "Custom", "Clear Day", "Overcast", "Foggy", "Dense Fog", "Sunset", "Dusk", "Night",
};
static const int atmosphere_values[] = {
    -1, ATMOSPHERE_CLEAR_DAY, ATMOSPHERE_OVERCAST, ATMOSPHERE_FOGGY, ATMOSPHERE_DENSE_FOG,
    ATMOSPHERE_SUNSET, ATMOSPHERE_DUSK, ATMOSPHERE_NIGHT,
};
CHOICE_TABLE(atmosphere_names, atmosphere_values);
_Static_assert(ARRAY_LEN(atmosphere_names) == ATMOSPHERE_PRESET_COUNT + 1, "Custom, then every preset");

static const char *const fog_near_names[] = {"50", "100", "150", "200", "300", "400"};
static const float fog_near_values[] = {50, 100, 150, 200, 300, 400};
CHOICE_TABLE(fog_near_names, fog_near_values);

static const char *const fog_far_names[] = {"400", "600", "800", "1000", "1200", "1400"};
static const float fog_far_values[] = {400, 600, 800, 1000, 1200, 1400};
CHOICE_TABLE(fog_far_names, fog_far_values);

static const char *const fog_color_names[] = {"Grey", "Blue", "White", "Warm", "Purple", "Dark"};
static const color_t fog_color_values[] = {
    {0x80, 0x80, 0x90, 0xFF}, {0x60, 0x80, 0xD0, 0xFF}, {0xC0, 0xC0, 0xC0, 0xFF},
    {0xD0, 0x80, 0x40, 0xFF}, {0x50, 0x30, 0x60, 0xFF}, {0x10, 0x10, 0x20, 0xFF},
};
CHOICE_TABLE(fog_color_names, fog_color_values);

// --- Controls tab: built by settings_init() from the demo's actions ---

// The buttons an action can be bound to, in the order the tab cycles through
// them (Start opens the menu; X and Y exist on a GameCube controller only)
static const int button_values[] = {
    BTN_A, BTN_B, BTN_Z, BTN_L, BTN_R, BTN_D_UP, BTN_D_DOWN, BTN_D_LEFT, BTN_D_RIGHT,
    BTN_C_UP, BTN_C_DOWN, BTN_C_LEFT, BTN_C_RIGHT,
};
static const char *button_names[ARRAY_LEN(button_values)];   // pad_button_name()
_Static_assert(ARRAY_LEN(button_values) <= MENU_MAX_OPTIONS, "every button fits in a menu item");

// --- The table ---

const char *const settings_tab_labels[SETTINGS_TAB_COUNT] = {
    "Settings", "Sound", "Lighting", "Environ", "Controls",
};

#define G SETTINGS_TAB_GENERAL
#define S SETTINGS_TAB_SOUND
#define L SETTINGS_TAB_LIGHTING
#define E SETTINGS_TAB_ENVIRON

static const SettingDef defs[SETTING_BINDING_FIRST] = {
    [SETTING_BG_COLOR]        = { G, "BG Color",     CHOICES(bg_names),         5, VALUES(SETTING_TYPE_COLOR, bg_values) },   // Light Blue
    [SETTING_DEBUG_TEXT]      = { G, "Debug Text",   CHOICES(on_off),           0, VALUES(SETTING_TYPE_BOOL, on_off_values) },
    [SETTING_CAMERA_MODE]     = { G, "Camera",       CHOICES(camera_names),     0, VALUES(SETTING_TYPE_INT, camera_values) },
    [SETTING_CAMERA_COLLIDE]  = { G, "Cam Collide",  CHOICES(off_on),           0, VALUES(SETTING_TYPE_BOOL, off_on_values) },
    [SETTING_FRAME_RATE]      = { G, "Frame Rate",   CHOICES(fps_names),        1, VALUES(SETTING_TYPE_INT, fps_values) },    // 60
    [SETTING_RESET_SCENE]     = { G, "Reset Scene",  CHOICES(reset_names),      0, .type = SETTING_TYPE_ACTION },
    [SETTING_UI_STYLE]        = { G, "UI Style",     CHOICES(ui_style_names),   0, VALUES(SETTING_TYPE_INT, ui_style_values) },
    [SETTING_LATENCY]         = { G, "Latency",      CHOICES(latency_names),    1, VALUES(SETTING_TYPE_INT, latency_values) },  // Low
    [SETTING_RUMBLE]          = { G, "Rumble",       CHOICES(on_off),           0, VALUES(SETTING_TYPE_BOOL, on_off_values) },

    [SETTING_SOUND]           = { S, "Master",       CHOICES(on_off),           1, VALUES(SETTING_TYPE_BOOL, on_off_values) },   // Off
    [SETTING_SFX_VOLUME]      = { S, "SFX Vol",      CHOICES(volume_names),     8, VALUES(SETTING_TYPE_FLOAT, volume_values) },  // 80%
    [SETTING_MUSIC_VOLUME]    = { S, "BGM Vol",      CHOICES(volume_names),     6, VALUES(SETTING_TYPE_FLOAT, volume_values) },  // 60%

    [SETTING_SUN_DIR]         = { L, "Sun Dir",      CHOICES(sun_dir_names),    0, VALUES(SETTING_TYPE_VEC3, sun_dir_values) },
    [SETTING_SUN_COLOR]       = { L, "Sun Color",    CHOICES(sun_color_names),  0, VALUES(SETTING_TYPE_VEC3, sun_color_values) },
    [SETTING_BRIGHTNESS]      = { L, "Brightness",   CHOICES(brightness_names), 4, VALUES(SETTING_TYPE_FLOAT, brightness_values) },  // 100%
    [SETTING_AMBIENT]         = { L, "Ambient",      CHOICES(ambient_names),    1, VALUES(SETTING_TYPE_FLOAT, ambient_values) },     // 20%
    [SETTING_SHADOWS]         = { L, "Shadows",      CHOICES(shadow_names),     0, VALUES(SETTING_TYPE_INT, shadow_values) },
    [SETTING_SHADOW_DARKNESS] = { L, "Shadow Dark",  CHOICES(shadow_dark_names), 1, VALUES(SETTING_TYPE_FLOAT, shadow_dark_values) }, // Medium
    [SETTING_POINT_LIGHTS]    = { L, "Pt Lights",    CHOICES(off_on),           0, VALUES(SETTING_TYPE_BOOL, off_on_values) },
    [SETTING_POINT_COLOR]     = { L, "Pt Color",     CHOICES(point_color_names), 0, VALUES(SETTING_TYPE_VEC3, point_color_values) },
    [SETTING_POINT_INTENSITY] = { L, "Pt Intensity", CHOICES(point_intensity_names), 2, VALUES(SETTING_TYPE_FLOAT, point_intensity_values) }, // 1.2
    [SETTING_POINT_RADIUS]    = { L, "Pt Radius",    CHOICES(point_radius_names), 2, VALUES(SETTING_TYPE_FLOAT, point_radius_values) },       // 300

    [SETTING_ATMOSPHERE]      = { E, "Preset",       CHOICES(atmosphere_names), 0, VALUES(SETTING_TYPE_INT, atmosphere_values) },  // Custom
    [SETTING_FOG]             = { E, "Fog",          CHOICES(off_on),           0, VALUES(SETTING_TYPE_BOOL, off_on_values) },
    [SETTING_FOG_NEAR]        = { E, "Fog Near",     CHOICES(fog_near_names),   3, VALUES(SETTING_TYPE_FLOAT, fog_near_values) },   // 200
    [SETTING_FOG_FAR]         = { E, "Fog Far",      CHOICES(fog_far_names),    4, VALUES(SETTING_TYPE_FLOAT, fog_far_values) },    // 1200
    [SETTING_FOG_COLOR]       = { E, "Fog Color",    CHOICES(fog_color_names),  0, VALUES(SETTING_TYPE_COLOR, fog_color_values) },
    [SETTING_SKY]             = { E, "Sky",          CHOICES(off_on),           0, VALUES(SETTING_TYPE_BOOL, off_on_values) },
};

#undef G
#undef S
#undef L
#undef E

// Each tab fits in a menu tab, and the Debug tab after them in the menu
_Static_assert(SETTING_SOUND - SETTING_BG_COLOR <= MENU_MAX_ITEMS, "Settings tab too long");
_Static_assert(SETTING_SUN_DIR - SETTING_SOUND <= MENU_MAX_ITEMS, "Sound tab too long");
_Static_assert(SETTING_ATMOSPHERE - SETTING_SUN_DIR <= MENU_MAX_ITEMS, "Lighting tab too long");
_Static_assert(SETTING_BINDING_FIRST - SETTING_ATMOSPHERE <= MENU_MAX_ITEMS, "Environ tab too long");
_Static_assert(DEMO_REMAP_COUNT <= MENU_MAX_ITEMS, "one Controls item per remappable action");
_Static_assert(SETTINGS_TAB_COUNT + 1 <= MENU_MAX_TABS, "the settings tabs and the Debug tab");

static SettingDef binding_defs[DEMO_REMAP_COUNT];  // Controls tab (settings_init)

// --- State ---

static Menu   *menu;
static uint8_t item_of[SETTING_COUNT];              // the option's row in its tab
static int8_t  taken[SETTING_COUNT];                // choice last taken, -1 = changed

const SettingDef *settings_def(SettingId id) {
    assertf(id >= 0 && id < SETTING_COUNT, "no setting %d", id);
    return id < SETTING_BINDING_FIRST ? &defs[id] : &binding_defs[id - SETTING_BINDING_FIRST];
}

// The Controls choice for a button (0 if it is not one of them)
static int button_choice(PadButton btn) {
    for (int c = 0; c < (int)ARRAY_LEN(button_values); c++)
        if (button_values[c] == btn) return c;
    return 0;
}

void settings_init(Menu *m) {
    assertf(m->tab_count == 0, "settings_init: the settings tabs come first");
    menu = m;

    // Controls: one option per remappable demo action, its choices the
    // bindable buttons, its default the action's default button
    for (int c = 0; c < (int)ARRAY_LEN(button_values); c++)
        button_names[c] = pad_button_name((PadButton)button_values[c]);
    for (int i = 0; i < DEMO_REMAP_COUNT; i++) {
        ActionId a = (ActionId)(DEMO_REMAP_FIRST + i);
        binding_defs[i] = (SettingDef){
            .tab = SETTINGS_TAB_CONTROLS, .label = demo_action_names[a - ACTION_GAME_FIRST],
            .choices = button_names, .count = ARRAY_LEN(button_values),
            .default_choice = button_choice(demo_default_button(a)),
            VALUES(SETTING_TYPE_INT, button_values),
        };
    }

    for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
        int tab = menu_add_tab(menu, settings_tab_labels[t]);
        assertf(tab == t, "settings_init: tab %d came out as %d", t, tab);
        (void)tab;   // release builds compile the assert out
    }
    for (int id = 0; id < SETTING_COUNT; id++) {
        const SettingDef *d = settings_def((SettingId)id);
        int item = menu_add_item(menu, d->tab, d->label, d->choices, d->count, d->default_choice);
        assertf(item >= 0, "settings_init: no room for %s", d->label);
        item_of[id] = (uint8_t)item;
    }
    settings_invalidate();
}

// --- Reading ---

int settings_choice(SettingId id) {
    const SettingDef *d = settings_def(id);
    int c = menu_get_value(menu, d->tab, item_of[id]);
    return (c >= 0 && c < d->count) ? c : d->default_choice;
}

const char *settings_choice_name(SettingId id) {
    return settings_def(id)->choices[settings_choice(id)];
}

static const void *value_of(SettingId id, SettingType type, int size) {
    const SettingDef *d = settings_def(id);
    assertf(d->type == type, "setting %s is not of type %d", d->label, (int)type);
    return (const char *)d->values + settings_choice(id) * size;
}

bool settings_bool(SettingId id) {
    return *(const bool *)value_of(id, SETTING_TYPE_BOOL, sizeof(bool));
}

int settings_int(SettingId id) {
    return *(const int *)value_of(id, SETTING_TYPE_INT, sizeof(int));
}

float settings_float(SettingId id) {
    return *(const float *)value_of(id, SETTING_TYPE_FLOAT, sizeof(float));
}

const float *settings_vec3(SettingId id) {
    return (const float *)value_of(id, SETTING_TYPE_VEC3, 3 * sizeof(float));
}

color_t settings_color(SettingId id) {
    return *(const color_t *)value_of(id, SETTING_TYPE_COLOR, sizeof(color_t));
}

// --- Changing ---

void settings_set_choice(SettingId id, int choice) {
    const SettingDef *d = settings_def(id);
    menu_set_value(menu, d->tab, item_of[id], choice);   // ignores a choice out of range
}

void settings_step_choice(SettingId id, int dir) {
    int n = settings_def(id)->count;
    settings_set_choice(id, ((settings_choice(id) + dir) % n + n) % n);
}

void settings_set_bool(SettingId id, bool on) {
    const SettingDef *d = settings_def(id);
    assertf(d->type == SETTING_TYPE_BOOL, "setting %s is not a bool", d->label);
    for (int c = 0; c < d->count; c++) {
        if (((const bool *)d->values)[c] == on) {
            settings_set_choice(id, c);
            return;
        }
    }
}

void settings_set_disabled(SettingId id, bool disabled) {
    const SettingDef *d = settings_def(id);
    menu_item_set_disabled(menu, d->tab, item_of[id], disabled);
}

bool settings_trigger(SettingId id) {
    assertf(settings_def(id)->type == SETTING_TYPE_ACTION, "setting %d is not an action", id);
    if (settings_choice(id) == 0) return false;
    settings_set_choice(id, 0);
    return true;
}

// --- Changes ---

bool settings_take(SettingId id) {
    int c = settings_choice(id);
    bool changed = (c != taken[id]);
    taken[id] = (int8_t)c;
    return changed;
}

bool settings_take_range(SettingId first, SettingId last) {
    bool changed = false;
    for (int id = first; id <= last; id++)
        changed |= settings_take((SettingId)id);   // takes every one, no short cut
    return changed;
}

void settings_invalidate(void) {
    memset(taken, -1, sizeof(taken));
}
