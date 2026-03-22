#ifndef INPUT_H
#define INPUT_H

#include <libdragon.h>
#include <stdbool.h>
#include "action.h"

typedef struct {
    float orbit_azimuth;
    float orbit_elevation;
    float zoom_delta;
    float target_y_delta;
    bool has_input;
} InputState;

void input_update(InputState *state);

#endif
