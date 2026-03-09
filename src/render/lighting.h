#ifndef LIGHTING_H
#define LIGHTING_H

#include <libdragon.h>
#include <stdbool.h>
#include "../math/vec3.h"

// --- Point Lights ---

#define MAX_POINT_LIGHTS 4

typedef struct {
    vec3_t position;        // World-space position
    float  color[3];        // RGB color [0.0, 1.0]
    float  intensity;       // Brightness multiplier [0.0, 2.0]
    float  radius;          // Falloff distance (world units)
    bool   active;          // Whether this light contributes
} PointLight;

// --- Shadow Configuration ---

typedef enum {
    SHADOW_OFF,
    SHADOW_BLOB,
    SHADOW_PROJECTED,
} ShadowMode;

typedef struct {
    ShadowMode mode;
    float      darkness;    // 0.0 = invisible, 1.0 = fully black
    float      floor_y;     // Y-plane for projection
    float      blob_radius; // Blob shadow size (world units)
} ShadowConfig;

// --- Light Configuration ---

typedef struct {
    // Directional light (sun)
    float direction[3];         // Normalized, toward light source
    float sun_color[3];         // RGB color [0.0, 1.0]
    float sun_intensity;        // Brightness multiplier [0.0, 2.0]

    // Ambient
    float ambient[3];           // RGB ambient color [0.0, 1.0]

    // Specular
    float specular_intensity;   // Specular highlight strength [0.0, 1.0]
    int   shininess;            // Specular exponent (4, 8, 16, 32, 64)

    // Point lights
    PointLight point_lights[MAX_POINT_LIGHTS];
    int        point_light_count;

    // Shadow
    ShadowConfig shadow;
} LightConfig;

// Initialize with defaults (matches original visual output)
void lighting_init(LightConfig *config);

// Calculate lit color for a surface.
// world_pos: world-space position of surface (for point lights).
//            Pass NULL to skip point light contribution.
color_t lighting_calculate(const LightConfig *config, float normal[3],
                           float view_dir[3], const float *world_pos);

#endif
