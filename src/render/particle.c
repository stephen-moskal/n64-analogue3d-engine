#include "particle.h"
#include "texture.h"
#include "atmosphere.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- Internal particle state ---

typedef struct {
    vec3_t  position;
    vec3_t  velocity;
    float   lifetime;
    float   max_lifetime;
    uint8_t color[4];
    float   scale;
    bool    alive;
} Particle;

typedef struct {
    const ParticleEmitterDef *def;
    vec3_t   position;
    bool     active;        // For continuous spawning
    int      pool_start;    // Index into global pool
    int      pool_count;    // Number of particles reserved
    float    spawn_accum;   // Fractional accumulator for continuous spawning
} ParticleEmitter;

// --- Static state ---

static Particle       pool[PARTICLE_MAX_POOL];
static ParticleEmitter emitters[PARTICLE_MAX_EMITTERS];
static int            pool_allocated = 0;
static bool           initialized = false;

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

// --- Guard band (same as mesh.c) ---

#define GUARD_X_MIN  -1024.0f
#define GUARD_X_MAX   1344.0f
#define GUARD_Y_MIN  -1024.0f
#define GUARD_Y_MAX   1264.0f

// --- Lifecycle ---

void particle_init(void) {
    memset(pool, 0, sizeof(pool));
    memset(emitters, 0, sizeof(emitters));
    pool_allocated = 0;
    initialized = true;
    srand(TICKS_READ());
}

void particle_cleanup(void) {
    memset(pool, 0, sizeof(pool));
    memset(emitters, 0, sizeof(emitters));
    pool_allocated = 0;
    initialized = false;
}

// --- Emitter management ---

int particle_emitter_create(const ParticleEmitterDef *def, vec3_t position,
                            int pool_size) {
    if (!initialized || !def) return -1;
    if (pool_allocated + pool_size > PARTICLE_MAX_POOL) return -1;

    // Find free emitter slot
    int handle = -1;
    for (int i = 0; i < PARTICLE_MAX_EMITTERS; i++) {
        if (emitters[i].def == NULL) {
            handle = i;
            break;
        }
    }
    if (handle < 0) return -1;

    ParticleEmitter *em = &emitters[handle];
    em->def = def;
    em->position = position;
    em->active = false;
    em->pool_start = pool_allocated;
    em->pool_count = pool_size;
    em->spawn_accum = 0.0f;

    // Mark all particles in slice as dead
    for (int i = em->pool_start; i < em->pool_start + em->pool_count; i++) {
        pool[i].alive = false;
    }

    pool_allocated += pool_size;
    return handle;
}

void particle_emitter_destroy(int handle) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    ParticleEmitter *em = &emitters[handle];

    // Kill all particles in this emitter's slice
    for (int i = em->pool_start; i < em->pool_start + em->pool_count; i++) {
        pool[i].alive = false;
    }

    memset(em, 0, sizeof(ParticleEmitter));
    em->def = NULL;

    // Compact pool: reclaim freed space at the tail.
    // Scan active emitters to find the highest pool endpoint.
    int max_end = 0;
    for (int i = 0; i < PARTICLE_MAX_EMITTERS; i++) {
        if (emitters[i].def != NULL) {
            int end = emitters[i].pool_start + emitters[i].pool_count;
            if (end > max_end) max_end = end;
        }
    }
    pool_allocated = max_end;
}

void particle_emitter_set_position(int handle, vec3_t position) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    if (emitters[handle].def == NULL) return;
    emitters[handle].position = position;
}

void particle_emitter_set_active(int handle, bool active) {
    if (handle < 0 || handle >= PARTICLE_MAX_EMITTERS) return;
    if (emitters[handle].def == NULL) return;
    emitters[handle].active = active;
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

    // Random lifetime
    p->lifetime = rand_range(def->lifetime_min, def->lifetime_max);
    p->max_lifetime = p->lifetime;

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
    ParticleEmitter *em = &emitters[handle];
    if (em->def == NULL) return;

    int spawned = 0;
    int target = em->def->burst_count;

    for (int i = em->pool_start;
         i < em->pool_start + em->pool_count && spawned < target;
         i++) {
        if (!pool[i].alive) {
            spawn_particle(&pool[i], em);
            spawned++;
        }
    }
}

// --- Update ---

