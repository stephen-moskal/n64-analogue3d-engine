#ifndef FLOOR_H
#define FLOOR_H

#include "camera.h"
#include "lighting.h"

#define FLOOR_Y -100.0f

void floor_draw(const Camera *cam, const LightConfig *light);

#endif
