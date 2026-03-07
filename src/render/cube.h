#ifndef CUBE_H
#define CUBE_H

#include <libdragon.h>
#include "mesh.h"
#include "lighting.h"
#include "camera.h"

void cube_init(void);
void cube_cleanup(void);

// Returns pointer to the static cube mesh (valid after cube_init)
const Mesh *cube_get_mesh(void);

#endif
