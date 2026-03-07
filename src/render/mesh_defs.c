#include "mesh_defs.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- Static mesh storage ---

static Mesh pillar_mesh;
static Mesh platform_mesh;
static Mesh pyramid_mesh;
static bool initialized = false;

// --- Pillar: 8-sided cylinder, height [-1,1], radius 1.0 ---

static void build_pillar(void) {
    mesh_init(&pillar_mesh);
    pillar_mesh.backface_cull = true;

    // Single flat-color material: stone gray
    int mat = mesh_add_material(&pillar_mesh, (Material){
        .type = MATERIAL_FLAT_COLOR,
        .texture_slot = -1,
        .base_color = {180, 160, 140}
    });

    const int SIDES = 8;
    const float RADIUS = 1.0f;
    const float HALF_H = 1.0f;

    // --- Side faces (one group per side for proper per-face lighting) ---
    for (int i = 0; i < SIDES; i++) {
        float a0 = i * (2.0f * M_PI / SIDES);
        float a1 = (i + 1) * (2.0f * M_PI / SIDES);
        float mid = (a0 + a1) * 0.5f;

        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);

        // Face normal: outward at midpoint angle
        float nx = cosf(mid), nz = sinf(mid);

        mesh_begin_group(&pillar_mesh, mat);
        int base = pillar_mesh.vertex_count;

        // 4 vertices: bottom-left, bottom-right, top-right, top-left
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {c0 * RADIUS, -HALF_H, s0 * RADIUS},
            .normal = {nx, 0, nz}, .uv = {0, 0}
        });
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {c1 * RADIUS, -HALF_H, s1 * RADIUS},
            .normal = {nx, 0, nz}, .uv = {0, 0}
        });
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {c1 * RADIUS, HALF_H, s1 * RADIUS},
            .normal = {nx, 0, nz}, .uv = {0, 0}
        });
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {c0 * RADIUS, HALF_H, s0 * RADIUS},
            .normal = {nx, 0, nz}, .uv = {0, 0}
        });

        mesh_add_triangle(&pillar_mesh, base, base + 1, base + 2);
        mesh_add_triangle(&pillar_mesh, base, base + 2, base + 3);
        mesh_end_group(&pillar_mesh);
    }

    // --- Top cap (single group, normal = +Y) ---
    mesh_begin_group(&pillar_mesh, mat);
    int top_center = pillar_mesh.vertex_count;
    mesh_add_vertex(&pillar_mesh, (MeshVertex){
        .position = {0, HALF_H, 0},
        .normal = {0, 1, 0}, .uv = {0, 0}
    });
    for (int i = 0; i < SIDES; i++) {
        float a = i * (2.0f * M_PI / SIDES);
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {cosf(a) * RADIUS, HALF_H, sinf(a) * RADIUS},
            .normal = {0, 1, 0}, .uv = {0, 0}
        });
    }
    for (int i = 0; i < SIDES; i++) {
        int next = (i + 1) % SIDES;
        mesh_add_triangle(&pillar_mesh, top_center, top_center + 1 + i, top_center + 1 + next);
    }
    mesh_end_group(&pillar_mesh);

    // --- Bottom cap (single group, normal = -Y) ---
    mesh_begin_group(&pillar_mesh, mat);
    int bot_center = pillar_mesh.vertex_count;
    mesh_add_vertex(&pillar_mesh, (MeshVertex){
        .position = {0, -HALF_H, 0},
        .normal = {0, -1, 0}, .uv = {0, 0}
    });
    for (int i = 0; i < SIDES; i++) {
        float a = i * (2.0f * M_PI / SIDES);
        mesh_add_vertex(&pillar_mesh, (MeshVertex){
            .position = {cosf(a) * RADIUS, -HALF_H, sinf(a) * RADIUS},
            .normal = {0, -1, 0}, .uv = {0, 0}
        });
    }
    for (int i = 0; i < SIDES; i++) {
        int next = (i + 1) % SIDES;
        // Reversed winding for bottom face (normal points down)
        mesh_add_triangle(&pillar_mesh, bot_center, bot_center + 1 + next, bot_center + 1 + i);
    }
    mesh_end_group(&pillar_mesh);

    mesh_compute_bounds(&pillar_mesh);
}

// --- Platform: rectangular box, size 4.0 x 0.5 x 2.0 ---

