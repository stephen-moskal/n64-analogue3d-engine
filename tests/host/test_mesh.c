#include "test.h"
#include "render/mesh_defs.h"
#include "engine/hot.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

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

// D8: build arrays grow as needed and mesh_finalize() packs them into one
// exact-size, 16-byte aligned block without changing the geometry.
static void test_mesh_finalize(void) {
    Mesh m;
    mesh_init(&m);
    int mat = mesh_add_material(&m, (Material){.type = MATERIAL_FLAT_COLOR, .texture_slot = -1});
    mesh_begin_group(&m, mat);
    // 20 vertices on a fan in the XZ plane, 19 triangles: past the initial
    // 16 vertices / 48 indices, so both arrays grow once
    for (int i = 0; i < 20; i++) {
        float a = i * 0.3f;
        mesh_add_vertex(&m, (MeshVertex){
            .position = {cosf(a), 0, sinf(a)}, .normal = {0, 1, 0}, .uv = {(float)i, 0},
        });
    }
    for (int i = 1; i < 20; i++) mesh_add_triangle(&m, 0, (uint16_t)(i < 19 ? i + 1 : 1), (uint16_t)i);
    mesh_end_group(&m);
    CHECK(m.vertex_count == 20 && m.index_count == 57);
    CHECK(m.vertex_capacity >= 20 && m.index_capacity >= 57);
    CHECK(!m.finalized && m.block == NULL);

    MeshVertex before[20];
    uint16_t before_idx[57];
    for (int i = 0; i < 20; i++) before[i] = m.vertices[i];
    for (int i = 0; i < 57; i++) before_idx[i] = m.indices[i];

    mesh_finalize(&m);
    CHECK(m.finalized && m.block != NULL);
    CHECK(m.vertex_capacity == 20 && m.index_capacity == 57);
    CHECK(((uintptr_t)m.vertices & 15) == 0);
    CHECK((char *)m.indices == (char *)m.vertices + 20 * sizeof(MeshVertex));
    int same = 1;
    for (int i = 0; i < 20; i++) same &= memcmp(&before[i], &m.vertices[i], sizeof(MeshVertex)) == 0;
    for (int i = 0; i < 57; i++) same &= (before_idx[i] == m.indices[i]);
    CHECK(same);
    CHECK(m.bound_radius > 0.9f && m.groups[0].planar);   // bounds + group analysis ran

    mesh_cleanup(&m);
    CHECK(m.block == NULL && m.vertices == NULL && m.vertex_count == 0 && !m.finalized);

    // The built-in shapes are finalized to their exact size
    mesh_defs_init();
    const Mesh *shapes[] = {mesh_defs_get_pillar(), mesh_defs_get_platform(),
                            mesh_defs_get_pyramid(), mesh_defs_get_sphere()};
    for (int i = 0; i < 4; i++) {
        CHECK(shapes[i]->finalized && shapes[i]->block != NULL);
        CHECK(shapes[i]->vertex_capacity == shapes[i]->vertex_count);
    }
    mesh_defs_cleanup();
}

// Geometry blocks at fixed D-cache colours (S9.1, D26): packed in the window
// one after another, restarting after mesh_placement_reset()
static uint32_t colour_of(const void *p) { return (uint32_t)((uintptr_t)p & (ENGINE_DCACHE_BYTES - 1)); }

static void test_mesh_placement(void) {
    mesh_placement_reset();
    mesh_defs_init();
    const Mesh *shapes[] = {mesh_defs_get_pillar(), mesh_defs_get_platform(),
                            mesh_defs_get_pyramid(), mesh_defs_get_sphere()};
    uint32_t prev_end = 0;
    for (int i = 0; i < 4; i++) {
        const Mesh *s = shapes[i];
        size_t bytes = s->vertex_count * sizeof(MeshVertex) + s->index_count * sizeof(uint16_t);
        uint32_t c = colour_of(s->vertices);
        CHECK(c >= ENGINE_GEOMETRY_COLOUR_LO);
        if (bytes <= ENGINE_GEOMETRY_COLOUR_HI - ENGINE_GEOMETRY_COLOUR_LO) {
            CHECK(c + bytes <= ENGINE_GEOMETRY_COLOUR_HI);          // inside the window
            if (i > 0 && prev_end + bytes <= ENGINE_GEOMETRY_COLOUR_HI)
                CHECK(c == prev_end);                             // packed after the last one
            prev_end = (uint32_t)(c + ((bytes + 15) & ~(size_t)15));
        } else {
            CHECK(c == ENGINE_GEOMETRY_COLOUR_LO);                // too big: from the start
        }
    }
    uint32_t pillar = colour_of(mesh_defs_get_pillar()->vertices);
    CHECK(pillar == ENGINE_GEOMETRY_COLOUR_LO);                   // the first mesh built
    mesh_defs_cleanup();

    // A scene reset rebuilds its meshes at the same colours
    mesh_placement_reset();
    mesh_defs_init();
    CHECK(colour_of(mesh_defs_get_pillar()->vertices) == pillar);
    mesh_defs_cleanup();
}

void run_mesh_tests(void) {
    RUN_TEST(test_mesh_finalize);
    RUN_TEST(test_mesh_placement);
    RUN_TEST(test_mesh_winding);
    RUN_TEST(test_mesh_planar_groups);
    RUN_TEST(test_sphere_visible_all_sides);
}
