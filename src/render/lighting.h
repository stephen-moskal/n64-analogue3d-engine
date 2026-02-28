#ifndef LIGHTING_H
#define LIGHTING_H

#include <libdragon.h>

typedef struct {
    float ambient[3];
    float diffuse[3];
    float direction[3];
    float specular_intensity;
} LightConfig;

void lighting_init(LightConfig *config);
color_t lighting_calculate(const LightConfig *config, float normal[3], float view_dir[3]);

#endif
