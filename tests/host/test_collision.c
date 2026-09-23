#include "test.h"
#include "collision/collision.h"
#include "debug/stats.h"

static void test_sphere_sphere(void) {
    ColliderSphere a = {{0, 0, 0}, 10}, b = {{15, 0, 0}, 10}, c = {{25, 0, 0}, 4};
    CollisionResult r;
    CHECK(collision_sphere_sphere(&a, &b, &r));
    CHECK_NEAR(r.depth, 5, 1e-4);
    CHECK(!collision_sphere_sphere(&a, &c, &r));
}

static void test_sphere_aabb(void) {
    ColliderSphere s = {{0, 12, 0}, 5};
    ColliderAABB box = {{-10, -10, -10}, {10, 10, 10}};
    CollisionResult r;
    CHECK(collision_sphere_aabb(&s, &box, &r));
    s.center.y = 20;
    CHECK(!collision_sphere_aabb(&s, &box, &r));
}

static void test_ray_sphere(void) {
    Ray ray = {{-100, 0, 0}, {1, 0, 0}, 1000};
    ColliderSphere s = {{0, 0, 0}, 10};
    CollisionResult r;
    CHECK(collision_ray_sphere(&ray, &s, &r));
    CHECK_NEAR(r.distance, 90, 1e-3);
    ray.max_distance = 50;
    CHECK(!collision_ray_sphere(&ray, &s, &r));
}

static void test_world_layers_and_raycast(void) {
    CollisionWorld w;
    collision_world_init(&w);
    // Two overlapping spheres on the default layer, one overlapping sphere on ENV
    // that only tests against ENV: layer masks decide which pairs report.
    int a = collision_add_sphere(&w, (vec3_t){0, 0, 0}, 10, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    int b = collision_add_sphere(&w, (vec3_t){12, 0, 0}, 10, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    int c = collision_add_sphere(&w, (vec3_t){-12, 0, 0}, 10, COLLISION_LAYER_ENV, COLLISION_LAYER_ENV, NULL);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(collision_test_all(&w) == 1);

    // Raycast returns the nearest hit and counts in the frame stats
    uint32_t before = g_stats_cur.raycasts;
    Ray ray = {{100, 0, 0}, {-1, 0, 0}, 1000};
    CollisionResult r;
    CHECK(collision_raycast(&w, &ray, COLLISION_LAYER_ALL, &r));
    CHECK_NEAR(r.distance, 100 - 22, 1e-3);   // sphere b spans x = 2..22
    CHECK(g_stats_cur.raycasts == before + 1);

    // Masked raycast only sees ENV
    CHECK(collision_raycast(&w, &ray, COLLISION_LAYER_ENV, &r));
    CHECK_NEAR(r.distance, 100 - (-2), 1e-3); // sphere c spans x = -22..-2
}

// Scans are bounded by the highest active slot (D14): the bound must follow
// removals at the top and ignore holes below it.
static void test_sparse_slots(void) {
    CollisionWorld w;
    collision_world_init(&w);
    CHECK(w.high == 0);
    int h[6];
    for (int i = 0; i < 6; i++) {
        h[i] = collision_add_sphere(&w, (vec3_t){i * 100.0f, 0, 0}, 10,
                                    COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    }
    CHECK(w.high == 6 && w.count == 6);

    // Removing the top two lowers the bound; a hole in the middle does not
    collision_remove(&w, h[5]);
    collision_remove(&w, h[4]);
    collision_remove(&w, h[2]);
    CHECK(w.high == 4 && w.count == 3);

    // A new collider takes the lowest free slot (the hole) and keeps the bound
    int n = collision_add_sphere(&w, (vec3_t){150, 0, 0}, 60,
                                 COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);
    CHECK(n == 2 && w.high == 4);
    CHECK(collision_test_all(&w) == 1);            // the new sphere overlaps slot 1 only

    // Queries still see every collider below the bound
    Ray ray = {{-100, 0, 0}, {1, 0, 0}, 1000};
    CollisionResult r;
    CHECK(collision_raycast(&w, &ray, COLLISION_LAYER_ALL, &r));
    CHECK_NEAR(r.distance, 90, 1e-3);              // sphere 0 spans x = -10..10
    int out[8];
    CHECK(collision_overlap_sphere(&w, (vec3_t){300, 0, 0}, 5, COLLISION_LAYER_ALL, out, 8) == 1);
    CHECK(out[0] == 3);

    // Removing everything empties the bound
    for (int i = 0; i < 4; i++) collision_remove(&w, i);
    CHECK(w.high == 0 && w.count == 0);
    CHECK(collision_test_all(&w) == 0);
}

void run_collision_tests(void) {
    RUN_TEST(test_sparse_slots);
    RUN_TEST(test_sphere_sphere);
    RUN_TEST(test_sphere_aabb);
    RUN_TEST(test_ray_sphere);
    RUN_TEST(test_world_layers_and_raycast);
}
