#include "mesh.h"
#include "texture.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- Lifecycle ---

void mesh_init(Mesh *mesh) {
    memset(mesh, 0, sizeof(Mesh));
    mesh->vertices = NULL;
    mesh->indices = NULL;
    mesh->backface_cull = true;
}

void mesh_cleanup(Mesh *mesh) {
    if (mesh->vertices) {
        free(mesh->vertices);
        mesh->vertices = NULL;
    }
    if (mesh->indices) {
        free(mesh->indices);
        mesh->indices = NULL;
    }
    mesh->vertex_count = 0;
    mesh->index_count = 0;
    mesh->material_count = 0;
    mesh->group_count = 0;
}

// --- Building ---

int mesh_add_material(Mesh *mesh, Material mat) {
    if (mesh->material_count >= MESH_MAX_MATERIALS) return -1;
    int idx = mesh->material_count++;
    mesh->materials[idx] = mat;
    return idx;
}

int mesh_add_vertex(Mesh *mesh, MeshVertex vert) {
    if (mesh->vertex_count >= MESH_MAX_VERTICES) return -1;

    // Lazy allocation: allocate full capacity on first vertex
    if (!mesh->vertices) {
        mesh->vertices = malloc(sizeof(MeshVertex) * MESH_MAX_VERTICES);
        if (!mesh->vertices) return -1;
    }

    int idx = mesh->vertex_count++;
    mesh->vertices[idx] = vert;
    return idx;
}

void mesh_add_triangle(Mesh *mesh, uint16_t i0, uint16_t i1, uint16_t i2) {
    if (mesh->index_count + 3 > MESH_MAX_INDICES) return;

    // Lazy allocation
    if (!mesh->indices) {
        mesh->indices = malloc(sizeof(uint16_t) * MESH_MAX_INDICES);
        if (!mesh->indices) return;
    }

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
}

// --- Rendering ---

// Guard band: vertices outside these screen-space bounds overflow
// RDP 12.2 fixed-point math and cause rendering artifacts.
#define GUARD_X_MIN  -1024.0f
#define GUARD_X_MAX   1344.0f
#define GUARD_Y_MIN  -1024.0f
#define GUARD_Y_MAX   1264.0f

