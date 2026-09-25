#include "particle_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Particle simulation: pool, emitters, update. The renderer is in
// particle_draw.c; this half has no rendering dependencies and is compiled
// into the host unit tests (tests/host/test_particle.c).

// --- State (shared with particle_draw.c through particle_internal.h) ---

Particle        particle_pool[PARTICLE_MAX_POOL];
ParticleEmitter particle_emitters[PARTICLE_MAX_EMITTERS];
int             particle_pool_allocated = 0;
bool            particle_initialized = false;

// --- Random utilities ---

static float rand_range(float min, float max) {
    float t = (float)rand() / (float)RAND_MAX;
    return min + t * (max - min);
}

static vec3_t rand_vec3(const vec3_t *min, const vec3_t *max) {
    return (vec3_t){
        rand_range(min->x, max->x),
        rand_range(min->y, max->y),
        rand_range(min->z, max->z),
    };
}

// --- Lifecycle ---

void particle_init(void) {
    memset(particle_pool, 0, sizeof(particle_pool));
    memset(particle_emitters, 0, sizeof(particle_emitters));
    particle_pool_allocated = 0;
    particle_initialized = true;
    srand(TICKS_READ());
}

void particle_cleanup(void) {
    memset(particle_pool, 0, sizeof(particle_pool));
    memset(particle_emitters, 0, sizeof(particle_emitters));
    particle_pool_allocated = 0;
    particle_initialized = false;
}

// --- Emitter management ---

int particle_emitter_create(const ParticleEmitterDef *def, vec3_t position,
                            int pool_size) {
    if (!particle_initialized || !def) return -1;
    if (particle_pool_allocated + pool_size > PARTICLE_MAX_POOL) return -1;

    // Find free emitter slot
    int handle = -1;
    for (int i = 0; i < PARTICLE_MAX_EMITTERS; i++) {
        if (particle_emitters[i].def == NULL) {
            handle = i;
            break;
        }
    }
    if (handle < 0) return -1;

    ParticleEmitter *em = &particle_emitters[handle];
    em->def = def;
    em->position = position;
    em->active = false;
    em->pool_start = particle_pool_allocated;
    em->pool_count = pool_size;
    em->spawn_accum = 0.0f;

    // Mark all particles in slice as dead
    for (int i = em->pool_start; i < em->pool_start + em->pool_count; i++) {
        particle_pool[i].alive = false;
    }

    particle_pool_allocated += pool_size;
    return handle;
}

void particle_emitter_destroy(int handle) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    ParticleEmitter *em = &particle_emitters[handle];

    // Kill all particles in this emitter's slice
    for (int i = em->pool_start; i < em->pool_start + em->pool_count; i++) {
        particle_pool[i].alive = false;
    }

    memset(em, 0, sizeof(ParticleEmitter));
    em->def = NULL;

    // Compact pool: reclaim freed space at the tail.
    // Scan active emitters to find the highest pool endpoint.
    int max_end = 0;
    for (int i = 0; i < PARTICLE_MAX_EMITTERS; i++) {
        if (particle_emitters[i].def != NULL) {
            int end = particle_emitters[i].pool_start + particle_emitters[i].pool_count;
            if (end > max_end) max_end = end;
        }
    }
    particle_pool_allocated = max_end;
}

void particle_emitter_set_position(int handle, vec3_t position) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    if (particle_emitters[handle].def == NULL) return;
    particle_emitters[handle].position = position;
}

void particle_emitter_set_active(int handle, bool active) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    if (particle_emitters[handle].def == NULL) return;
    particle_emitters[handle].active = active;
}

// --- Spawn a single particle ---

static void spawn_particle(Particle *p, const ParticleEmitter *em) {
    const ParticleEmitterDef *def = em->def;

    // Position: emitter origin + optional sphere offset
    p->position = em->position;
    if (def->spawn_shape == PARTICLE_SPAWN_SPHERE && def->spawn_radius > 0.0f) {
        // Random direction, random distance within radius
        vec3_t offset = {
            rand_range(-1.0f, 1.0f),
            rand_range(-1.0f, 1.0f),
            rand_range(-1.0f, 1.0f),
        };
        float len = vec3_length(&offset);
        if (len > 0.001f) {
            float r = rand_range(0.0f, def->spawn_radius);
            offset = vec3_scale(&offset, r / len);
            p->position = vec3_add(&p->position, &offset);
        }
    }

    // Random velocity within range
    p->velocity = rand_vec3(&def->velocity_min, &def->velocity_max);

    // Random lifetime (a zero lifetime dies on the next update, before use)
    p->lifetime = rand_range(def->lifetime_min, def->lifetime_max);
    p->inv_max_lifetime = p->lifetime > 0.0f ? 1.0f / p->lifetime : 0.0f;

    // Initial color and scale
    p->color[0] = def->color_start[0];
    p->color[1] = def->color_start[1];
    p->color[2] = def->color_start[2];
    p->color[3] = def->color_start[3];
    p->scale = def->scale_start;

    p->alive = true;
}

