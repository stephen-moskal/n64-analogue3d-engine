#ifndef SETTINGS_H
#define SETTINGS_H

/*
 * The game's options: the Start menu tabs Settings, Sound, Lighting, Environ
 * and Controls (the Debug tab is src/debug/debug_menu.c). Every option is one
 * row of the table in settings.c: its label, its choices, its default, and
 * the value each choice stands for, side by side (D10: the strings used to
 * live in main.c and their meanings in demo_scene.c, matched by raw index).
 *
 * The menu holds which choice is selected. Game code reads the value through
 * the typed accessors below and applies an option when it changes
 * (settings_take). docs/MENU_SYSTEM.md, "Settings".
 */

#include <stdbool.h>
#include <libdragon.h>
#include "menu.h"
#include "../scenes/demo_controls.h"

typedef enum {
    SETTINGS_TAB_GENERAL,       // "Settings"
    SETTINGS_TAB_SOUND,
    SETTINGS_TAB_LIGHTING,
    SETTINGS_TAB_ENVIRON,
    SETTINGS_TAB_CONTROLS,
    SETTINGS_TAB_COUNT
} SettingsTab;

// Every option, tab by tab in menu order, with the type of its value
typedef enum {
    // Settings tab
    SETTING_BG_COLOR,           // color: background without a preset or fog
    SETTING_DEBUG_TEXT,         // bool: the HUD
    SETTING_CAMERA_MODE,        // int: CameraMode
    SETTING_CAMERA_COLLIDE,     // bool
    SETTING_FRAME_RATE,         // int: FPS limit, 0 = the display's rate
    SETTING_RESET_SCENE,        // action: settings_trigger()
    SETTING_UI_STYLE,           // int: index into ui_styles[]
    SETTING_LATENCY,            // int: LatencyChoice
    SETTING_RUMBLE,             // bool
    // Sound tab
    SETTING_SOUND,              // bool: master on/off
    SETTING_SFX_VOLUME,         // float 0..1
    SETTING_MUSIC_VOLUME,       // float 0..1
    // Lighting tab
    SETTING_SUN_DIR,            // vec3: toward the sun
    SETTING_SUN_COLOR,          // vec3: RGB 0..1
    SETTING_BRIGHTNESS,         // float: sun intensity
    SETTING_AMBIENT,            // float
    SETTING_SHADOWS,            // int: ShadowMode
    SETTING_SHADOW_DARKNESS,    // float 0..1
    SETTING_POINT_LIGHTS,       // bool
    SETTING_POINT_COLOR,        // vec3: RGB
    SETTING_POINT_INTENSITY,    // float
    SETTING_POINT_RADIUS,       // float: world units
    // Environ tab
    SETTING_ATMOSPHERE,         // int: AtmospherePresetID, -1 = Custom
    SETTING_FOG,                // bool (Custom)
    SETTING_FOG_NEAR,           // float
    SETTING_FOG_FAR,            // float
    SETTING_FOG_COLOR,          // color
    SETTING_SKY,                // bool (Custom)
    // Controls tab: one per remappable demo action, in its order; int: PadButton
    SETTING_BINDING_FIRST,
    SETTING_COUNT = SETTING_BINDING_FIRST + DEMO_REMAP_COUNT
} SettingId;

// The Controls option of a remappable demo action (DEMO_REMAP_FIRST ...)
#define SETTING_BINDING(action) ((SettingId)(SETTING_BINDING_FIRST + (action) - DEMO_REMAP_FIRST))

// Latency choices (the demo maps them to input_set_sync and engine_set_pacing)
typedef enum {
    LATENCY_CLASSIC,            // newest completed read, render ahead: the engine before S9
    LATENCY_LOW,                // wait for the vblank's read, render ahead (default)
    LATENCY_LOWEST,             // wait for the read, no rendering ahead
} LatencyChoice;

// Add the five tabs to a menu that has none yet (main.c; the Debug tab
// follows). Every option starts at its default and counts as changed.
void settings_init(Menu *menu);

// The selected choice (0 .. count-1), its name, and the value it stands for.
// Each accessor must match the option's type (asserted).
int          settings_choice(SettingId id);
const char  *settings_choice_name(SettingId id);
bool         settings_bool(SettingId id);
int          settings_int(SettingId id);
float        settings_float(SettingId id);
const float *settings_vec3(SettingId id);
color_t      settings_color(SettingId id);

// Change the selection from code: a choice, the next or previous one
// (wrapping), or the choice whose value is `on`. Or grey an option out: the
// cursor still visits it, but its value can't change.
void settings_set_choice(SettingId id, int choice);
void settings_step_choice(SettingId id, int dir);
void settings_set_bool(SettingId id, bool on);
void settings_set_disabled(SettingId id, bool disabled);

// An action option ("---" / "Reset!"): true once when it is set, which also
// puts it back
bool settings_trigger(SettingId id);

// Changes. An option has changed when its choice differs from the one last
// taken. settings_take() answers and takes it; settings_take_range() takes
// first..last and answers whether any of them changed (one apply for a
// group). settings_invalidate() marks every option changed: game code whose
// state was just reset calls it, so that the next update applies every
// option and the scene matches the menu (D27).
bool settings_take(SettingId id);
bool settings_take_range(SettingId first, SettingId last);
void settings_invalidate(void);

// The table (for tests and tools)
typedef enum {
    SETTING_TYPE_ACTION,        // no values: choice 0 = idle, 1 = do it
    SETTING_TYPE_BOOL,
    SETTING_TYPE_INT,
    SETTING_TYPE_FLOAT,
    SETTING_TYPE_VEC3,
    SETTING_TYPE_COLOR,
} SettingType;

typedef struct {
    SettingsTab  tab;
    const char  *label;
    const char *const *choices;  // what the menu shows
    int          count;
    int          default_choice;
    SettingType  type;
    const void  *values;         // `value_count` values of `type` (NULL for an action)
    int          value_count;    // equals `count`
} SettingDef;

const SettingDef *settings_def(SettingId id);
extern const char *const settings_tab_labels[SETTINGS_TAB_COUNT];

#endif
