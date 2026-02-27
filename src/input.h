#ifndef INPUT_H
#define INPUT_H

#include <libdragon.h>
#include <stdbool.h>

typedef struct {
    float rotation_x;
    float rotation_y;
    bool has_input;
} InputState;

void input_init(void);
void input_update(InputState *state);

#endif
