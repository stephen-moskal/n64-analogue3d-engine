#include "mesh.h"
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include "../engine/hot.h"

// Mesh building and bounds. No rendering dependencies, so this file is also
// compiled into the host unit tests (tests/host).

// Build arrays start at these sizes and double as needed (roadmap D8: every
// mesh used to allocate MESH_MAX_* entries, 18 KB, whatever its size).
#define MESH_INITIAL_VERTICES  16
#define MESH_INITIAL_INDICES   48

// --- Lifecycle ---

void mesh_init(Mesh *mesh) {
    memset(mesh, 0, sizeof(Mesh));
    mesh->vertices = NULL;
    mesh->indices = NULL;
    mesh->block = NULL;
    mesh->backface_cull = true;
}

void mesh_cleanup(Mesh *mesh) {
    if (mesh->block) {
        free(mesh->block);              // finalized: one allocation holds both arrays
    } else {
        free(mesh->vertices);
        free(mesh->indices);
    }
    mesh->block = NULL;
    mesh->vertices = NULL;
    mesh->indices = NULL;
    mesh->vertex_count = mesh->vertex_capacity = 0;
    mesh->index_count = mesh->index_capacity = 0;
    mesh->material_count = 0;
    mesh->group_count = 0;
    mesh->finalized = false;
}

// Grow a build array to hold at least `needed` entries (doubling, capped at
// `max`). Returns false when `needed` exceeds `max` or memory runs out.
static bool grow_array(void **array, int *capacity, int needed, int initial,
                       int max, size_t elem_size) {
    if (needed <= *capacity) return true;
    if (needed > max) return false;
    int cap = *capacity > 0 ? *capacity : initial;
    while (cap < needed) cap *= 2;
    if (cap > max) cap = max;
    void *p = realloc(*array, (size_t)cap * elem_size);
    if (!p) return false;
    *array = p;
    *capacity = cap;
    return true;
}

// --- Building ---

int mesh_add_material(Mesh *mesh, Material mat) {
    if (mesh->material_count >= MESH_MAX_MATERIALS) return -1;
    int idx = mesh->material_count++;
    mesh->materials[idx] = mat;
    return idx;
}

int mesh_add_vertex(Mesh *mesh, MeshVertex vert) {
    assertf(!mesh->finalized, "mesh_add_vertex() after mesh_finalize()");
    if (mesh->finalized) return -1;
    if (!grow_array((void **)&mesh->vertices, &mesh->vertex_capacity,
                    mesh->vertex_count + 1, MESH_INITIAL_VERTICES,
                    MESH_MAX_VERTICES, sizeof(MeshVertex))) return -1;

    int idx = mesh->vertex_count++;
    mesh->vertices[idx] = vert;
    return idx;
}

void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2) {
    assertf(!mesh->finalized, "mesh_add_triangle() after mesh_finalize()");
    if (mesh->finalized) return;
    if (!grow_array((void **)&mesh->indices, &mesh->index_capacity,
                    mesh->index_count + 3, MESH_INITIAL_INDICES,
                    MESH_MAX_INDICES, sizeof(uint16_t))) return;

    mesh->indices[mesh->index_count++] = i0;
    mesh->indices[mesh->index_count++] = i1;
    mesh->indices[mesh->index_count++] = i2;

    // Auto-update current group's index count
    if (mesh->group_count > 0) {
        mesh->groups[mesh->group_count - 1].index_count += 3;
    }
}

int mesh_begin_group(Mesh *mesh, int material_index) {
    if (mesh->group_count >= MESH_MAX_GROUPS) return -1;

    int idx = mesh->group_count++;
    mesh->groups[idx].material_index = material_index;
    mesh->groups[idx].index_start = mesh->index_count;
    mesh->groups[idx].index_count = 0;
    return idx;
}

void mesh_end_group(Mesh *mesh) {
    // No-op — group's index_count is tracked by mesh_add_triangle.
    // Exists for symmetry with mesh_begin_group and future use.
}

void mesh_compute_bounds(Mesh *mesh) {
    if (mesh->vertex_count == 0) {
        mesh->bound_center = (vec3_t){0, 0, 0};
        mesh->bound_radius = 0;
        return;
    }

    // Compute centroid
    float cx = 0, cy = 0, cz = 0;
    for (int i = 0; i < mesh->vertex_count; i++) {
        cx += mesh->vertices[i].position[0];
        cy += mesh->vertices[i].position[1];
        cz += mesh->vertices[i].position[2];
    }
    float inv_n = 1.0f / mesh->vertex_count;
    cx *= inv_n;
    cy *= inv_n;
    cz *= inv_n;
    mesh->bound_center = (vec3_t){cx, cy, cz};

    // Compute radius (max distance from centroid)
    float max_dist_sq = 0;
    for (int i = 0; i < mesh->vertex_count; i++) {
        float dx = mesh->vertices[i].position[0] - cx;
        float dy = mesh->vertices[i].position[1] - cy;
        float dz = mesh->vertices[i].position[2] - cz;
        float dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > max_dist_sq) max_dist_sq = dist_sq;
    }
    mesh->bound_radius = sqrtf(max_dist_sq);

    for (int g = 0; g < mesh->group_count; g++) {
        mesh_analyze_group(mesh, &mesh->groups[g]);
    }
}

