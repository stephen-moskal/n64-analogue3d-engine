#include "test.h"
#include "render/mesh_defs.h"
#include <math.h>

// Built-in meshes: consistent winding (the per-triangle cull depends on it)
// and group planarity (decides which cull path mesh_draw takes). D3 / D24.

static void cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Every triangle is counter-clockwise seen from the side its normals point to
static int count_bad_winding(const Mesh *m) {
    int bad = 0;
    for (int i = 0; i < m->index_count; i += 3) {
        const MeshVertex *v[3] = {
            &m->vertices[m->indices[i]], &m->vertices[m->indices[i + 1]],
            &m->vertices[m->indices[i + 2]],
        };
        float e1[3], e2[3], c[3];
        for (int k = 0; k < 3; k++) {
            e1[k] = v[1]->position[k] - v[0]->position[k];
            e2[k] = v[2]->position[k] - v[0]->position[k];
        }
        cross3(e1, e2, c);
        float d = 0;
        for (int k = 0; k < 3; k++) {
            d += c[k] * (v[0]->normal[k] + v[1]->normal[k] + v[2]->normal[k]);
        }
        if (d <= 0) bad++;
    }
    return bad;
}

static int count_planar(const Mesh *m) {
    int n = 0;
    for (int g = 0; g < m->group_count; g++) n += m->groups[g].planar;
    return n;
}

static void test_mesh_winding(void) {
    mesh_defs_init();
    CHECK(count_bad_winding(mesh_defs_get_pillar()) == 0);
    CHECK(count_bad_winding(mesh_defs_get_platform()) == 0);
    CHECK(count_bad_winding(mesh_defs_get_pyramid()) == 0);
    CHECK(count_bad_winding(mesh_defs_get_sphere()) == 0);
}

static void test_mesh_planar_groups(void) {
    const Mesh *flat[] = {mesh_defs_get_pillar(), mesh_defs_get_platform(),
                          mesh_defs_get_pyramid()};
    for (int i = 0; i < 3; i++) CHECK(count_planar(flat[i]) == flat[i]->group_count);

    const Mesh *s = mesh_defs_get_sphere();
    CHECK(s->group_count == 6);
    CHECK(count_planar(s) == 0);
    // A sphere band's average normal is its axis, not any face's normal:
    // the reason curved groups are lit per triangle
    CHECK_NEAR(s->groups[3].normal[1], 1.0f, 1e-3);   // upper-middle band
}

// D24: the demo sphere must be about half visible from every side. The old
// group test used each band's first vertex (+Z) and culled almost the whole
// sphere when the camera was on its -Z side.
static void test_sphere_visible_all_sides(void) {
    const Mesh *s = mesh_defs_get_sphere();
    vec3_t pos = {280, -50, 280}, scale = {50, 50, 50};
    mat4_t model;
    mat4_from_srt(&model, &scale, 0, 0, 0, &pos);

    for (int a = 0; a < 8; a++) {
        float az = a * (float)M_PI / 4.0f;
        vec3_t eye = {pos.x + 300 * sinf(az), pos.y + 150, pos.z + 300 * cosf(az)};
        Camera cam;
        camera_init(&cam, &CAMERA_DEFAULT);
        camera_set_fixed(&cam, eye, pos);
        camera_update(&cam);
        mat4_t mvp;
        mat4_mul(&mvp, &cam.vp, &model);

        int front = 0, disagree = 0;
        for (int i = 0; i < s->index_count; i += 3) {
            float scr[3][2], w[3][3];
            for (int v = 0; v < 3; v++) {
                const MeshVertex *mv = &s->vertices[s->indices[i + v]];
                vec3_t p = {mv->position[0], mv->position[1], mv->position[2]};
                vec4_t clip;
                mat4_mul_vec3(&clip, &mvp, &p);
                scr[v][0] = (clip.x / clip.w * 0.5f + 0.5f) * 320.0f;
                scr[v][1] = (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * 240.0f;
                w[v][0] = pos.x + mv->position[0] * 50.0f;
                w[v][1] = pos.y + mv->position[1] * 50.0f;
                w[v][2] = pos.z + mv->position[2] * 50.0f;
            }
            bool is_front = mesh_screen_area2(scr[0], scr[1], scr[2]) < 0.0f;
            front += is_front;
            // Winding agrees with the exact test: camera on the front side of
            // the triangle's own plane (true face normal, not vertex normals)
            float e1[3], e2[3], fn[3];
            for (int k = 0; k < 3; k++) { e1[k] = w[1][k] - w[0][k]; e2[k] = w[2][k] - w[0][k]; }
            cross3(e1, e2, fn);
            float facing = fn[0] * (eye.x - w[0][0]) + fn[1] * (eye.y - w[0][1]) +
                           fn[2] * (eye.z - w[0][2]);
            if (fabsf(facing) > 1.0f && is_front != (facing > 0)) disagree++;
        }
        CHECK(front >= 24 && front <= 40);
        CHECK(disagree == 0);
    }
    mesh_defs_cleanup();
}

void run_mesh_tests(void) {
    RUN_TEST(test_mesh_winding);
    RUN_TEST(test_mesh_planar_groups);
    RUN_TEST(test_sphere_visible_all_sides);
}
