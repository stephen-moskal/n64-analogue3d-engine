#include "input.h"

#define ANALOG_DEADZONE    8
#define ANALOG_SCALE       0.002f
#define ZOOM_SPEED         5.0f
#define TARGET_SHIFT_SPEED 2.0f

void input_init(void) {
    joypad_init();
}

void input_update(InputState *state) {
    joypad_poll();

    state->orbit_azimuth   = 0.0f;
    state->orbit_elevation = 0.0f;
    state->zoom_delta      = 0.0f;
    state->target_y_delta  = 0.0f;
    state->has_input       = false;

    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t held  = joypad_get_buttons_held(JOYPAD_PORT_1);

    // Analog stick -> camera orbit
    int stick_x = inputs.stick_x;
    int stick_y = inputs.stick_y;

    if (stick_x > ANALOG_DEADZONE || stick_x < -ANALOG_DEADZONE) {
        state->orbit_azimuth = -stick_x * ANALOG_SCALE;
        state->has_input = true;
    }
    if (stick_y > ANALOG_DEADZONE || stick_y < -ANALOG_DEADZONE) {
        state->orbit_elevation = stick_y * ANALOG_SCALE;
        state->has_input = true;
    }

    // C-up/C-down -> zoom
    if (held.c_up) {
        state->zoom_delta = -ZOOM_SPEED;
        state->has_input = true;
    }
    if (held.c_down) {
        state->zoom_delta = ZOOM_SPEED;
        state->has_input = true;
    }

    // C-left/C-right -> shift target Y
    if (held.c_left) {
        state->target_y_delta = -TARGET_SHIFT_SPEED;
        state->has_input = true;
    }
    if (held.c_right) {
        state->target_y_delta = TARGET_SHIFT_SPEED;
        state->has_input = true;
    }
}
