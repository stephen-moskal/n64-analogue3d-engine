#ifndef PARTICLE_INTERNAL_H
#define PARTICLE_INTERNAL_H

// State shared by the two halves of the particle system: particle.c (pool,
// emitters, update; no rendering, so it runs in the host tests) and
// particle_draw.c (the renderer, hot path). Not a public API: use particle.h.

#include "particle.h"

typedef struct {
    vec3_t  position;
    vec3_t  velocity;
    float   lifetime;           // seconds left
    float   inv_max_lifetime;   // 1 / lifetime at spawn (interpolation without a divide)
    uint8_t color[4];
    float   scale;
    bool    alive;
} Particle;

typedef struct {
    const ParticleEmitterDef *def;
    vec3_t   position;
    bool     active;            // continuous spawning on
    int      pool_start;        // first particle of this emitter's slice of the pool
    int      pool_count;        // particles reserved for this emitter
    float    spawn_accum;       // fractional accumulator for continuous spawning
} ParticleEmitter;

extern Particle        particle_pool[PARTICLE_MAX_POOL];
extern ParticleEmitter particle_emitters[PARTICLE_MAX_EMITTERS];
extern int             particle_pool_allocated;   // particles in use: [0, allocated)
extern bool            particle_initialized;

#endif
