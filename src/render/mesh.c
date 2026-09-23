#include "mesh.h"
#include "texture.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "atmosphere.h"
#include "../engine/hot.h"
#include <math.h>

// Mesh rendering. Building and bounds live in mesh_build.c.

// Guard band: vertices outside these screen-space bounds overflow
// RDP 12.2 fixed-point math and cause rendering artifacts.
#define GUARD_X_MIN  -1024.0f
#define GUARD_X_MAX   1344.0f
#define GUARD_Y_MIN  -1024.0f
#define GUARD_Y_MAX   1264.0f

// Local point -> world (model is column-major: m[col][row])
static inline ENGINE_HOT void model_point(const mat4_t *m, const float p[3], float out[3]) {
    for (int r = 0; r < 3; r++) {
        out[r] = m->m[0][r] * p[0] + m->m[1][r] * p[1] + m->m[2][r] * p[2] + m->m[3][r];
    }
}

// Local normal -> world unit normal through the cofactor matrix
// (inverse transpose times det), so non-uniform scale keeps normals
// perpendicular to their faces. cof[k] = column_{k+1} x column_{k+2}.
static inline ENGINE_HOT void model_normal(const float cof[3][3], const float n[3], float out[3]) {
    for (int r = 0; r < 3; r++) {
        out[r] = cof[0][r] * n[0] + cof[1][r] * n[1] + cof[2][r] * n[2];
    }
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        out[0] *= inv; out[1] *= inv; out[2] *= inv;
    }
}

static ENGINE_HOT void cofactor3(const mat4_t *m, float cof[3][3]) {
    for (int k = 0; k < 3; k++) {
        const float *a = m->m[(k + 1) % 3];
        const float *b = m->m[(k + 2) % 3];
        cof[k][0] = a[1] * b[2] - a[2] * b[1];
        cof[k][1] = a[2] * b[0] - a[0] * b[2];
        cof[k][2] = a[0] * b[1] - a[1] * b[0];
    }
}

// Light a material and set the colour: prim colour when unfogged, or return
// it in shade[] (0..1) for the shade-RGB channels when fog is on.
static inline ENGINE_HOT void light_material(const Material *mat, const LightConfig *light,
                                  float nrm[3], float view_dir[3],
                                  const float world_pos[3], bool use_fog,
                                  float shade[3]) {
    color_t lit = lighting_calculate(light, nrm, view_dir, world_pos);
    uint8_t r = (uint8_t)((mat->base_color[0] * lit.r) / 255);
    uint8_t g = (uint8_t)((mat->base_color[1] * lit.g) / 255);
    uint8_t b = (uint8_t)((mat->base_color[2] * lit.b) / 255);
    if (use_fog) {
        shade[0] = r / 255.0f;
        shade[1] = g / 255.0f;
        shade[2] = b / 255.0f;
    } else {
        rdpq_set_prim_color(RGBA32(r, g, b, 255));
    }
}

