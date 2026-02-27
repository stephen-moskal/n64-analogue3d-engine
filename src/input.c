#include "input.h"

#define ANALOG_DEADZONE 8
#define ANALOG_SCALE 0.001f
#define DPAD_ROTATION 0.03f

void input_init(void) {
    joypad_init();
}

void input_update(InputState *state) {
    joypad_poll();

    state->rotation_x = 0.0f;
    state->rotation_y = 0.0f;
    state->has_input = false;

    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);

    // Analog stick input (smooth rotation)
    int stick_x = inputs.stick_x;
    int stick_y = inputs.stick_y;

    if (stick_x > ANALOG_DEADZONE || stick_x < -ANALOG_DEADZONE) {
        state->rotation_y = stick_x * ANALOG_SCALE;
        state->has_input = true;
    }

    if (stick_y > ANALOG_DEADZONE || stick_y < -ANALOG_DEADZONE) {
        state->rotation_x = -stick_y * ANALOG_SCALE;
        state->has_input = true;
    }

    // D-pad input (discrete rotation)
    if (held.d_left) {
        state->rotation_y = -DPAD_ROTATION;
        state->has_input = true;
    }
    if (held.d_right) {
        state->rotation_y = DPAD_ROTATION;
        state->has_input = true;
    }
    if (held.d_up) {
        state->rotation_x = -DPAD_ROTATION;
        state->has_input = true;
    }
    if (held.d_down) {
        state->rotation_x = DPAD_ROTATION;
        state->has_input = true;
    }
}