// Precompute a group's centre, normal and planarity (roadmap D3/D24).
// A group is planar when every vertex shares one normal and lies on the plane
// through the centre: one plane test then decides facing for the whole group.
// Curved groups (sphere bands) wrap around the mesh, so neither the first
// vertex's normal nor the average normal describes them; mesh_draw culls and
// lights those per triangle instead.
void mesh_analyze_group(const Mesh *mesh, MeshFaceGroup *group) {
    group->planar = false;
    group->center[0] = group->center[1] = group->center[2] = 0.0f;
    group->normal[0] = 0.0f; group->normal[1] = 1.0f; group->normal[2] = 0.0f;
    if (group->index_count == 0) return;

    const uint16_t *idx = &mesh->indices[group->index_start];
    float nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < group->index_count; i++) {
        const MeshVertex *v = &mesh->vertices[idx[i]];
        group->center[0] += v->position[0];
        group->center[1] += v->position[1];
        group->center[2] += v->position[2];
        nx += v->normal[0]; ny += v->normal[1]; nz += v->normal[2];
    }
    float inv = 1.0f / group->index_count;
    for (int k = 0; k < 3; k++) group->center[k] *= inv;

    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len > 1e-4f) {
        group->normal[0] = nx / len;
        group->normal[1] = ny / len;
        group->normal[2] = nz / len;
    }

    const float *n0 = mesh->vertices[idx[0]].normal;
    const float EPS = 1e-3f;
    bool planar = true;
    for (int i = 0; i < group->index_count && planar; i++) {
        const MeshVertex *v = &mesh->vertices[idx[i]];
        float dn = fabsf(v->normal[0] - n0[0]) + fabsf(v->normal[1] - n0[1]) +
                   fabsf(v->normal[2] - n0[2]);
        float dp = (v->position[0] - group->center[0]) * n0[0] +
                   (v->position[1] - group->center[1]) * n0[1] +
                   (v->position[2] - group->center[2]) * n0[2];
        if (dn > EPS || fabsf(dp) > EPS * (1.0f + mesh->bound_radius)) planar = false;
    }
    group->planar = planar;
    if (planar) {
        group->normal[0] = n0[0]; group->normal[1] = n0[1]; group->normal[2] = n0[2];
    }
}

// --- Geometry placement (S9.1, D26; the colours are in engine/hot.h) ---

// Next free colour in the window. Reset at every scene init, so a scene's
// meshes get the same colours after every boot and every reset.
static uint32_t geometry_cursor = ENGINE_GEOMETRY_COLOUR_LO;

void mesh_placement_reset(void) {
    geometry_cursor = ENGINE_GEOMETRY_COLOUR_LO;
}

// A block of `bytes` at a chosen D-cache colour: packed after the previous
// block in [LO, HI), wrapping; a larger block starts at LO. The allocation is
// 8 KB aligned, so the colour is its offset; the bytes before it (at most
// HI - 16) are the price of a fixed colour. *raw gets the pointer to free.
static char *geometry_alloc(size_t bytes, void **raw) {
    size_t size = (bytes + 15) & ~(size_t)15;
    uint32_t colour = ENGINE_GEOMETRY_COLOUR_LO;
    if (size <= ENGINE_GEOMETRY_COLOUR_HI - ENGINE_GEOMETRY_COLOUR_LO) {
        if (geometry_cursor + size > ENGINE_GEOMETRY_COLOUR_HI) geometry_cursor = ENGINE_GEOMETRY_COLOUR_LO;
        colour = geometry_cursor;
        geometry_cursor += (uint32_t)size;
    }
    char *block = memalign(ENGINE_DCACHE_BYTES, colour + size);
    *raw = block;
    return block ? block + colour : NULL;
}

// --- Finalize ---

void mesh_finalize(Mesh *mesh) {
    if (mesh->finalized) return;
    mesh_compute_bounds(mesh);

    // One allocation for both arrays, at a chosen D-cache colour (above) and
    // so aligned to the 16-byte line: a 32-byte vertex spans exactly two
    // lines, and a mesh's vertex and index data sit together. The build
    // arrays (grown by doubling) are freed.
    size_t vbytes = sizeof(MeshVertex) * (size_t)mesh->vertex_count;
    size_t ibytes = sizeof(uint16_t) * (size_t)mesh->index_count;
    if (vbytes + ibytes > 0) {
        void *raw;
        char *data = geometry_alloc(vbytes + ibytes, &raw);
        if (data) {     // out of memory: keep the (valid) build arrays
            if (vbytes) memcpy(data, mesh->vertices, vbytes);
            if (ibytes) memcpy(data + vbytes, mesh->indices, ibytes);
            free(mesh->vertices);
            free(mesh->indices);
            mesh->block = raw;
            mesh->vertices = vbytes ? (MeshVertex *)data : NULL;
            mesh->indices = ibytes ? (uint16_t *)(data + vbytes) : NULL;
            mesh->vertex_capacity = mesh->vertex_count;
            mesh->index_capacity = mesh->index_count;
        }
    }
    mesh->finalized = true;
}