ENGINE_HOT void mesh_draw(const Mesh *mesh, const mat4_t *model,
                          const Camera *cam, const LightConfig *light) {
    if (mesh->vertex_count == 0 || mesh->index_count == 0) return;
    STATS_INC(mesh_draws);

    // 1. Transform bounding sphere center to world space for frustum cull
    PROF_BEGIN(PROF_MESH_CULL);
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

    bool visible = camera_sphere_visible(cam, &world_center, world_radius);
    PROF_END(PROF_MESH_CULL);
    if (!visible) {
        STATS_INC(mesh_culled_frustum);
        return;
    }

    // 2. Build MVP = VP * Model, and the normal matrix
    mat4_t mvp;
    mat4_mul(&mvp, &cam->vp, model);
    float cof[3][3];
    cofactor3(model, cof);

    // 3. View direction (for lighting) and camera position (for culling)
    float view_dir[3] = {cam->view_dir.x, cam->view_dir.y, cam->view_dir.z};
    float cam_pos[3] = {cam->position.x, cam->position.y, cam->position.z};

    int total_tris = 0;

    // 4. Query fog state once per draw call
    const FogConfig *fog = atmosphere_get_fog();
    bool use_fog = fog->enabled;
    if (use_fog) {
        rdpq_set_fog_color(fog->color);
    }

    // 5. Set the RDP mode only when the material type or alpha cutout changes.
    //    rdpq_set_mode_standard() clears alpha compare, so a material without
    //    cutout never inherits it from the previous one (roadmap defect D5).
    int last_mode_key = -1;

    for (int g = 0; g < mesh->group_count; g++) {
        const MeshFaceGroup *group = &mesh->groups[g];
        if (group->index_count == 0) continue;

        const Material *mat = &mesh->materials[group->material_index];

        int mode_key = (int)mat->type * 2 + (mat->alpha_cutout ? 1 : 0);
        if (mode_key != last_mode_key) {
            rdpq_set_mode_standard();
            STATS_INC(mode_changes);
            rdpq_mode_zbuf(true, true);

            if (use_fog) {
                switch (mat->type) {
                case MATERIAL_TEXTURED:
                    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
                    rdpq_mode_persp(true);
                    rdpq_mode_filter(FILTER_BILINEAR);
                    break;
                case MATERIAL_FLAT_COLOR:
                    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
                    break;
                }
                rdpq_mode_fog(RDPQ_FOG_STANDARD);
            } else {
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
            }

            if (mat->alpha_cutout) {
                rdpq_mode_alphacompare(1);
            }

            last_mode_key = mode_key;
        }

        // Planar groups (cube faces, pillar sides) are culled and lit once,
        // exactly: the camera is either in front of the group's plane or not.
        // Curved groups (sphere bands) face every way at once, so they are
        // culled by screen winding and lit per triangle below (D3, D24).
        bool planar = group->planar;
        float shade[3] = {0, 0, 0};   // lit colour 0..1 (shade RGB when fogged)
        if (planar) {
            PROF_BEGIN(PROF_MESH_LIGHT);
            float world_pos[3], nrm[3];
            model_point(model, group->center, world_pos);
            model_normal(cof, group->normal, nrm);
            bool culled = mesh->backface_cull &&
                (nrm[0] * (cam_pos[0] - world_pos[0]) +
                 nrm[1] * (cam_pos[1] - world_pos[1]) +
                 nrm[2] * (cam_pos[2] - world_pos[2])) <= 0.0f;
            if (!culled) {
                light_material(mat, light, nrm, view_dir, world_pos, use_fog, shade);
            }
            PROF_END(PROF_MESH_LIGHT);
            if (culled) { STATS_INC(groups_culled_backface); continue; }
        }

        // Upload the texture only for groups that survive the cull
        // (texture changes per group, mode doesn't; roadmap defect D4)
        if (mat->type == MATERIAL_TEXTURED && mat->texture_slot >= 0) {
            texture_upload(mat->texture_slot, TILE0);
        }
        STATS_INC(groups_drawn);

        // Select triangle format for this group
        const rdpq_trifmt_t *trifmt;
        if (use_fog) {
            trifmt = (mat->type == MATERIAL_TEXTURED)
                ? &TRIFMT_ZBUF_SHADE_TEX : &TRIFMT_ZBUF_SHADE;
        } else {
            // Flat materials must not use a textured format: the combiner
            // ignores TEX0, and the RDP validator flags it (roadmap defect D2).
            trifmt = (mat->type == MATERIAL_TEXTURED)
                ? &TRIFMT_ZBUF_TEX : &TRIFMT_ZBUF;
        }

        // Draw all triangles in this group
        PROF_BEGIN(PROF_MESH_TRIS);
        for (int i = group->index_start;
             i < group->index_start + group->index_count;
             i += 3) {
            const MeshVertex *tri_verts[3] = {
                &mesh->vertices[mesh->indices[i]],
                &mesh->vertices[mesh->indices[i + 1]],
                &mesh->vertices[mesh->indices[i + 2]],
            };
            float screen[3][10] ENGINE_NOINIT;  // Max: {X,Y,Z,R,G,B,A,S,T,INV_W}
            float inv_ws[3] ENGINE_NOINIT, fog_ts[3] ENGINE_NOINIT;
            bool reject = false;

            for (int v = 0; v < 3; v++) {
                vec3_t pos = {tri_verts[v]->position[0],
                              tri_verts[v]->position[1],
                              tri_verts[v]->position[2]};
                vec4_t clip;
                mat4_mul_vec3(&clip, &mvp, &pos);

                // Near-plane rejection
                if (clip.w < 1.0f) { reject = true; STATS_INC(tris_rejected_near); break; }

                float inv_w = 1.0f / clip.w;
                float ndc_x = clip.x * inv_w;
                float ndc_y = clip.y * inv_w;
                float ndc_z = clip.z * inv_w;

                screen[v][0] = (ndc_x * 0.5f + 0.5f) * 320.0f;
                screen[v][1] = (1.0f - (ndc_y * 0.5f + 0.5f)) * 240.0f;

                // Guard band check
                if (screen[v][0] < GUARD_X_MIN || screen[v][0] > GUARD_X_MAX ||
                    screen[v][1] < GUARD_Y_MIN || screen[v][1] > GUARD_Y_MAX) {
                    reject = true; STATS_INC(tris_rejected_guard); break;
                }

                float depth = ndc_z * 0.5f + 0.5f;
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;
                screen[v][2] = depth;
                inv_ws[v] = inv_w;
                if (use_fog) fog_ts[v] = fog_calculate_factor(clip.w);
            }

            if (reject) continue;

            if (!planar) {
                // Exact per-triangle back-face test on the projected winding
                if (mesh->backface_cull &&
                    mesh_screen_area2(screen[0], screen[1], screen[2]) >= 0.0f) {
                    STATS_INC(tris_culled_backface);
                    continue;
                }
                // Flat-shade this triangle with the average of its vertex normals
                PROF_BEGIN(PROF_MESH_LIGHT);
                float n_obj[3] ENGINE_NOINIT, c_obj[3] ENGINE_NOINIT;
                float nrm[3] ENGINE_NOINIT, world_pos[3] ENGINE_NOINIT;
                for (int k = 0; k < 3; k++) {
                    n_obj[k] = tri_verts[0]->normal[k] + tri_verts[1]->normal[k] +
                               tri_verts[2]->normal[k];
                    c_obj[k] = (tri_verts[0]->position[k] + tri_verts[1]->position[k] +
                                tri_verts[2]->position[k]) * (1.0f / 3.0f);
                }
                model_normal(cof, n_obj, nrm);
                model_point(model, c_obj, world_pos);
                light_material(mat, light, nrm, view_dir, world_pos, use_fog, shade);
                PROF_END(PROF_MESH_LIGHT);
            }

            for (int v = 0; v < 3; v++) {
                if (use_fog) {
                    screen[v][3] = shade[0];
                    screen[v][4] = shade[1];
                    screen[v][5] = shade[2];
                    screen[v][6] = 1.0f - fog_ts[v];  // 1.0=visible, 0.0=full fog
                    if (mat->type == MATERIAL_TEXTURED) {
                        screen[v][7] = tri_verts[v]->uv[0];
                        screen[v][8] = tri_verts[v]->uv[1];
                        screen[v][9] = inv_ws[v];
                    }
                } else {
                    screen[v][3] = tri_verts[v]->uv[0];
                    screen[v][4] = tri_verts[v]->uv[1];
                    screen[v][5] = inv_ws[v];
                }
            }

            rdpq_triangle(trifmt, screen[0], screen[1], screen[2]);
            total_tris++;
        }
        PROF_END(PROF_MESH_TRIS);
    }

    STATS_ADD(tris_mesh, total_tris);
}