// --- Burst ---

void particle_emitter_burst(int handle) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    ParticleEmitter *em = &particle_emitters[handle];
    if (em->def == NULL) return;

    int spawned = 0;
    int target = em->def->burst_count;

    for (int i = em->pool_start;
         i < em->pool_start + em->pool_count && spawned < target;
         i++) {
        if (!particle_pool[i].alive) {
            spawn_particle(&particle_pool[i], em);
            spawned++;
        }
    }
}

// --- Update ---

void particle_update(float dt) {
    if (!particle_initialized || dt <= 0.0f) return;

    // Each emitter owns a contiguous slice of the pool, so its particles are
    // updated with its definition directly: no per-particle owner search
    // (roadmap D14), and the definition's constants are loaded once.
    for (int e = 0; e < PARTICLE_MAX_EMITTERS; e++) {
        ParticleEmitter *em = &particle_emitters[e];
        const ParticleEmitterDef *def = em->def;
        if (def == NULL) continue;
        const int start = em->pool_start;
        const int end = em->pool_start + em->pool_count;

        // Continuous emitters: spawn over time
        if (em->active && def->spawn_rate > 0.0f) {
            em->spawn_accum += def->spawn_rate * dt;
            while (em->spawn_accum >= 1.0f) {
                // Find a dead particle in this emitter's slice
                bool found = false;
                for (int i = start; i < end; i++) {
                    if (!particle_pool[i].alive) {
                        spawn_particle(&particle_pool[i], em);
                        found = true;
                        break;
                    }
                }
                em->spawn_accum -= 1.0f;
                if (!found) {
                    em->spawn_accum = 0.0f;
                    break;
                }
            }
        }

        // Per-emitter constants
        const float gx = def->gravity.x * dt;
        const float gy = def->gravity.y * dt;
        const float gz = def->gravity.z * dt;
        float damp = 1.0f;
        if (def->drag > 0.0f) {
            damp = 1.0f - def->drag * dt;
            if (damp < 0.0f) damp = 0.0f;
        }
        float c0[4], dc[4];
        for (int c = 0; c < 4; c++) {
            c0[c] = (float)def->color_start[c];
            dc[c] = (float)def->color_end[c] - c0[c];
        }
        const float s0 = def->scale_start;
        const float ds = def->scale_end - def->scale_start;

        for (int i = start; i < end; i++) {
            Particle *p = &particle_pool[i];
            if (!p->alive) continue;

            // Decrement lifetime
            p->lifetime -= dt;
            if (p->lifetime <= 0.0f) {
                p->alive = false;
                continue;
            }

            // Gravity, then drag
            p->velocity.x = (p->velocity.x + gx) * damp;
            p->velocity.y = (p->velocity.y + gy) * damp;
            p->velocity.z = (p->velocity.z + gz) * damp;

            // Integrate position
            p->position.x += p->velocity.x * dt;
            p->position.y += p->velocity.y * dt;
            p->position.z += p->velocity.z * dt;

            // Interpolation factor: 0 at birth, 1 at death
            float t = 1.0f - p->lifetime * p->inv_max_lifetime;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            // Interpolate color
            for (int c = 0; c < 4; c++) {
                float val = c0[c] + dc[c] * t;
                if (val < 0.0f) val = 0.0f;
                if (val > 255.0f) val = 255.0f;
                p->color[c] = (uint8_t)val;
            }

            // Interpolate scale
            p->scale = s0 + ds * t;
        }
    }
}

// --- Batches for the renderer ---

int particle_batches(ParticleBatch batch[PARTICLE_BLEND_COUNT]) {
    for (int m = 0; m < PARTICLE_BLEND_COUNT; m++) batch[m].count = batch[m].slices = 0;
    if (!particle_initialized) return 0;
    int total = 0;
    for (int e = 0; e < PARTICLE_MAX_EMITTERS; e++) {
        const ParticleEmitter *em = &particle_emitters[e];
        if (em->def == NULL) continue;
        ParticleBatch *b = &batch[em->def->blend_mode == PARTICLE_BLEND_ALPHA ?
                                  PARTICLE_BLEND_ALPHA : PARTICLE_BLEND_ADDITIVE];
        int from = em->pool_start, to = em->pool_start + em->pool_count;
        int n = 0;
        for (int i = from; i < to; i++) n += particle_pool[i].alive;
        b->count += n;
        b->from[b->slices] = (uint8_t)from;
        b->to[b->slices] = (uint8_t)to;
        b->slices++;
        total += n;
    }
    return total;
}

// --- Stats ---

int particle_alive_count(void) {
    int count = 0;
    for (int i = 0; i < particle_pool_allocated; i++) {
        if (particle_pool[i].alive) count++;
    }
    return count;
}
