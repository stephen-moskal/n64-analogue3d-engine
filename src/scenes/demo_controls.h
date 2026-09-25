#ifndef DEMO_CONTROLS_H
#define DEMO_CONTROLS_H

// The demo's actions and their default bindings (src/input/action.h). Every
// player gets a context built from demo_bindings; the Controls tab
// (ui/settings.c) remaps player 1's, one row per action from
// DEMO_REMAP_FIRST, by the name in demo_action_names.

#include "../input/action.h"

enum {
    ACT_CONFIRM = ACTION_GAME_FIRST,    // A: enter / cycle a mode
    ACT_CANCEL,                         // B: back; launches the ball
    ACT_SELECT_MODE,                    // Z: object selection on / off
    ACT_CAM_NEXT,                       // R: next camera mode
    ACT_CAM_PREV,                       // L: previous camera mode
    ACT_CYCLE_NEXT,                     // D-Right: next object
    ACT_CYCLE_PREV,                     // D-Left: previous object
    ACT_ZOOM_IN,                        // C-Up (held)
    ACT_ZOOM_OUT,                       // C-Down (held)
    ACT_SHIFT_UP,                       // C-Right (held)
    ACT_SHIFT_DOWN,                     // C-Left (held)
    ACT_MENU,                           // Start: open the Start menu (not remappable)
    ACT_LOOK_X, ACT_LOOK_Y,             // main stick: camera orbit, object moves (analog)
    ACT_DEMO_END
};

#define DEMO_REMAP_FIRST  ACT_CONFIRM
#define DEMO_REMAP_COUNT  (ACT_MENU - ACT_CONFIRM)      // the Controls tab rows
#define DEMO_ACTION_COUNT (ACT_DEMO_END - ACTION_GAME_FIRST)

extern const ActionBinding demo_bindings[];
extern const int           demo_binding_count;
extern const char *const   demo_action_names[DEMO_ACTION_COUNT];   // from ACTION_GAME_FIRST

// The button an action is bound to by default (its first button binding)
PadButton demo_default_button(ActionId action);

#endif
