#ifndef MESH_DEFS_H
#define MESH_DEFS_H

#include "mesh.h"

// Initialize all built-in mesh shapes (call once at scene init)
void mesh_defs_init(void);

// Free all mesh geometry
void mesh_defs_cleanup(void);

// Get shared mesh pointers (valid after mesh_defs_init)
const Mesh *mesh_defs_get_pillar(void);
const Mesh *mesh_defs_get_platform(void);
const Mesh *mesh_defs_get_pyramid(void);

#endif
