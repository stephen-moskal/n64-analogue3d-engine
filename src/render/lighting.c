#include "lighting.h"
#include <math.h>
#include <string.h>

static float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float clamp(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

// Fast integer power via binary exponentiation.
// For shininess=32, only 5 multiplications (vs powf using log/exp).
static float fast_pow_int(float base, int exp) {
    float result = 1.0f;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

void lighting_init(LightConfig *config) {
    memset(config, 0, sizeof(LightConfig));

    // Ambient light (low intensity, slightly blue)
    config->ambient[0] = 0.15f;
    config->ambient[1] = 0.15f;
    config->ambient[2] = 0.20f;

    // Directional light color (warm white)
    config->sun_color[0] = 0.85f;
    config->sun_color[1] = 0.80f;
    config->sun_color[2] = 0.70f;
    config->sun_intensity = 1.0f;

    // Light direction (normalized, from upper-right-front)
    float len = sqrtf(1.0f + 1.0f + 1.0f);
    config->direction[0] = 1.0f / len;
    config->direction[1] = 1.0f / len;
    config->direction[2] = 1.0f / len;

    // Specular
    config->specular_intensity = 0.5f;
    config->shininess = 8;

    // Point lights off by default
    config->point_light_count = 0;

    // Shadows off by default
    config->shadow.mode = SHADOW_OFF;
    config->shadow.darkness = 0.6f;
    config->shadow.floor_y = -100.0f;
    config->shadow.blob_radius = 80.0f;
}

color_t lighting_calculate(const LightConfig *config, float normal[3],
                           float view_dir[3], const float *world_pos) {
    // Ambient component
    float r = config->ambient[0];
    float g = config->ambient[1];
    float b = config->ambient[2];

    // Directional (sun) diffuse component
    float ndotl = dot3(normal, config->direction);
    if (ndotl > 0) {
        float di = config->sun_intensity;
        r += config->sun_color[0] * ndotl * di;
        g += config->sun_color[1] * ndotl * di;
        b += config->sun_color[2] * ndotl * di;
    }

    // Directional specular component (Blinn-Phong)
    float half_vec[3] = {
        config->direction[0] + view_dir[0],
        config->direction[1] + view_dir[1],
        config->direction[2] + view_dir[2]
    };
    float half_len = sqrtf(half_vec[0] * half_vec[0] +
                           half_vec[1] * half_vec[1] +
                           half_vec[2] * half_vec[2]);
    if (half_len > 0.001f) {
        half_vec[0] /= half_len;
        half_vec[1] /= half_len;
        half_vec[2] /= half_len;

        float ndoth = dot3(normal, half_vec);
        if (ndoth > 0) {
            float spec = fast_pow_int(ndoth, config->shininess);
            r += config->specular_intensity * spec;
            g += config->specular_intensity * spec;
            b += config->specular_intensity * spec;
        }
    }

    // Point light contributions (only when world_pos is provided)
    if (world_pos && config->point_light_count > 0) {
        for (int i = 0; i < config->point_light_count; i++) {
            const PointLight *pl = &config->point_lights[i];
            if (!pl->active) continue;

            // Direction from surface to light
            float lx = pl->position.x - world_pos[0];
            float ly = pl->position.y - world_pos[1];
            float lz = pl->position.z - world_pos[2];

            // Squared distance — early out before sqrtf
            float dist_sq = lx * lx + ly * ly + lz * lz;
            float radius_sq = pl->radius * pl->radius;
            if (dist_sq >= radius_sq) continue;

            // Distance and normalize
            float dist = sqrtf(dist_sq);
            if (dist < 0.001f) continue;
            float inv_dist = 1.0f / dist;
            lx *= inv_dist;
            ly *= inv_dist;
            lz *= inv_dist;

            // Smooth quadratic falloff: (1 - (d/r)^2)^2
            float t = dist / pl->radius;
            float atten = (1.0f - t * t);
            atten *= atten;
            atten *= pl->intensity;

            // Diffuse contribution (no specular for point lights — CPU budget)
            float pl_dir[3] = {lx, ly, lz};
            float pl_ndotl = dot3(normal, pl_dir);
            if (pl_ndotl > 0) {
                r += pl->color[0] * pl_ndotl * atten;
                g += pl->color[1] * pl_ndotl * atten;
                b += pl->color[2] * pl_ndotl * atten;
            }
        }
    }

    // Convert to 8-bit color
    uint8_t ir = (uint8_t)(clamp(r, 0.0f, 1.0f) * 255.0f);
    uint8_t ig = (uint8_t)(clamp(g, 0.0f, 1.0f) * 255.0f);
    uint8_t ib = (uint8_t)(clamp(b, 0.0f, 1.0f) * 255.0f);

    return RGBA32(ir, ig, ib, 255);
}
