#include "lighting.h"
#include <math.h>

static float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float clamp(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

void lighting_init(LightConfig *config) {
    // Ambient light (low intensity, slightly blue)
    config->ambient[0] = 0.15f;
    config->ambient[1] = 0.15f;
    config->ambient[2] = 0.20f;

    // Diffuse light (warm white)
    config->diffuse[0] = 0.85f;
    config->diffuse[1] = 0.80f;
    config->diffuse[2] = 0.70f;

    // Light direction (normalized, from upper-right-front)
    float len = sqrtf(1.0f + 1.0f + 1.0f);
    config->direction[0] = 1.0f / len;
    config->direction[1] = 1.0f / len;
    config->direction[2] = 1.0f / len;

    // Specular intensity
    config->specular_intensity = 0.5f;
}

color_t lighting_calculate(const LightConfig *config, float normal[3], float view_dir[3]) {
    // Ambient component
    float r = config->ambient[0];
    float g = config->ambient[1];
    float b = config->ambient[2];

    // Diffuse component (Lambert)
    float ndotl = dot3(normal, config->direction);
    if (ndotl > 0) {
        r += config->diffuse[0] * ndotl;
        g += config->diffuse[1] * ndotl;
        b += config->diffuse[2] * ndotl;
    }

    // Specular component (Blinn-Phong approximation)
    // Half vector between light and view
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
            float spec = ndoth * ndoth * ndoth * ndoth;  // shininess ~16
            spec *= ndoth * ndoth * ndoth * ndoth;       // shininess ~32
            r += config->specular_intensity * spec;
            g += config->specular_intensity * spec;
            b += config->specular_intensity * spec;
        }
    }

    // Convert to 8-bit color
    uint8_t ir = (uint8_t)(clamp(r, 0.0f, 1.0f) * 255.0f);
    uint8_t ig = (uint8_t)(clamp(g, 0.0f, 1.0f) * 255.0f);
    uint8_t ib = (uint8_t)(clamp(b, 0.0f, 1.0f) * 255.0f);

    return RGBA32(ir, ig, ib, 255);
}
