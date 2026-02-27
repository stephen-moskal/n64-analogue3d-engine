#ifndef CUBE_H
#define CUBE_H

#include <libdragon.h>
#include "lighting.h"

void cube_init(void);
void cube_update(float rot_x, float rot_y);
void cube_draw(const LightConfig *light);
void cube_cleanup(void);

#endif
