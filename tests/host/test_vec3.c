#include "test.h"
#include "math/vec3.h"

static void test_basic_ops(void) {
    vec3_t a = {1, 2, 3}, b = {4, -5, 6};
    vec3_t s = vec3_add(&a, &b);
    CHECK_NEAR(s.x, 5, 1e-6); CHECK_NEAR(s.y, -3, 1e-6); CHECK_NEAR(s.z, 9, 1e-6);
    CHECK_NEAR(vec3_dot(&a, &b), 4 - 10 + 18, 1e-6);
    vec3_t c = vec3_cross(&a, &b);
    // Cross product is perpendicular to both inputs
    CHECK_NEAR(vec3_dot(&c, &a), 0, 1e-4);
    CHECK_NEAR(vec3_dot(&c, &b), 0, 1e-4);
}

static void test_normalize(void) {
    vec3_t v = {3, 4, 12};
    CHECK_NEAR(vec3_length(&v), 13, 1e-5);
    vec3_t n = vec3_normalize(&v);
    CHECK_NEAR(vec3_length(&n), 1, 1e-5);
    CHECK_NEAR(n.z, 12.0 / 13.0, 1e-5);
}

static void test_reflect(void) {
    vec3_t v = {1, -1, 0};
    vec3_t up = {0, 1, 0};
    vec3_t r = vec3_reflect(&v, &up);
    CHECK_NEAR(r.x, 1, 1e-6);
    CHECK_NEAR(r.y, 1, 1e-6);
    CHECK_NEAR(r.z, 0, 1e-6);
}

void run_vec3_tests(void) {
    RUN_TEST(test_basic_ops);
    RUN_TEST(test_normalize);
    RUN_TEST(test_reflect);
}
