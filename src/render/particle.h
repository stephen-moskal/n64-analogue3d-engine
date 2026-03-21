#ifndef PARTICLE_H
#define PARTICLE_H

#include <libdragon.h>
#include <stdbool.h>
#include "../math/vec3.h"
#include "camera.h"

// --- Constants ---

#define PARTICLE_MAX_POOL     128
#define PARTICLE_MAX_EMITTERS 8

// --- Enums ---

typedef enum {
    PARTICLE_BLEND_ADDITIVE,
    PARTICLE_BLEND_ALPHA,
} ParticleBlendMode;

typedef enum {
    PARTICLE_SPAWN_POINT,
    PARTICLE_SPAWN_SPHERE,
} ParticleSpawnShape;

// --- Effect definition (static const, data-driven) ---

typedef struct {
    int   burst_count;          // Particles per burst (0 = continuous only)
    float spawn_rate;           // Particles/sec for continuous mode (0 = burst only)

    float lifetime_min;         // Min lifetime in seconds
    float lifetime_max;         // Max lifetime in seconds

    vec3_t velocity_min;        // Per-axis min initial velocity
    vec3_t velocity_max;        // Per-axis max initial velocity

    vec3_t gravity;             // Acceleration per second^2
    float  drag;                // Velocity damping per second (0=none, 1=full stop)

    uint8_t color_start[4];    // RGBA at birth
    uint8_t color_end[4];      // RGBA at death

    float scale_start;          // Quad half-size at birth
    float scale_end;            // Quad half-size at death

    ParticleSpawnShape spawn_shape;
    float spawn_radius;         // Sphere radius for PARTICLE_SPAWN_SPHERE

    ParticleBlendMode blend_mode;
} ParticleEmitterDef;

// --- API ---

void particle_init(void);
void particle_cleanup(void);

// Create an emitter. Returns handle (0..MAX-1) or -1 on failure.
// pool_size: number of particles reserved for this emitter from global pool.
int  particle_emitter_create(const ParticleEmitterDef *def, vec3_t position,
                             int pool_size);
void particle_emitter_destroy(int handle);
void particle_emitter_set_position(int handle, vec3_t position);
void particle_emitter_burst(int handle);
void particle_emitter_set_active(int handle, bool active);

void particle_update(float dt);
void particle_draw(const Camera *cam);
int  particle_alive_count(void);

#endif
