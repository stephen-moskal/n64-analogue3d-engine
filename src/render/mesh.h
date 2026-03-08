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
void mesh_compute_bounds(Mesh *mesh);

// --- Rendering ---

void mesh_draw(const Mesh *mesh, const mat4_t *model,
               const Camera *cam, const LightConfig *light);

#endif
