#ifndef BILLBOARD_H
#define BILLBOARD_H

#include <libdragon.h>
#include <stdbool.h>
#include "../math/vec3.h"
#include "camera.h"
#include "lighting.h"
#include "../scene/scene.h"

typedef enum {
    BILLBOARD_SPHERICAL,    // Fully faces camera (particles, markers)
    BILLBOARD_CYLINDRICAL,  // Only rotates around Y axis (trees, NPCs)
} BillboardMode;

typedef struct {
    int texture_slot;       // Index into texture system
    BillboardMode mode;
    float width, height;    // World-space dimensions
    uint8_t color[3];       // Tint / base color (modulated by lighting)
} BillboardData;

// Initialize shared billboard quad mesh (call once at scene init)
void billboard_init(void);

// Free shared billboard quad mesh
void billboard_cleanup(void);

// SceneObject.on_draw callback — reads BillboardData from obj->data
void billboard_draw(SceneObject *obj, const Camera *cam, const LightConfig *light);

#endif