void mesh_draw(const Mesh *mesh, const mat4_t *model,
               const Camera *cam, const LightConfig *light) {
    if (mesh->vertex_count == 0 || mesh->index_count == 0) return;

    // 1. Transform bounding sphere center to world space for frustum cull
    vec4_t world_center_h;
    mat4_mul_vec3(&world_center_h, model, &mesh->bound_center);
    vec3_t world_center = {world_center_h.x, world_center_h.y, world_center_h.z};

    // Scale bounding radius by max column length of model matrix.
    // Use squared lengths to find the max, only one sqrtf at the end.
    float sx_sq = model->m[0][0] * model->m[0][0] +
                  model->m[0][1] * model->m[0][1] +
                  model->m[0][2] * model->m[0][2];
    float sy_sq = model->m[1][0] * model->m[1][0] +
                  model->m[1][1] * model->m[1][1] +
                  model->m[1][2] * model->m[1][2];
    float sz_sq = model->m[2][0] * model->m[2][0] +
                  model->m[2][1] * model->m[2][1] +
                  model->m[2][2] * model->m[2][2];
    float max_sq = sx_sq > sy_sq ? (sx_sq > sz_sq ? sx_sq : sz_sq)
                                 : (sy_sq > sz_sq ? sy_sq : sz_sq);
    float world_radius = mesh->bound_radius * sqrtf(max_sq);

    if (!camera_sphere_visible(cam, &world_center, world_radius)) {
        return;
    }

    // 2. Build MVP = VP * Model
    mat4_t mvp;
    mat4_mul(&mvp, &cam->vp, model);

    // 3. Camera direction (for backface culling)
    float tcx = cam->position.x - world_center.x;
    float tcy = cam->position.y - world_center.y;
    float tcz = cam->position.z - world_center.z;
    float tc_len = sqrtf(tcx * tcx + tcy * tcy + tcz * tcz);
    if (tc_len > 0.001f) {
        float inv = 1.0f / tc_len;
        tcx *= inv; tcy *= inv; tcz *= inv;
    }

    // 4. View direction (for lighting)
    float view_dir[3] = {cam->view_dir.x, cam->view_dir.y, cam->view_dir.z};

    int total_tris = 0;

    // 5. Set RDP mode ONCE before all groups.
    //    Only change texture/color per group — avoid repeated mode resets.
    //    If a mesh mixes material types, mode is reset only at the boundary.
    int last_mat_type = -1;

    for (int g = 0; g < mesh->group_count; g++) {
        const MeshFaceGroup *group = &mesh->groups[g];
        if (group->index_count == 0) continue;

        const Material *mat = &mesh->materials[group->material_index];

        // Only reset RDP mode when material type changes
        if ((int)mat->type != last_mat_type) {
            rdpq_set_mode_standard();
            rdpq_mode_zbuf(true, true);

            switch (mat->type) {
            case MATERIAL_TEXTURED:
                rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
                rdpq_mode_persp(true);
                rdpq_mode_filter(FILTER_BILINEAR);
                break;

            case MATERIAL_FLAT_COLOR:
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                break;
            }

            last_mat_type = (int)mat->type;
        }

        // Upload texture for this group (texture changes per group, mode doesn't)
        if (mat->type == MATERIAL_TEXTURED && mat->texture_slot >= 0) {
            texture_upload(mat->texture_slot, TILE0);
        }

        // Compute group normal + lighting ONCE per group.
        // In flat shading, all triangles in a group share the same normal
        // (all vertices were built with the same face normal).
        const MeshVertex *gv0 = &mesh->vertices[mesh->indices[group->index_start]];
        float nx = model->m[0][0] * gv0->normal[0] +
                   model->m[1][0] * gv0->normal[1] +
                   model->m[2][0] * gv0->normal[2];
        float ny = model->m[0][1] * gv0->normal[0] +
                   model->m[1][1] * gv0->normal[1] +
                   model->m[2][1] * gv0->normal[2];
        float nz = model->m[0][2] * gv0->normal[0] +
                   model->m[1][2] * gv0->normal[1] +
                   model->m[2][2] * gv0->normal[2];

        // Re-normalize (model matrix may include scale)
        float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen > 0.001f) {
            float inv = 1.0f / nlen;
            nx *= inv; ny *= inv; nz *= inv;
        }

        // Backface cull entire group
        if (mesh->backface_cull) {
            float facing = nx * tcx + ny * tcy + nz * tcz;
            if (facing < 0.0f) continue;
        }

        // Lighting — once per group
        float normal_f[3] = {nx, ny, nz};
        color_t lit_color = lighting_calculate(light, normal_f, view_dir);
        uint8_t r = (uint8_t)((mat->base_color[0] * lit_color.r) / 255);
        uint8_t g_col = (uint8_t)((mat->base_color[1] * lit_color.g) / 255);
        uint8_t b = (uint8_t)((mat->base_color[2] * lit_color.b) / 255);
        rdpq_set_prim_color(RGBA32(r, g_col, b, 255));

        // Draw all triangles in this group
        for (int i = group->index_start;
             i < group->index_start + group->index_count;
             i += 3) {
            const MeshVertex *v0 = &mesh->vertices[mesh->indices[i]];
            const MeshVertex *v1 = &mesh->vertices[mesh->indices[i + 1]];
            const MeshVertex *v2 = &mesh->vertices[mesh->indices[i + 2]];

            // Transform and project 3 vertices
            const MeshVertex *tri_verts[3] = {v0, v1, v2};
            float screen[3][6];  // {X, Y, Z, S, T, INV_W}
            bool reject = false;

            for (int v = 0; v < 3; v++) {
                vec3_t pos = {tri_verts[v]->position[0],
                              tri_verts[v]->position[1],
                              tri_verts[v]->position[2]};
                vec4_t clip;
                mat4_mul_vec3(&clip, &mvp, &pos);

                // Near-plane rejection: vertices behind or very close to
                // camera produce garbage Z from the perspective divide.
                if (clip.w < 1.0f) { reject = true; break; }

                float inv_w = 1.0f / clip.w;
                float ndc_x = clip.x * inv_w;
                float ndc_y = clip.y * inv_w;
                float ndc_z = clip.z * inv_w;

                screen[v][0] = (ndc_x * 0.5f + 0.5f) * 320.0f;
                screen[v][1] = (1.0f - (ndc_y * 0.5f + 0.5f)) * 240.0f;

                // Guard band: reject vertices that overflow RDP fixed-point
                if (screen[v][0] < GUARD_X_MIN || screen[v][0] > GUARD_X_MAX ||
                    screen[v][1] < GUARD_Y_MIN || screen[v][1] > GUARD_Y_MAX) {
                    reject = true; break;
                }

                // Depth with clamp to valid [0, 1] range
                float depth = ndc_z * 0.5f + 0.5f;
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                screen[v][2] = depth;

                screen[v][3] = tri_verts[v]->uv[0];
                screen[v][4] = tri_verts[v]->uv[1];
                screen[v][5] = inv_w;
            }

            if (reject) continue;

            rdpq_triangle(&TRIFMT_ZBUF_TEX, screen[0], screen[1], screen[2]);
            total_tris++;
        }
    }

    texture_stats_add_triangles(total_tris);
}
