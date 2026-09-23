#include "test.h"
#include "render/particle_internal.h"

// Particle simulation (particle.c): each emitter's slice of the pool is
// updated with that emitter's definition (D14), lifetimes expire, bursts and
// continuous spawning respect the slice, destroy compacts the pool.

static const ParticleEmitterDef def_fall = {
    .burst_count = 4, .lifetime_min = 1.0f, .lifetime_max = 1.0f,
    .gravity = {0, -100, 0},
    .color_start = {255, 0, 0, 255}, .color_end = {0, 0, 255, 0},
    .scale_start = 10.0f, .scale_end = 20.0f,
    .spawn_shape = PARTICLE_SPAWN_POINT,
};

static const ParticleEmitterDef def_rise = {
    .burst_count = 2, .lifetime_min = 0.5f, .lifetime_max = 0.5f,
    .gravity = {0, 100, 0},
    .color_start = {255, 255, 255, 255}, .color_end = {255, 255, 255, 255},
    .scale_start = 10.0f, .scale_end = 20.0f,
    .spawn_shape = PARTICLE_SPAWN_POINT,
};

static const ParticleEmitterDef def_stream = {
    .spawn_rate = 10.0f, .lifetime_min = 5.0f, .lifetime_max = 5.0f,
    .scale_start = 1.0f, .scale_end = 1.0f,
    .spawn_shape = PARTICLE_SPAWN_POINT,
};

static void test_particle_emitters(void) {
    particle_init();
    int a = particle_emitter_create(&def_fall, (vec3_t){0, 0, 0}, 8);
    int b = particle_emitter_create(&def_rise, (vec3_t){100, 0, 0}, 4);
    CHECK(a == 0 && b == 1);
    CHECK(particle_pool_allocated == 12);
    particle_emitter_burst(a);
    particle_emitter_burst(b);
    CHECK(particle_alive_count() == 6);

    particle_update(0.25f);
    const Particle *pa = &particle_pool[0];        // emitter a's slice: 0..7
    const Particle *pb = &particle_pool[8];        // emitter b's slice: 8..11
    CHECK_NEAR(pa->velocity.y, -25.0f, 1e-4);      // own gravity, not the other emitter's
    CHECK_NEAR(pb->velocity.y, 25.0f, 1e-4);
    CHECK_NEAR(pa->position.y, -6.25f, 1e-4);      // velocity first, then position
    CHECK_NEAR(pa->scale, 12.5f, 1e-3);            // a quarter through its life
    CHECK(pa->color[0] == 191 && pa->color[2] == 63);
    CHECK_NEAR(pb->scale, 15.0f, 1e-3);            // half through (0.5 s lifetime)

    particle_update(0.3f);                         // b's particles expire, a's live on
    CHECK(particle_alive_count() == 4);

    particle_emitter_destroy(b);                   // top of the pool: allocation shrinks
    CHECK(particle_pool_allocated == 8);
    particle_emitter_destroy(a);
    CHECK(particle_pool_allocated == 0 && particle_alive_count() == 0);
    particle_cleanup();
}

static void test_particle_continuous(void) {
    particle_init();
    int e = particle_emitter_create(&def_stream, (vec3_t){0, 0, 0}, 2);
    particle_emitter_set_active(e, true);
    particle_update(0.35f);                        // 3.5 due, the slice holds 2
    CHECK(particle_alive_count() == 2);
    CHECK(particle_emitters[e].spawn_accum == 0.0f);   // backlog dropped when full
    particle_cleanup();
}

void run_particle_tests(void) {
    RUN_TEST(test_particle_emitters);
    RUN_TEST(test_particle_continuous);
}
