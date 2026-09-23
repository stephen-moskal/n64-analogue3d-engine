#ifndef HOST_TEST_H
#define HOST_TEST_H

/*
 * Tiny assert-style test runner for host unit tests.
 * A failing CHECK prints file:line and the expression, and the test binary
 * exits non-zero at the end (CI fails). Tests keep running after a failure so
 * one run reports everything.
 */

#include <stdio.h>
#include <math.h>

extern int g_checks, g_failures;
extern const char *g_current_test;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; \
        printf("  FAIL %s (%s:%d): %s\n", g_current_test, __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    g_checks++; \
    double _a = (double)(a), _b = (double)(b); \
    if (fabs(_a - _b) > (double)(eps)) { g_failures++; \
        printf("  FAIL %s (%s:%d): %s = %g, expected %g (+/- %g)\n", \
               g_current_test, __FILE__, __LINE__, #a, _a, _b, (double)(eps)); } \
} while (0)

#define RUN_TEST(fn) do { g_current_test = #fn; fn(); } while (0)

// Each test file provides one entry point
void run_vec3_tests(void);
void run_collision_tests(void);
void run_physics_tests(void);
void run_action_tests(void);
void run_camera_tests(void);
void run_frametime_tests(void);
void run_mesh_tests(void);

#endif