static void build_platform(void) {
    mesh_init(&platform_mesh);
    platform_mesh.backface_cull = true;

    int mat = mesh_add_material(&platform_mesh, (Material){
        .type = MATERIAL_FLAT_COLOR,
        .texture_slot = -1,
        .base_color = {140, 100, 60}
    });

    // Half-sizes
    const float HX = 2.0f, HY = 0.25f, HZ = 1.0f;

    // 8 corners of the box
    const float verts[8][3] = {
        {-HX, -HY, -HZ},  // 0: left  bottom back
        { HX, -HY, -HZ},  // 1: right bottom back
        { HX, -HY,  HZ},  // 2: right bottom front
        {-HX, -HY,  HZ},  // 3: left  bottom front
        {-HX,  HY, -HZ},  // 4: left  top    back
        { HX,  HY, -HZ},  // 5: right top    back
        { HX,  HY,  HZ},  // 6: right top    front
        {-HX,  HY,  HZ},  // 7: left  top    front
    };

    // 6 faces: {v0, v1, v2, v3}, normal
    const int faces[6][4] = {
        {3, 2, 6, 7},  // Front  (+Z)
        {1, 0, 4, 5},  // Back   (-Z)
        {7, 6, 5, 4},  // Top    (+Y)
        {0, 1, 2, 3},  // Bottom (-Y)
        {2, 1, 5, 6},  // Right  (+X)
        {0, 3, 7, 4},  // Left   (-X)
    };
    const float normals[6][3] = {
        { 0,  0,  1},  // Front
        { 0,  0, -1},  // Back
        { 0,  1,  0},  // Top
        { 0, -1,  0},  // Bottom
        { 1,  0,  0},  // Right
        {-1,  0,  0},  // Left
    };

    for (int f = 0; f < 6; f++) {
        mesh_begin_group(&platform_mesh, mat);
        int base = platform_mesh.vertex_count;

        for (int v = 0; v < 4; v++) {
            int vi = faces[f][v];
            mesh_add_vertex(&platform_mesh, (MeshVertex){
                .position = {verts[vi][0], verts[vi][1], verts[vi][2]},
                .normal = {normals[f][0], normals[f][1], normals[f][2]},
                .uv = {0, 0}
            });
        }

        mesh_add_triangle(&platform_mesh, base, base + 1, base + 2);
        mesh_add_triangle(&platform_mesh, base, base + 2, base + 3);
        mesh_end_group(&platform_mesh);
    }

    mesh_compute_bounds(&platform_mesh);
}

// --- Pyramid: 4-sided, base [-1,1] XZ, apex at Y=1 ---

static void build_pyramid(void) {
    mesh_init(&pyramid_mesh);
    pyramid_mesh.backface_cull = true;

    int mat = mesh_add_material(&pyramid_mesh, (Material){
        .type = MATERIAL_FLAT_COLOR,
        .texture_slot = -1,
        .base_color = {200, 180, 100}
    });

    // Apex and base corners
    const float apex[3] = {0, 1, 0};
    const float base_v[4][3] = {
        {-1, -1, -1},  // 0: back-left
        { 1, -1, -1},  // 1: back-right
        { 1, -1,  1},  // 2: front-right
        {-1, -1,  1},  // 3: front-left
    };

    // 4 side faces: each is a triangle from apex to two base edges
    const int side_faces[4][2] = {
        {3, 2},  // Front  (Z+)
        {1, 0},  // Back   (Z-)
        {2, 1},  // Right  (X+)
        {0, 3},  // Left   (X-)
    };

    // Compute side normals (cross product of two edges per face)
    for (int f = 0; f < 4; f++) {
        int i0 = side_faces[f][0];
        int i1 = side_faces[f][1];

        // Edge vectors from base[i0] to apex and base[i0] to base[i1]
        float e1[3] = {apex[0] - base_v[i0][0], apex[1] - base_v[i0][1], apex[2] - base_v[i0][2]};
        float e2[3] = {base_v[i1][0] - base_v[i0][0], base_v[i1][1] - base_v[i0][1], base_v[i1][2] - base_v[i0][2]};

        // Normal = cross(e1, e2), then normalize
        float nx = e1[1] * e2[2] - e1[2] * e2[1];
        float ny = e1[2] * e2[0] - e1[0] * e2[2];
        float nz = e1[0] * e2[1] - e1[1] * e2[0];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 0.001f) { nx /= len; ny /= len; nz /= len; }

        mesh_begin_group(&pyramid_mesh, mat);
        int base_idx = pyramid_mesh.vertex_count;

        mesh_add_vertex(&pyramid_mesh, (MeshVertex){
            .position = {base_v[i0][0], base_v[i0][1], base_v[i0][2]},
            .normal = {nx, ny, nz}, .uv = {0, 0}
        });
        mesh_add_vertex(&pyramid_mesh, (MeshVertex){
            .position = {base_v[i1][0], base_v[i1][1], base_v[i1][2]},
            .normal = {nx, ny, nz}, .uv = {0, 0}
        });
        mesh_add_vertex(&pyramid_mesh, (MeshVertex){
            .position = {apex[0], apex[1], apex[2]},
            .normal = {nx, ny, nz}, .uv = {0, 0}
        });

        mesh_add_triangle(&pyramid_mesh, base_idx, base_idx + 1, base_idx + 2);
        mesh_end_group(&pyramid_mesh);
    }

    // Base (2 triangles, normal = -Y)
    mesh_begin_group(&pyramid_mesh, mat);
    int bi = pyramid_mesh.vertex_count;
    for (int v = 0; v < 4; v++) {
        mesh_add_vertex(&pyramid_mesh, (MeshVertex){
            .position = {base_v[v][0], base_v[v][1], base_v[v][2]},
            .normal = {0, -1, 0}, .uv = {0, 0}
        });
    }
    mesh_add_triangle(&pyramid_mesh, bi, bi + 1, bi + 2);
    mesh_add_triangle(&pyramid_mesh, bi, bi + 2, bi + 3);
    mesh_end_group(&pyramid_mesh);

    mesh_compute_bounds(&pyramid_mesh);
}

// --- Public API ---

void mesh_defs_init(void) {
    if (initialized) return;
    build_pillar();
    build_platform();
    build_pyramid();
    initialized = true;
}

void mesh_defs_cleanup(void) {
    if (!initialized) return;
    mesh_cleanup(&pillar_mesh);
    mesh_cleanup(&platform_mesh);
    mesh_cleanup(&pyramid_mesh);
    initialized = false;
}

const Mesh *mesh_defs_get_pillar(void) { return &pillar_mesh; }
const Mesh *mesh_defs_get_platform(void) { return &platform_mesh; }
const Mesh *mesh_defs_get_pyramid(void) { return &pyramid_mesh; }
