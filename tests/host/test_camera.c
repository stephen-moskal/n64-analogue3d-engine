#include "test.h"
#include "render/camera.h"

static void test_mat4_identity(void) {
    mat4_t id = {{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    mat4_t s;
    vec3_t scale = {2, 3, 4}, t = {10, 20, 30};
    mat4_from_srt(&s, &scale, 0, 0, 0, &t);
    mat4_t out;
    mat4_mul(&out, &id, &s);
    vec3_t p = {1, 1, 1};
    vec4_t r;
    mat4_mul_vec3(&r, &out, &p);
    CHECK_NEAR(r.x, 12, 1e-4);
    CHECK_NEAR(r.y, 23, 1e-4);
    CHECK_NEAR(r.z, 34, 1e-4);
    CHECK_NEAR(r.w, 1, 1e-6);
}

static void test_frustum(void) {
    Camera cam;
    camera_init(&cam, &CAMERA_DEFAULT);   // orbits the origin at distance 300
    camera_update(&cam);

    vec3_t origin = {0, 0, 0};
    CHECK(camera_sphere_visible(&cam, &origin, 10));

    // Directly behind the camera
    vec3_t behind = {
        cam.position.x * 2.0f, cam.position.y * 2.0f, cam.position.z * 2.0f,
    };
    CHECK(!camera_sphere_visible(&cam, &behind, 10));

    // Beyond the far plane (2000) along the view direction
    vec3_t far = {
        cam.position.x - cam.position.x * 10.0f,
        cam.position.y - cam.position.y * 10.0f,
        cam.position.z - cam.position.z * 10.0f,
    };
    CHECK(!camera_sphere_visible(&cam, &far, 10));

    // Far off to the side
    vec3_t side = {5000, 0, 0};
    CHECK(!camera_sphere_visible(&cam, &side, 10));
}

void run_camera_tests(void) {
    RUN_TEST(test_mat4_identity);
    RUN_TEST(test_frustum);
}
