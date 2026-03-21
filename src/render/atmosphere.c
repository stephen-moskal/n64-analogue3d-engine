#include "atmosphere.h"
#include <string.h>

// ============================================================
// Global State
// ============================================================

static FogConfig g_fog = {
    .enabled = false,
    .color = {0x80, 0x80, 0x90, 0xFF},
    .near_distance = 200.0f,
    .far_distance  = 1200.0f,
};

static SkyConfig g_sky = {
    .enabled = false,
    .band_count = 4,
    .band_colors = {
        {0x40, 0x60, 0xC0, 0xFF},
        {0x70, 0x90, 0xD0, 0xFF},
        {0xA0, 0xB8, 0xD8, 0xFF},
        {0xC0, 0xD0, 0xE0, 0xFF},
    },
};

// ============================================================
// Presets — bottom sky band = fog color = bg_color for seamless blending
// ============================================================

static const AtmospherePreset presets[ATMOSPHERE_PRESET_COUNT] = {
    [ATMOSPHERE_CLEAR_DAY] = {
        .name = "Clear Day",
        .fog = {
            .enabled = true,
            .color = {0xC0, 0xD8, 0xE8, 0xFF},
            .near_distance = 400.0f,
            .far_distance  = 1400.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 4,
            .band_colors = {
                {0x30, 0x50, 0xC0, 0xFF},
                {0x50, 0x80, 0xD0, 0xFF},
                {0x90, 0xB0, 0xE0, 0xFF},
                {0xC0, 0xD8, 0xE8, 0xFF},
            },
        },
        .bg_color = {0xC0, 0xD8, 0xE8, 0xFF},
        .lighting = {
            .sun_intensity = 1.0f,
            .ambient = {0.20f, 0.20f, 0.25f},
            .sun_color = {0.85f, 0.80f, 0.70f},
        },
    },
    [ATMOSPHERE_OVERCAST] = {
        .name = "Overcast",
        .fog = {
            .enabled = true,
            .color = {0x88, 0x88, 0x90, 0xFF},
            .near_distance = 250.0f,
            .far_distance  = 1000.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 3,
            .band_colors = {
                {0x70, 0x70, 0x78, 0xFF},
                {0x80, 0x80, 0x88, 0xFF},
                {0x88, 0x88, 0x90, 0xFF},
            },
        },
        .bg_color = {0x88, 0x88, 0x90, 0xFF},
        .lighting = {
            .sun_intensity = 0.6f,
            .ambient = {0.25f, 0.25f, 0.28f},
            .sun_color = {0.70f, 0.70f, 0.75f},
        },
    },
    [ATMOSPHERE_FOGGY] = {
        .name = "Foggy",
        .fog = {
            .enabled = true,
            .color = {0xA0, 0xA0, 0xA8, 0xFF},
            .near_distance = 100.0f,
            .far_distance  = 600.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 2,
            .band_colors = {
                {0x90, 0x90, 0x98, 0xFF},
                {0xA0, 0xA0, 0xA8, 0xFF},
            },
        },
        .bg_color = {0xA0, 0xA0, 0xA8, 0xFF},
        .lighting = {
            .sun_intensity = 0.4f,
            .ambient = {0.30f, 0.30f, 0.32f},
            .sun_color = {0.65f, 0.65f, 0.70f},
        },
    },
    [ATMOSPHERE_DENSE_FOG] = {
        .name = "Dense Fog",
        .fog = {
            .enabled = true,
            .color = {0xB0, 0xB0, 0xB0, 0xFF},
            .near_distance = 50.0f,
            .far_distance  = 350.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 2,
            .band_colors = {
                {0xA8, 0xA8, 0xA8, 0xFF},
                {0xB0, 0xB0, 0xB0, 0xFF},
            },
        },
        .bg_color = {0xB0, 0xB0, 0xB0, 0xFF},
        .lighting = {
            .sun_intensity = 0.2f,
            .ambient = {0.35f, 0.35f, 0.35f},
            .sun_color = {0.60f, 0.60f, 0.65f},
        },
    },
    [ATMOSPHERE_SUNSET] = {
        .name = "Sunset",
        .fog = {
            .enabled = true,
            .color = {0xD0, 0x80, 0x40, 0xFF},
            .near_distance = 300.0f,
            .far_distance  = 1200.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 5,
            .band_colors = {
                {0x20, 0x10, 0x40, 0xFF},
                {0x80, 0x30, 0x50, 0xFF},
                {0xD0, 0x60, 0x30, 0xFF},
                {0xE0, 0x90, 0x40, 0xFF},
                {0xD0, 0x80, 0x40, 0xFF},
            },
        },
        .bg_color = {0xD0, 0x80, 0x40, 0xFF},
        .lighting = {
            .sun_intensity = 0.7f,
            .ambient = {0.15f, 0.10f, 0.10f},
            .sun_color = {1.00f, 0.55f, 0.25f},
        },
    },
    [ATMOSPHERE_DUSK] = {
        .name = "Dusk",
        .fog = {
            .enabled = true,
            .color = {0x40, 0x30, 0x60, 0xFF},
            .near_distance = 200.0f,
            .far_distance  = 900.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 4,
            .band_colors = {
                {0x10, 0x08, 0x20, 0xFF},
                {0x30, 0x20, 0x50, 0xFF},
                {0x50, 0x30, 0x60, 0xFF},
                {0x40, 0x30, 0x60, 0xFF},
            },
        },
        .bg_color = {0x40, 0x30, 0x60, 0xFF},
        .lighting = {
            .sun_intensity = 0.3f,
            .ambient = {0.08f, 0.06f, 0.12f},
            .sun_color = {0.50f, 0.30f, 0.50f},
        },
    },
    [ATMOSPHERE_NIGHT] = {
        .name = "Night",
        .fog = {
            .enabled = true,
            .color = {0x08, 0x08, 0x18, 0xFF},
            .near_distance = 300.0f,
            .far_distance  = 1000.0f,
        },
        .sky = {
            .enabled = true,
            .band_count = 3,
            .band_colors = {
                {0x02, 0x02, 0x08, 0xFF},
                {0x05, 0x05, 0x12, 0xFF},
                {0x08, 0x08, 0x18, 0xFF},
            },
        },
        .bg_color = {0x08, 0x08, 0x18, 0xFF},
        .lighting = {
            .sun_intensity = 0.05f,
            .ambient = {0.03f, 0.03f, 0.06f},
            .sun_color = {0.20f, 0.20f, 0.40f},
        },
    },
};

