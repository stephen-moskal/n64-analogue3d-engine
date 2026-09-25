#ifndef MESH_H
#define MESH_H

#include <libdragon.h>
#include <stdbool.h>
#include <math.h>
#include "../math/vec3.h"
#include "camera.h"
#include "lighting.h"

// --- Limits ---

#define MESH_MAX_VERTICES    512    // ~170 triangles max per mesh
#define MESH_MAX_INDICES     1024   // Up to 341 triangles
#define MESH_MAX_MATERIALS   8      // Unique material slots per mesh
#define MESH_MAX_GROUPS      16     // Face groups per mesh

// --- Material ---

typedef enum {
    MATERIAL_TEXTURED,          // Texture * flat color (RDPQ_COMBINER_TEX_FLAT)
    MATERIAL_FLAT_COLOR,        // Flat color only (RDPQ_COMBINER_FLAT)
} MaterialType;

typedef struct {
    MaterialType type;
    int texture_slot;           // Index into texture system (-1 = no texture)
    uint8_t base_color[3];      // RGB base color (modulated by lighting)
    bool alpha_cutout;          // Discard pixels with alpha=0 (for sprites)
} Material;

// --- Vertex ---

typedef struct {
    float position[3];          // Local-space XYZ
    float normal[3];            // Local-space normal (lighting + backface cull)
    float uv[2];                // Texture coordinates (S, T)
} MeshVertex;

// --- Face Group ---

typedef struct {
    int material_index;         // Index into Mesh.materials[]
    int index_start;            // First index in Mesh.indices[]
    int index_count;            // Number of indices (must be multiple of 3)

    // Filled by mesh_compute_bounds() (mesh_analyze_group)
    float center[3];            // Average vertex position (local space)
    float normal[3];            // Shared normal if planar, else normalised average
    bool  planar;               // One normal, one plane: cull/light the group once
} MeshFaceGroup;

// --- Mesh ---

typedef struct {
    // Geometry (heap-allocated). While a mesh is built the two arrays grow
    // as needed; mesh_finalize() moves them into one exact-size block
    // (vertices, then indices) owned by `block` (roadmap D8).
    MeshVertex *vertices;
    uint16_t *indices;
    int vertex_count;
    int index_count;
    int vertex_capacity;        // allocated entries while building
    int index_capacity;
    void *block;                // the finalized geometry allocation (NULL while building)
    bool finalized;             // no vertices or triangles can be added any more

    // Materials & face groups
    Material materials[MESH_MAX_MATERIALS];
    int material_count;
    MeshFaceGroup groups[MESH_MAX_GROUPS];
    int group_count;

    // Bounding volume (local-space, for frustum culling)
    vec3_t bound_center;
    float bound_radius;

    // Flags
    bool backface_cull;         // Enable CPU backface culling (default: true)
} Mesh;

// --- Lifecycle ---

void mesh_init(Mesh *mesh);
void mesh_cleanup(Mesh *mesh);

// --- Building ---
// Usage: mesh_init → add materials → begin_group → add vertices/triangles →
//        end_group → (repeat) → mesh_finalize

int  mesh_add_material(Mesh *mesh, Material mat);
int  mesh_add_vertex(Mesh *mesh, MeshVertex vert);
void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2);
int  mesh_begin_group(Mesh *mesh, int material_index);
void mesh_end_group(Mesh *mesh);
void mesh_compute_bounds(Mesh *mesh);   // also analyses every face group
void mesh_analyze_group(const Mesh *mesh, MeshFaceGroup *group);

// Last build step: computes bounds and face-group data (mesh_compute_bounds)
// and moves the geometry into one exact-size allocation, vertices then
// indices, placed at a fixed D-cache colour (engine/hot.h, D26). Adding
// vertices or triangles afterwards fails.
void mesh_finalize(Mesh *mesh);

// Start packing geometry blocks from the window's first colour again. The
// scene system calls it at every scene init, so a scene's meshes take the
// same colours after every boot and reset.
void mesh_placement_reset(void);

// World-space bounding sphere of a mesh under a model matrix: the centre is
// transformed, the radius scaled by the largest axis scale (squared column
// lengths compared, one sqrtf). For frustum culling.
static inline void mesh_world_bounds(const Mesh *mesh, const mat4_t *model,
                                     vec3_t *center, float *radius) {
    vec4_t c;
    mat4_mul_vec3(&c, model, &mesh->bound_center);
    *center = (vec3_t){c.x, c.y, c.z};
    float sx = model->m[0][0] * model->m[0][0] + model->m[0][1] * model->m[0][1] +
               model->m[0][2] * model->m[0][2];
    float sy = model->m[1][0] * model->m[1][0] + model->m[1][1] * model->m[1][1] +
               model->m[1][2] * model->m[1][2];
    float sz = model->m[2][0] * model->m[2][0] + model->m[2][1] * model->m[2][1] +
               model->m[2][2] * model->m[2][2];
    float m = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
    *radius = mesh->bound_radius * sqrtf(m);
}

// Normal matrix of a model matrix: its cofactor matrix (inverse transpose
// times the determinant), so non-uniform scale keeps normals perpendicular to
// their faces. cof[k] = column_{k+1} x column_{k+2}; a local normal n maps to
// out[r] = cof[0][r]*n[0] + cof[1][r]*n[1] + cof[2][r]*n[2] (not unit length).
static inline void mesh_normal_matrix(const mat4_t *m, float cof[3][3]) {
    for (int k = 0; k < 3; k++) {
        const float *a = m->m[(k + 1) % 3];
        const float *b = m->m[(k + 2) % 3];
        cof[k][0] = a[1] * b[2] - a[2] * b[1];
        cof[k][1] = a[2] * b[0] - a[0] * b[2];
        cof[k][2] = a[0] * b[1] - a[1] * b[0];
    }
}

// Twice the signed area of a screen-space triangle (x right, y down).
// Front faces are wound counter-clockwise seen from outside the mesh, which
// after the viewport's Y flip gives a NEGATIVE area; >= 0 is a back face or
// degenerate. Used by mesh_draw's per-triangle cull.
static inline float mesh_screen_area2(const float a[2], const float b[2],
                                      const float c[2]) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
}

// --- Rendering ---

void mesh_draw(const Mesh *mesh, const mat4_t *model,
               const Camera *cam, const LightConfig *light);

// Measurement (Bench = Mesh, ROADMAP_v2 Phase 3 S1): mesh_draw transforms,
// culls and lights as usual but does not submit the triangles, so the
// difference in mesh_tris is the submission's share. Debug builds only (a
// no-op in release).
void mesh_debug_set_skip_submit(bool skip);

#endif
