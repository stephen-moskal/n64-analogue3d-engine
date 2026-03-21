#ifndef ATMOSPHERE_H
#define ATMOSPHERE_H

#include <libdragon.h>
#include <stdbool.h>

// ============================================================
// Fog Configuration
// ============================================================

typedef struct {
    bool    enabled;
    color_t color;           // Fog color (also set as RDP fog register)
    float   near_distance;   // Camera-space depth where fog starts (0% fog)
    float   far_distance;    // Camera-space depth where fog is 100%
} FogConfig;

// ============================================================
// Sky Configuration
// ============================================================

#define SKY_MAX_BANDS  5

typedef struct {
    bool    enabled;
    int     band_count;                   // Number of gradient bands (2-5)
    color_t band_colors[SKY_MAX_BANDS];   // Top-to-bottom gradient colors
} SkyConfig;

// ============================================================
// Atmosphere Presets
// ============================================================

#define ATMOSPHERE_PRESET_COUNT  7

typedef enum {
    ATMOSPHERE_CLEAR_DAY,
    ATMOSPHERE_OVERCAST,
    ATMOSPHERE_FOGGY,
    ATMOSPHERE_DENSE_FOG,
    ATMOSPHERE_SUNSET,
    ATMOSPHERE_DUSK,
    ATMOSPHERE_NIGHT,
} AtmospherePresetID;

typedef struct {
    const char *name;
    FogConfig   fog;
    SkyConfig   sky;
    color_t     bg_color;    // Background clear color
} AtmospherePreset;

// ============================================================
// Global State API
// ============================================================

void atmosphere_init(void);

// --- Fog ---
void atmosphere_set_fog_enabled(bool enabled);
bool atmosphere_get_fog_enabled(void);
void atmosphere_set_fog_color(color_t color);
void atmosphere_set_fog_near(float near_dist);
float atmosphere_get_fog_near(void);
void atmosphere_set_fog_far(float far_dist);
float atmosphere_get_fog_far(void);
const FogConfig *atmosphere_get_fog(void);

// --- Sky ---
void atmosphere_set_sky_enabled(bool enabled);
bool atmosphere_get_sky_enabled(void);
void atmosphere_set_sky_config(const SkyConfig *sky);
const SkyConfig *atmosphere_get_sky(void);

// --- Presets ---
const AtmospherePreset *atmosphere_get_preset(AtmospherePresetID id);
void atmosphere_apply_preset(AtmospherePresetID id);

// ============================================================
// Utility Functions (used by renderers)
// ============================================================

// Calculate fog factor for a given camera-space depth (clip.w).
// Returns 0.0 = no fog (fully visible), 1.0 = full fog (fully obscured).
float fog_calculate_factor(float camera_depth);

// Blend a color toward fog color by the fog factor for the given depth.
// Returns: lerp(original, fog_color, fog_factor)
color_t fog_blend_color(color_t original, float camera_depth);

// ============================================================
// Rendering
// ============================================================

// Draw sky gradient bands. Call from scene on_draw before floor/geometry.
void sky_draw(void);

#endif