// ============================================================
// Init
// ============================================================

void atmosphere_init(void) {
    g_fog.enabled = false;
    g_fog.color = RGBA32(0x80, 0x80, 0x90, 0xFF);
    g_fog.near_distance = 200.0f;
    g_fog.far_distance  = 1200.0f;

    g_sky.enabled = false;
    g_sky.band_count = 4;
    g_sky.band_colors[0] = RGBA32(0x40, 0x60, 0xC0, 0xFF);
    g_sky.band_colors[1] = RGBA32(0x70, 0x90, 0xD0, 0xFF);
    g_sky.band_colors[2] = RGBA32(0xA0, 0xB8, 0xD8, 0xFF);
    g_sky.band_colors[3] = RGBA32(0xC0, 0xD0, 0xE0, 0xFF);
}

// ============================================================
// Fog Getters/Setters
// ============================================================

void atmosphere_set_fog_enabled(bool enabled) { g_fog.enabled = enabled; }
bool atmosphere_get_fog_enabled(void) { return g_fog.enabled; }

void atmosphere_set_fog_color(color_t color) { g_fog.color = color; }

void atmosphere_set_fog_near(float near_dist) { g_fog.near_distance = near_dist; }
float atmosphere_get_fog_near(void) { return g_fog.near_distance; }

