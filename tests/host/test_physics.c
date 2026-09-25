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

// ROADMAP_v2 S11 (D16): the demo's ball at the frame rates it runs at. The
// demo's layout: the ground box (top y = -100) and the platform (15 units
// thick, top y = -85) under the spawn point, the ball relaunched upward.
typedef struct {
    float rest_time;        // seconds until it rests for good (-1: never)
    float lowest_bottom;    // lowest y of the ball's bottom seen after an update
    float final_y;
    int   flag_flips;       // grounded changes after the ball came to rest
} BallRun;

static BallRun run_demo_ball(float dt, float seconds, float spawn_y) {
    CollisionWorld cw;
    collision_world_init(&cw);
    collision_add_aabb(&cw, (vec3_t){-1500, -180, -1500}, (vec3_t){1500, -100, 1500},
                       COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
                       COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
    collision_add_aabb(&cw, (vec3_t){-160, -100, -360}, (vec3_t){160, -85, -240},
                       COLLISION_LAYER_ENV, COLLISION_LAYER_ENV, NULL);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    int h = physics_body_add(&pw, &PHYSICS_DEF_BALL, (vec3_t){0, spawn_y, -300});
    PhysicsBody *b = physics_body_get(&pw, h);
    physics_body_apply_impulse(b, (vec3_t){0, 400.0f, 0});       // BALL_RELAUNCH_VY

    const float r = PHYSICS_DEF_BALL.radius, rest_y = -85.0f + r;
    BallRun run = { .rest_time = -1.0f, .lowest_bottom = 1e9f };
    int n = (int)(seconds / dt + 0.5f);
    bool was_grounded = false;
    bool rested = false;                                      // came to rest at least once
    for (int i = 1; i <= n; i++) {
        physics_world_update(&pw, dt);
        if (b->position.y - r < run.lowest_bottom) run.lowest_bottom = b->position.y - r;
        bool resting = b->grounded && fabsf(b->position.y - rest_y) < 0.5f;
        if (rested && b->grounded != was_grounded) run.flag_flips++;
        if (!resting) run.rest_time = -1.0f;                  // not at rest (yet, or again)
        else if (run.rest_time < 0.0f) run.rest_time = i * dt;
        rested |= resting;
        was_grounded = b->grounded;
    }
    run.final_y = b->position.y;
    return run;
}

static void test_ball_frame_rates(void) {
    // 60 and 30 FPS, and the NTSC display's real frame times
    static const float dts[] = {1.0f / 60.0f, 1.0f / 30.0f, 1.0f / 59.826f, 2.0f / 59.826f};
    float rest[4];
    for (int k = 0; k < 4; k++) {
        BallRun run = run_demo_ball(dts[k], 8.0f, 50.0f);
        CHECK(run.lowest_bottom >= -85.0f - 0.01f);           // never inside the platform
        CHECK(run.rest_time > 0.0f && run.rest_time < 5.0f);  // at rest within 5 s
        CHECK_NEAR(run.final_y, -85.0f + PHYSICS_DEF_BALL.radius, 0.5);
        CHECK(run.flag_flips == 0);                           // grounded stays set at rest
        rest[k] = run.rest_time;
    }
    // Fixed 1/60 s steps: the frame rate changes only when the result is seen
    CHECK(fabsf(rest[1] - rest[0]) <= 1.0f / 30.0f + 1.0f / 60.0f);
    CHECK(fabsf(rest[3] - rest[2]) <= 2.0f / 59.826f + 1.0f / 60.0f);
}

// Dropped from high enough to reach its terminal speed (~820 units/s, ~14
// units per step, under the ball's radius), onto the 15-unit platform
static void test_ball_no_tunnelling(void) {
    static const float dts[] = {1.0f / 60.0f, 1.0f / 30.0f};
    for (int k = 0; k < 2; k++) {
        BallRun run = run_demo_ball(dts[k], 10.0f, 3000.0f);
        CHECK(run.lowest_bottom >= -85.0f - 0.01f);
        CHECK_NEAR(run.final_y, -85.0f + PHYSICS_DEF_BALL.radius, 0.5);
    }
}

// Simulation time follows the display's time: 10 s of NTSC frames (59.826 Hz)
// run 10 s of 1/60 s steps, one more than 598 frames
static void test_steps_follow_time(void) {
    CollisionWorld cw;
    collision_world_init(&cw);
    PhysicsWorld pw;
    physics_world_init(&pw, &cw);
    physics_body_add(&pw, &DEF_FREEFALL, (vec3_t){0, 1000, 0});
    int steps = 0;
    for (int i = 0; i < 598; i++) {                      // 598 / 59.826 Hz = 9.9957 s
        memset(&g_stats_cur, 0, sizeof(g_stats_cur));
        physics_world_update(&pw, 1.0f / 59.826f);
        steps += g_stats_cur.physics_steps;
    }
    CHECK(steps >= 599 && steps <= 600);                 // 9.9957 s x 60 = 599.7
}

void run_physics_tests(void) {
    RUN_TEST(test_free_fall);
    RUN_TEST(test_bounce_and_rest);
    RUN_TEST(test_max_steps_clamp);
    RUN_TEST(test_kinematic);
    RUN_TEST(test_ball_frame_rates);
    RUN_TEST(test_ball_no_tunnelling);
    RUN_TEST(test_steps_follow_time);
}
