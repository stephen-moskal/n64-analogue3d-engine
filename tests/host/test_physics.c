#include "test.h"
#include "physics/physics.h"
#include "debug/stats.h"
#include <string.h>

// No air damping, full gravity: should follow y = y0 + 0.5 g t^2
static const PhysicsBodyDef DEF_FREEFALL = {
    .mass = 1.0f, .restitution = 0.5f, .friction = 0.3f,
    .damping = 0.0f, .gravity_scale = 1.0f, .radius = 10.0f,
};

static void test_free_fall(void) {
    CollisionWorld cw;
    collision_world_init(&cw);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    int h = physics_body_add(&pw, &DEF_FREEFALL, (vec3_t){0, 1000, 0});
    CHECK(h >= 0);

    for (int i = 0; i < 30; i++) physics_world_update(&pw, PHYSICS_DT);   // 0.5 s
    float t = 0.5f;
    float expected = 1000.0f + 0.5f * PHYSICS_GRAVITY_DEFAULT * t * t;   // 877.4
    float fall_expected = 1000.0f - expected;
    float fall = 1000.0f - pw.bodies[h].position.y;
    // Semi-implicit Euler at 60 Hz is within a few percent of the analytic fall
    CHECK_NEAR(fall, fall_expected, fall_expected * 0.05);
    CHECK(pw.bodies[h].velocity.y < 0);
}

static void test_bounce_and_rest(void) {
    CollisionWorld cw;
    collision_world_init(&cw);
    collision_add_aabb(&cw, (vec3_t){-500, -20, -500}, (vec3_t){500, 0, 500},
                       COLLISION_LAYER_ENV, COLLISION_LAYER_ENV, NULL);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    int h = physics_body_add(&pw, &PHYSICS_DEF_BALL, (vec3_t){0, 300, 0});
    CHECK(h >= 0);

    bool bounced = false;
    for (int i = 0; i < 60 * 8; i++) {                 // 8 simulated seconds
        physics_world_update(&pw, PHYSICS_DT);
        if (pw.bodies[h].velocity.y > 50.0f) bounced = true;
    }
    const PhysicsBody *b = &pw.bodies[h];
    CHECK(bounced);                                    // restitution 0.7 bounces up
    CHECK(physics_body_is_grounded(b));                // and comes to rest
    CHECK(fabsf(b->velocity.y) < 5.0f);
    // Resting on the floor surface (y = 0) at its radius, never inside it
    CHECK(b->position.y >= -0.5f);
    CHECK(b->position.y <= PHYSICS_DEF_BALL.radius + 2.0f);
}

static void test_max_steps_clamp(void) {
    CollisionWorld cw;
    collision_world_init(&cw);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    physics_body_add(&pw, &DEF_FREEFALL, (vec3_t){0, 1000, 0});
    memset(&g_stats_cur, 0, sizeof(g_stats_cur));
    physics_world_update(&pw, 1.0f);                   // a 1-second stall
    CHECK(g_stats_cur.physics_steps == PHYSICS_MAX_STEPS);
    CHECK(pw.accumulator <= PHYSICS_DT + 1e-6f);       // leftover clamped
}

// A kinematic body is left alone by the simulation (held by game code)
static void test_kinematic(void) {
    CollisionWorld cw;
    collision_world_init(&cw);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    int h = physics_body_add(&pw, &DEF_FREEFALL, (vec3_t){0, 1000, 0});
    CHECK(!pw.bodies[h].kinematic);                    // simulated by default
    pw.bodies[h].kinematic = true;
    for (int i = 0; i < 30; i++) physics_world_update(&pw, PHYSICS_DT);
    CHECK(pw.bodies[h].position.y == 1000.0f && pw.bodies[h].velocity.y == 0.0f);
    pw.bodies[h].kinematic = false;                    // released: falls
    for (int i = 0; i < 30; i++) physics_world_update(&pw, PHYSICS_DT);
    CHECK(pw.bodies[h].position.y < 1000.0f);
}

void run_physics_tests(void) {
    RUN_TEST(test_free_fall);
    RUN_TEST(test_bounce_and_rest);
    RUN_TEST(test_max_steps_clamp);
    RUN_TEST(test_kinematic);
}
