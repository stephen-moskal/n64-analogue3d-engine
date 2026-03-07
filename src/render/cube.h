#ifndef CUBE_H
#define CUBE_H

#include <libdragon.h>
#include "lighting.h"
#include "camera.h"

void cube_init(void);
void cube_update(float dt);
void cube_draw(const Camera *cam, const LightConfig *light);
void cube_cleanup(void);

#endif
