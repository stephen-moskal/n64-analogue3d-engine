#ifndef MESH_H
#define MESH_H

#include <libdragon.h>
#include <stdbool.h>
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
    // Geometry (heap-allocated)
    MeshVertex *vertices;
    uint16_t *indices;
    int vertex_count;
    int index_count;

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
//        end_group → (repeat) → compute_bounds

int  mesh_add_material(Mesh *mesh, Material mat);
int  mesh_add_vertex(Mesh *mesh, MeshVertex vert);
void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2);
int  mesh_begin_group(Mesh *mesh, int material_index);
void mesh_end_group(Mesh *mesh);
void mesh_compute_bounds(Mesh *mesh);   // also analyses every face group
void mesh_analyze_group(const Mesh *mesh, MeshFaceGroup *group);

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

#endif
