#include "input.h"

#define ZOOM_SPEED         5.0f
#define TARGET_SHIFT_SPEED 2.0f

void input_update(InputState *state) {
    // action_update() must be called before this — it handles joypad_poll()

    state->orbit_azimuth   = action_analog_x();
    state->orbit_elevation = action_analog_y();
    state->zoom_delta      = 0.0f;
    state->target_y_delta  = 0.0f;
    state->has_input       = action_has_analog();

    if (action_held(ACTION_ZOOM_IN)) {
        state->zoom_delta = -ZOOM_SPEED;
        state->has_input = true;
    }
    if (action_held(ACTION_ZOOM_OUT)) {
        state->zoom_delta = ZOOM_SPEED;
        state->has_input = true;
    }
    if (action_held(ACTION_SHIFT_UP)) {
        state->target_y_delta = TARGET_SHIFT_SPEED;
        state->has_input = true;
    }
    if (action_held(ACTION_SHIFT_DOWN)) {
        state->target_y_delta = -TARGET_SHIFT_SPEED;
        state->has_input = true;
    }
}
