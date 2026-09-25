#include "demo_controls.h"
#include "../engine/util.h"

const ActionBinding demo_bindings[] = {
    BIND(ACT_CONFIRM,     BTN_A),
    BIND(ACT_CANCEL,      BTN_B),
    BIND(ACT_SELECT_MODE, BTN_Z),
    BIND(ACT_CAM_NEXT,    BTN_R),
    BIND(ACT_CAM_PREV,    BTN_L),
    BIND(ACT_CYCLE_NEXT,  BTN_D_RIGHT),
    BIND(ACT_CYCLE_PREV,  BTN_D_LEFT),
    BIND(ACT_ZOOM_IN,     BTN_C_UP),
    BIND(ACT_ZOOM_OUT,    BTN_C_DOWN),
    BIND(ACT_SHIFT_UP,    BTN_C_RIGHT),
    BIND(ACT_SHIFT_DOWN,  BTN_C_LEFT),
    BIND(ACT_MENU,        BTN_START),
    BIND_AXIS(ACT_LOOK_X, AXIS_STICK_X, 1),
    BIND_AXIS(ACT_LOOK_Y, AXIS_STICK_Y, 1),
};
const int demo_binding_count = ARRAY_LEN(demo_bindings);
_Static_assert(ARRAY_LEN(demo_bindings) <= ACTION_MAX_BINDINGS, "the demo's bindings fit a context");
_Static_assert(ACT_DEMO_END <= ACTION_MAX, "the demo's actions fit");

const char *const demo_action_names[DEMO_ACTION_COUNT] = {
    [ACT_CONFIRM - ACTION_GAME_FIRST]     = "Confirm",
    [ACT_CANCEL - ACTION_GAME_FIRST]      = "Cancel",
    [ACT_SELECT_MODE - ACTION_GAME_FIRST] = "Select",
    [ACT_CAM_NEXT - ACTION_GAME_FIRST]    = "Cam Next",
    [ACT_CAM_PREV - ACTION_GAME_FIRST]    = "Cam Prev",
    [ACT_CYCLE_NEXT - ACTION_GAME_FIRST]  = "Cycle Next",
    [ACT_CYCLE_PREV - ACTION_GAME_FIRST]  = "Cycle Prev",
    [ACT_ZOOM_IN - ACTION_GAME_FIRST]     = "Zoom In",
    [ACT_ZOOM_OUT - ACTION_GAME_FIRST]    = "Zoom Out",
    [ACT_SHIFT_UP - ACTION_GAME_FIRST]    = "Shift Up",
    [ACT_SHIFT_DOWN - ACTION_GAME_FIRST]  = "Shift Down",
    [ACT_MENU - ACTION_GAME_FIRST]        = "Menu",
    [ACT_LOOK_X - ACTION_GAME_FIRST]      = "Look X",
    [ACT_LOOK_Y - ACTION_GAME_FIRST]      = "Look Y",
};

PadButton demo_default_button(ActionId action) {
    for (int i = 0; i < demo_binding_count; i++) {
        const ActionBinding *b = &demo_bindings[i];
        if (b->action == action && b->source == SRC_BUTTON) return (PadButton)b->code;
    }
    return BTN_NONE;
}
