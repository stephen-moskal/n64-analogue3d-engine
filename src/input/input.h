#ifndef INPUT_H
#define INPUT_H

#include <libdragon.h>
#include <stdbool.h>

typedef struct {
    float orbit_azimuth;
    float orbit_elevation;
    float zoom_delta;
    float target_y_delta;
    bool has_input;
} InputState;

void input_init(void);
void input_update(InputState *state);

#endif