void particle_update(float dt) {
    if (!initialized || dt <= 0.0f) return;

    // Continuous emitters: spawn over time
    for (int e = 0; e < PARTICLE_MAX_EMITTERS; e++) {
        ParticleEmitter *em = &emitters[e];
        if (em->def == NULL || !em->active) continue;
        if (em->def->spawn_rate <= 0.0f) continue;

        em->spawn_accum += em->def->spawn_rate * dt;
        while (em->spawn_accum >= 1.0f) {
            // Find a dead particle in this emitter's slice
            bool found = false;
            for (int i = em->pool_start;
                 i < em->pool_start + em->pool_count; i++) {
                if (!pool[i].alive) {
                    spawn_particle(&pool[i], em);
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

    // Update all alive particles
    for (int i = 0; i < pool_allocated; i++) {
        Particle *p = &pool[i];
        if (!p->alive) continue;

        // Decrement lifetime
        p->lifetime -= dt;
        if (p->lifetime <= 0.0f) {
            p->alive = false;
            continue;
        }

        // Find the owning emitter's def for gravity/drag/color/scale
        // (linear search over emitters — at most 8, cheap)
        const ParticleEmitterDef *def = NULL;
        for (int e = 0; e < PARTICLE_MAX_EMITTERS; e++) {
            const ParticleEmitter *em = &emitters[e];
            if (em->def != NULL &&
                i >= em->pool_start &&
                i < em->pool_start + em->pool_count) {
                def = em->def;
                break;
            }
        }
        if (!def) { p->alive = false; continue; }

        // Apply gravity
        p->velocity.x += def->gravity.x * dt;
        p->velocity.y += def->gravity.y * dt;
        p->velocity.z += def->gravity.z * dt;

        // Apply drag
        if (def->drag > 0.0f) {
            float damp = 1.0f - def->drag * dt;
            if (damp < 0.0f) damp = 0.0f;
            p->velocity.x *= damp;
            p->velocity.y *= damp;
            p->velocity.z *= damp;
        }

        // Integrate position
        p->position.x += p->velocity.x * dt;
        p->position.y += p->velocity.y * dt;
        p->position.z += p->velocity.z * dt;

        // Interpolation factor: 0 at birth, 1 at death
        float t = 1.0f - p->lifetime / p->max_lifetime;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        // Interpolate color
        for (int c = 0; c < 4; c++) {
            float start = (float)def->color_start[c];
            float end   = (float)def->color_end[c];
            float val   = start + (end - start) * t;
            if (val < 0.0f) val = 0.0f;
            if (val > 255.0f) val = 255.0f;
            p->color[c] = (uint8_t)val;
        }

        // Interpolate scale
        p->scale = def->scale_start + (def->scale_end - def->scale_start) * t;
    }
}

// --- Renderer ---

void particle_draw(const Camera *cam) {
    if (!initialized) return;

    // Count alive particles to early-out
    int alive = 0;
    for (int i = 0; i < pool_allocated; i++) {
        if (pool[i].alive) alive++;
    }
    if (alive == 0) return;

    bool use_fog = atmosphere_get_fog_enabled();

    // Compute camera basis vectors (billboard-facing, same as billboard.c spherical)
    vec3_t world_up = {0.0f, 1.0f, 0.0f};
    vec3_t neg_view = vec3_negate(&cam->view_dir);
    vec3_t cam_right = vec3_cross(&world_up, &neg_view);
    float right_len = vec3_length(&cam_right);

    if (right_len < 0.001f) {
        // Camera looking straight up/down — fallback
        cam_right = (vec3_t){1.0f, 0.0f, 0.0f};
    } else {
        float inv = 1.0f / right_len;
        cam_right.x *= inv;
        cam_right.y *= inv;
        cam_right.z *= inv;
    }

    vec3_t cam_up = vec3_cross(&neg_view, &cam_right);
    float up_len = vec3_length(&cam_up);
    if (up_len > 0.001f) {
        float inv = 1.0f / up_len;
        cam_up.x *= inv;
        cam_up.y *= inv;
        cam_up.z *= inv;
    }

    // Set RDP mode ONCE for all particles (additive blend)
    // Z-read ON, Z-write OFF (same as shadows)
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_zbuf(true, false);
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);

    int tri_count = 0;
    uint8_t last_r = 0, last_g = 0, last_b = 0, last_a = 0;
    bool color_set = false;

    for (int i = 0; i < pool_allocated; i++) {
        const Particle *p = &pool[i];
        if (!p->alive) continue;

        // Skip fully transparent particles
        if (p->color[3] == 0) continue;

        // Frustum cull: quick sphere test
        if (!camera_sphere_visible(cam, &p->position, p->scale * 2.0f))
            continue;

        // Apply fog dimming: distant particles fade to black (correct for additive blend)
        uint8_t pr = p->color[0], pg = p->color[1];
        uint8_t pb = p->color[2], pa = p->color[3];
        if (use_fog) {
            vec4_t center_clip;
            mat4_mul_vec3(&center_clip, &cam->vp, &p->position);
            float fog_t = fog_calculate_factor(center_clip.w);
            float dim = 1.0f - fog_t;
            pr = (uint8_t)(pr * dim);
            pg = (uint8_t)(pg * dim);
            pb = (uint8_t)(pb * dim);
            pa = (uint8_t)(pa * dim);
            if (pa == 0 && pr == 0) continue;
        }

        // Only change prim color when it differs
        if (!color_set ||
            pr != last_r || pg != last_g ||
            pb != last_b || pa != last_a) {
            rdpq_set_prim_color(RGBA32(pr, pg, pb, pa));
            last_r = pr;
            last_g = pg;
            last_b = pb;
            last_a = pa;
            color_set = true;
        }

        // Compute 4 quad corners in world space
        //   TL = pos + (-right + up) * scale
        //   TR = pos + ( right + up) * scale
        //   BL = pos + (-right - up) * scale
        //   BR = pos + ( right - up) * scale
        float s = p->scale;
        float corners[4][3];

        corners[0][0] = p->position.x + (-cam_right.x + cam_up.x) * s;
        corners[0][1] = p->position.y + (-cam_right.y + cam_up.y) * s;
        corners[0][2] = p->position.z + (-cam_right.z + cam_up.z) * s;

        corners[1][0] = p->position.x + ( cam_right.x + cam_up.x) * s;
        corners[1][1] = p->position.y + ( cam_right.y + cam_up.y) * s;
        corners[1][2] = p->position.z + ( cam_right.z + cam_up.z) * s;

        corners[2][0] = p->position.x + (-cam_right.x - cam_up.x) * s;
        corners[2][1] = p->position.y + (-cam_right.y - cam_up.y) * s;
        corners[2][2] = p->position.z + (-cam_right.z - cam_up.z) * s;

        corners[3][0] = p->position.x + ( cam_right.x - cam_up.x) * s;
        corners[3][1] = p->position.y + ( cam_right.y - cam_up.y) * s;
        corners[3][2] = p->position.z + ( cam_right.z - cam_up.z) * s;

        // Transform each corner through VP → screen space
        float screen[4][3];  // {X, Y, Z}
        bool reject = false;

        for (int v = 0; v < 4; v++) {
            vec3_t wpos = {corners[v][0], corners[v][1], corners[v][2]};
            vec4_t clip;
            mat4_mul_vec3(&clip, &cam->vp, &wpos);

            // Near-plane rejection
            if (clip.w < 1.0f) { reject = true; break; }

            float inv_w = 1.0f / clip.w;
            float ndc_x = clip.x * inv_w;
            float ndc_y = clip.y * inv_w;
            float ndc_z = clip.z * inv_w;

            screen[v][0] = (ndc_x * 0.5f + 0.5f) * 320.0f;
            screen[v][1] = (1.0f - (ndc_y * 0.5f + 0.5f)) * 240.0f;

            // Guard band check
            if (screen[v][0] < GUARD_X_MIN || screen[v][0] > GUARD_X_MAX ||
                screen[v][1] < GUARD_Y_MIN || screen[v][1] > GUARD_Y_MAX) {
                reject = true; break;
            }

            // Depth clamp
            float depth = ndc_z * 0.5f + 0.5f;
            if (depth < 0.0f) depth = 0.0f;
            if (depth > 1.0f) depth = 1.0f;
            screen[v][2] = depth;
        }

        if (reject) continue;

        // Emit 2 triangles: (TL, TR, BR) and (TL, BR, BL)
        rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[1], screen[3]);
        rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[3], screen[2]);
        tri_count += 2;
    }

    texture_stats_add_triangles(tri_count);
}

// --- Stats ---

int particle_alive_count(void) {
    int count = 0;
    for (int i = 0; i < pool_allocated; i++) {
        if (pool[i].alive) count++;
    }
    return count;
}
