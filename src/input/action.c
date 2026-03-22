#include "action.h"
#include <string.h>
#include <stdlib.h>

// ============================================================
// State
// ============================================================

static bool act_pressed[ACTION_COUNT];
static bool act_held[ACTION_COUNT];
static bool act_released[ACTION_COUNT];

static PhysicalButton active_bindings[ACTION_COUNT];
static const char *active_context_name = "None";
static float active_deadzone    = 8.0f;
static float active_sensitivity = 0.002f;

static float stick_x_val;
static float stick_y_val;
static bool  has_analog_input;

// ============================================================
// Built-in contexts
// ============================================================

const ActionContext ACTION_CTX_EXPLORATION = {
    .name = "Exploration",
    .bindings = {
        [ACTION_CONFIRM]       = BTN_A,
        [ACTION_CANCEL]        = BTN_B,
        [ACTION_SELECT_MODE]   = BTN_Z,
        [ACTION_CAM_MODE_NEXT] = BTN_R,
        [ACTION_CAM_MODE_PREV] = BTN_L,
        [ACTION_CYCLE_NEXT]    = BTN_D_RIGHT,
        [ACTION_CYCLE_PREV]    = BTN_D_LEFT,
        [ACTION_ZOOM_IN]       = BTN_C_UP,
        [ACTION_ZOOM_OUT]      = BTN_C_DOWN,
        [ACTION_SHIFT_UP]      = BTN_C_RIGHT,
        [ACTION_SHIFT_DOWN]    = BTN_C_LEFT,
    },
    .analog_deadzone    = 8.0f,
    .analog_sensitivity = 0.002f,
};

// ============================================================
// Button name tables
// ============================================================

static const char *button_names[BTN_COUNT] = {
    [BTN_A]       = "A",
    [BTN_B]       = "B",
    [BTN_Z]       = "Z",
    [BTN_L]       = "L",
    [BTN_R]       = "R",
    [BTN_D_UP]    = "D-Up",
    [BTN_D_DOWN]  = "D-Down",
    [BTN_D_LEFT]  = "D-Left",
    [BTN_D_RIGHT] = "D-Right",
    [BTN_C_UP]    = "C-Up",
    [BTN_C_DOWN]  = "C-Down",
    [BTN_C_LEFT]  = "C-Left",
    [BTN_C_RIGHT] = "C-Right",
};

static const char *action_names[ACTION_COUNT] = {
    [ACTION_CONFIRM]       = "Confirm",
    [ACTION_CANCEL]        = "Cancel",
    [ACTION_SELECT_MODE]   = "Select",
    [ACTION_CAM_MODE_NEXT] = "Cam Next",
    [ACTION_CAM_MODE_PREV] = "Cam Prev",
    [ACTION_CYCLE_NEXT]    = "Cycle Next",
    [ACTION_CYCLE_PREV]    = "Cycle Prev",
    [ACTION_ZOOM_IN]       = "Zoom In",
    [ACTION_ZOOM_OUT]      = "Zoom Out",
    [ACTION_SHIFT_UP]      = "Shift Up",
    [ACTION_SHIFT_DOWN]    = "Shift Down",
};

// ============================================================
// Internal: test a physical button in a joypad_buttons_t
// ============================================================

static bool button_test(joypad_buttons_t buttons, PhysicalButton btn) {
    switch (btn) {
    case BTN_A:       return buttons.a;
    case BTN_B:       return buttons.b;
    case BTN_Z:       return buttons.z;
    case BTN_L:       return buttons.l;
    case BTN_R:       return buttons.r;
    case BTN_D_UP:    return buttons.d_up;
    case BTN_D_DOWN:  return buttons.d_down;
    case BTN_D_LEFT:  return buttons.d_left;
    case BTN_D_RIGHT: return buttons.d_right;
    case BTN_C_UP:    return buttons.c_up;
    case BTN_C_DOWN:  return buttons.c_down;
    case BTN_C_LEFT:  return buttons.c_left;
    case BTN_C_RIGHT: return buttons.c_right;
    default:          return false;
    }
}

// ============================================================
// Lifecycle
// ============================================================

void action_init(void) {
    joypad_init();
    action_set_context(&ACTION_CTX_EXPLORATION);
    memset(act_pressed, 0, sizeof(act_pressed));
    memset(act_held, 0, sizeof(act_held));
    memset(act_released, 0, sizeof(act_released));
    stick_x_val = 0.0f;
    stick_y_val = 0.0f;
    has_analog_input = false;
}

void action_update(void) {
    joypad_poll();

    joypad_buttons_t raw_pressed  = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t raw_held     = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_buttons_t raw_released = joypad_get_buttons_released(JOYPAD_PORT_1);

    // Map physical buttons to actions via active bindings
    for (int a = 0; a < ACTION_COUNT; a++) {
        PhysicalButton btn = active_bindings[a];
        act_pressed[a]  = button_test(raw_pressed, btn);
        act_held[a]     = button_test(raw_held, btn);
        act_released[a] = button_test(raw_released, btn);
    }

    // Process analog stick with deadzone and sensitivity
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    int sx = inputs.stick_x;
    int sy = inputs.stick_y;
    int dz = (int)active_deadzone;

    stick_x_val = 0.0f;
    stick_y_val = 0.0f;
    has_analog_input = false;

    if (sx > dz || sx < -dz) {
        stick_x_val = -sx * active_sensitivity;
        has_analog_input = true;
    }
    if (sy > dz || sy < -dz) {
        stick_y_val = sy * active_sensitivity;
        has_analog_input = true;
    }
}

// ============================================================
// Query action states
// ============================================================

bool action_pressed(GameAction action) {
    if (action < 0 || action >= ACTION_COUNT) return false;
    return act_pressed[action];
}

bool action_held(GameAction action) {
    if (action < 0 || action >= ACTION_COUNT) return false;
    return act_held[action];
}

bool action_released(GameAction action) {
    if (action < 0 || action >= ACTION_COUNT) return false;
    return act_released[action];
}

// ============================================================
// Analog stick
// ============================================================

float action_analog_x(void) { return stick_x_val; }
float action_analog_y(void) { return stick_y_val; }
bool  action_has_analog(void) { return has_analog_input; }

// ============================================================
// Context management
// ============================================================

void action_set_context(const ActionContext *ctx) {
    if (!ctx) return;
    active_context_name = ctx->name;
    active_deadzone     = ctx->analog_deadzone;
    active_sensitivity  = ctx->analog_sensitivity;
    memcpy(active_bindings, ctx->bindings, sizeof(active_bindings));
}

const char *action_context_name(void) {
    return active_context_name;
}

// ============================================================
// Remapping
// ============================================================

void action_set_binding(GameAction action, PhysicalButton button) {
    if (action < 0 || action >= ACTION_COUNT) return;
    if (button < 0 || button >= BTN_COUNT) return;
    active_bindings[action] = button;
}

PhysicalButton action_get_binding(GameAction action) {
    if (action < 0 || action >= ACTION_COUNT) return BTN_NONE;
    return active_bindings[action];
}

// ============================================================
// Utility
// ============================================================

const char *action_button_name(PhysicalButton btn) {
    if (btn < 0 || btn >= BTN_COUNT) return "???";
    return button_names[btn];
}

const char *action_name(GameAction action) {
    if (action < 0 || action >= ACTION_COUNT) return "???";
    return action_names[action];
}