void atmosphere_set_fog_far(float far_dist) { g_fog.far_distance = far_dist; }
float atmosphere_get_fog_far(void) { return g_fog.far_distance; }

const FogConfig *atmosphere_get_fog(void) { return &g_fog; }

// ============================================================
// Sky Getters/Setters
// ============================================================

void atmosphere_set_sky_enabled(bool enabled) { g_sky.enabled = enabled; }
bool atmosphere_get_sky_enabled(void) { return g_sky.enabled; }

void atmosphere_set_sky_config(const SkyConfig *sky) {
    if (sky) g_sky = *sky;
}

const SkyConfig *atmosphere_get_sky(void) { return &g_sky; }

// ============================================================
// Presets
// ============================================================

const AtmospherePreset *atmosphere_get_preset(AtmospherePresetID id) {
    if (id < 0 || id >= ATMOSPHERE_PRESET_COUNT) return &presets[0];
    return &presets[id];
}

void atmosphere_apply_preset(AtmospherePresetID id) {
    const AtmospherePreset *p = atmosphere_get_preset(id);
    g_fog = p->fog;
    g_sky = p->sky;
    // bg_color must be applied by the caller (lives on Scene struct)
}

// ============================================================
// Fog Utility Functions
// ============================================================

float fog_calculate_factor(float camera_depth) {
    if (!g_fog.enabled) return 0.0f;

    float range = g_fog.far_distance - g_fog.near_distance;
    if (range < 1.0f) range = 1.0f;

    float t = (camera_depth - g_fog.near_distance) / range;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return t;
}

color_t fog_blend_color(color_t original, float camera_depth) {
    if (!g_fog.enabled) return original;

    float fog_t = fog_calculate_factor(camera_depth);
    if (fog_t <= 0.0f) return original;

    float inv_t = 1.0f - fog_t;
    uint8_t r = (uint8_t)(original.r * inv_t + g_fog.color.r * fog_t);
    uint8_t g = (uint8_t)(original.g * inv_t + g_fog.color.g * fog_t);
    uint8_t b = (uint8_t)(original.b * inv_t + g_fog.color.b * fog_t);

    return RGBA32(r, g, b, original.a);
}

// ============================================================
// Sky Renderer
// ============================================================

void sky_draw(void) {
    if (!g_sky.enabled || g_sky.band_count < 1) return;

    // Single color: fill entire screen
    if (g_sky.band_count == 1) {
        rdpq_set_mode_fill(g_sky.band_colors[0]);
        rdpq_fill_rectangle(0, 0, 320, 240);
        return;
    }

    // Smooth gradient: subdivide into thin strips and interpolate
    // between band colors (treated as evenly-spaced gradient stops).
    // 4px strips = 60 fill rectangles — negligible RDP cost.
    const int strip_h = 4;
    const int num_strips = 240 / strip_h;
    const int stops = g_sky.band_count;

    for (int s = 0; s < num_strips; s++) {
        int y0 = s * strip_h;
        int y1 = (s == num_strips - 1) ? 240 : y0 + strip_h;

        // Normalized position [0, 1] across screen height
        float t = (float)s / (float)(num_strips - 1);

        // Map to gradient stop pair and fractional position between them
        float seg = t * (stops - 1);
        int idx = (int)seg;
        if (idx >= stops - 1) idx = stops - 2;
        float frac = seg - (float)idx;
        float inv = 1.0f - frac;

        color_t c0 = g_sky.band_colors[idx];
        color_t c1 = g_sky.band_colors[idx + 1];

        color_t c = RGBA32(
            (uint8_t)(c0.r * inv + c1.r * frac),
            (uint8_t)(c0.g * inv + c1.g * frac),
            (uint8_t)(c0.b * inv + c1.b * frac),
            0xFF
        );

        rdpq_set_mode_fill(c);
        rdpq_fill_rectangle(0, y0, 320, y1);
    }
}
