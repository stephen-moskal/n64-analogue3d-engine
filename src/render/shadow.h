#ifndef SHADOW_H
#define SHADOW_H

#include <libdragon.h>
#include "../math/vec3.h"
#include "camera.h"
#include "lighting.h"
#include "mesh.h"

// Shadow caster data (passed from scene to shadow system)
typedef struct {
    const Mesh   *mesh;         // Mesh geometry to project
    const mat4_t *model;        // World transform of the mesh
    vec3_t        position;     // Object world position (for blob center)
    float         bound_radius; // Bounding radius scaled (for blob sizing)
} ShadowCaster;

// Begin shadow pass: sets up RDP state for shadow rendering.
// Must be called after floor_draw(), before object rendering.
void shadow_begin(const Camera *cam, const LightConfig *light);

// Draw a blob shadow (dark quad) under the given caster
void shadow_draw_blob(const Camera *cam, const LightConfig *light,
                      const ShadowCaster *caster);

// Draw a projected shadow of the caster's mesh onto the floor plane
void shadow_draw_projected(const Camera *cam, const LightConfig *light,
                           const ShadowCaster *caster);

// End shadow pass
void shadow_end(void);

#endif
