#include "shadow.h"
#include "texture.h"
#include "../debug/stats.h"
#include "../engine/hot.h"
#include "../engine/engine_config.h"
#include <math.h>
#include <string.h>

// Shadow sits slightly above floor to avoid Z-fighting.
// Floor uses Z_BIAS=0.005 pushing it deeper, so shadow at floor_y + small
// offset will naturally be in front of floor in the Z-buffer.
#define SHADOW_Y_OFFSET  0.01f

// Compute shadow color from darkness setting
static ENGINE_HOT color_t shadow_color_from_darkness(float darkness) {
    // darkness 0.0 = very light shadow, 1.0 = nearly black
    uint8_t v = (uint8_t)((1.0f - darkness) * 40.0f + 5.0f);
    return RGBA32(v, v, v + 4, 255);
}

ENGINE_HOT void shadow_begin(const Camera *cam, const LightConfig *light) {
    (void)cam;

    // Set up RDP for shadow rendering:
    // - 1-cycle mode (CRITICAL for hardware — never use fill mode for triangles)
    // - Flat color combiner
    // - Z-buffer read ON (shadows clip at floor edges)
    // - Z-buffer write OFF (shadows don't occlude objects drawn later)
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_zbuf(true, false);

    // Set shadow color
    color_t col = shadow_color_from_darkness(light->shadow.darkness);
    rdpq_set_prim_color(col);
}

void shadow_end(void) {
    // No explicit cleanup — next rendering pass sets its own RDP mode.
}

// ============================================================
// Blob Shadows — dark quad under each object
// ============================================================

ENGINE_HOT void shadow_draw_blob(const Camera *cam, const LightConfig *light,
                                 const ShadowCaster *caster) {
    float floor_y = light->shadow.floor_y;
    float sy = floor_y + SHADOW_Y_OFFSET;

    // Blob size: use shadow config radius, scaled by object's bound radius ratio
    float r = light->shadow.blob_radius;
    if (caster->bound_radius > 0) {
        // Scale blob proportionally to object size (capped)
        float scale = caster->bound_radius / 80.0f;
        if (scale < 0.5f) scale = 0.5f;
        if (scale > 2.0f) scale = 2.0f;
        r *= scale;
    }

    // Fade shadow based on height above floor (higher = lighter)
    float height = caster->position.y - floor_y;
    if (height < 0) return;  // Below floor — no shadow
    if (height > 400.0f) return;  // Too high — shadow too faint

    // Build 4 world-space corners of quad on floor plane
    float cx = caster->position.x;
    float cz = caster->position.z;

    float verts[4][3] = {
        {cx - r, sy, cz - r},  // back-left
        {cx + r, sy, cz - r},  // back-right
        {cx + r, sy, cz + r},  // front-right
        {cx - r, sy, cz + r},  // front-left
    };

    // Project each vertex through VP to screen
    float screen[4][3] ENGINE_NOINIT;
    for (int i = 0; i < 4; i++) {
        vec3_t pos = {verts[i][0], verts[i][1], verts[i][2]};
        vec4_t clip;
        mat4_mul_vec3(&clip, &cam->vp, &pos);

        if (clip.w < 1.0f) return;  // Behind camera — skip entire blob

        float inv_w = 1.0f / clip.w;
        screen[i][0] = (clip.x * inv_w * 0.5f + 0.5f) * (float)ENGINE_SCREEN_W;
        screen[i][1] = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * (float)ENGINE_SCREEN_H;

        if (screen[i][0] < ENGINE_GUARD_X_MIN || screen[i][0] > ENGINE_GUARD_X_MAX ||
            screen[i][1] < ENGINE_GUARD_Y_MIN || screen[i][1] > ENGINE_GUARD_Y_MAX)
            return;  // Guard band — skip

        float depth = clip.z * inv_w * 0.5f + 0.5f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        screen[i][2] = depth;
    }

    // Two triangles: (0,1,2) and (0,2,3)
    rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[1], screen[2]);
    rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[2], screen[3]);

    STATS_ADD(tris_shadow, 2);
}

// ============================================================
// Projected Shadows — mesh silhouette flattened onto floor
// ============================================================

// Shadow vertices of the current caster, each projected at most once (D14),
// the first time a drawn triangle needs it. Static scratch: a mesh has at most
// MESH_MAX_VERTICES vertices.
static float   shadow_scr[MESH_MAX_VERTICES][3];   // {X, Y, Z} on screen
static uint8_t shadow_state[MESH_MAX_VERTICES];    // 0 not yet, 1 on screen, 2 rejected

// Project one vertex through M = VP * S * model; false when it is in front of
// the near plane or outside the guard band.
static inline ENGINE_HOT bool shadow_project(const mat4_t *m, const MeshVertex *mv,
                                             float out[3]) {
    vec3_t local = {mv->position[0], mv->position[1], mv->position[2]};
    vec4_t clip;
    mat4_mul_vec3(&clip, m, &local);
    if (clip.w < 1.0f) return false;

    float inv_w = 1.0f / clip.w;
    float scr_x = (clip.x * inv_w * 0.5f + 0.5f) * (float)ENGINE_SCREEN_W;
    float scr_y = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * (float)ENGINE_SCREEN_H;
    if (scr_x < ENGINE_GUARD_X_MIN || scr_x > ENGINE_GUARD_X_MAX ||
        scr_y < ENGINE_GUARD_Y_MIN || scr_y > ENGINE_GUARD_Y_MAX) return false;

    float depth = clip.z * inv_w * 0.5f + 0.5f;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    out[0] = scr_x;
    out[1] = scr_y;
    out[2] = depth;
    return true;
}

ENGINE_HOT void shadow_draw_projected(const Camera *cam, const LightConfig *light,
                                      const ShadowCaster *caster) {
    const Mesh *mesh = caster->mesh;
    const mat4_t *model = caster->model;
    float floor_y = light->shadow.floor_y;
    float sy = floor_y + SHADOW_Y_OFFSET;

    // Light direction (toward light source)
    float lx = light->direction[0];
    float ly = light->direction[1];
    float lz = light->direction[2];

    // Guard: light must have vertical component to cast ground shadows
    if (ly < 0.05f) return;
    float kx = lx / ly, kz = lz / ly;

    // Cull the whole shadow when it is off screen. The shadow of the caster's
    // bounding sphere (radius r) is an ellipse around the projected centre
    // whose longest axis is r / ly, so a sphere of that radius contains it.
    vec3_t wc;
    float wr;
    mesh_world_bounds(mesh, model, &wc, &wr);
    vec3_t sc = {wc.x - kx * (wc.y - floor_y), sy, wc.z - kz * (wc.y - floor_y)};
    if (!camera_sphere_visible(cam, &sc, wr / ly)) return;

    // Projecting a world point along the light onto the plane y = sy is
    // affine: x' = x - kx*(y - floor_y), y' = sy, z' = z - kz*(y - floor_y).
    // So one matrix takes a local vertex straight to its shadow's clip
    // position: M = VP * S * model (column-major m[col][row]).
    mat4_t S = {{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {-kx,  0.0f, -kz,  0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {kx * floor_y, sy, kz * floor_y, 1.0f},
    }};
    mat4_t sm, m;
    mat4_mul(&sm, &S, model);
    mat4_mul(&m, &cam->vp, &sm);

    // A closed mesh's shadow is exactly covered by its light-facing faces, so
    // faces turned away from the light are skipped. A flat group is tested
    // once with its normal (before any of its vertices is projected); a
    // triangle of a curved group by its projected winding: seen from above
    // the floor, a light-facing face keeps the front-face winding (negative
    // screen area). Open or double-sided meshes (backface_cull off) project
    // every face.
    bool light_facing_only = mesh->backface_cull;
    float orient = (cam->position.y >= sy) ? 1.0f : -1.0f;   // camera below the floor flips it
    float cof[3][3];
    mesh_normal_matrix(model, cof);

    memset(shadow_state, 0, (size_t)mesh->vertex_count);
    int tri_count = 0;

    for (int g = 0; g < mesh->group_count; g++) {
        const MeshFaceGroup *group = &mesh->groups[g];
        if (group->index_count == 0) continue;

        bool check_winding = light_facing_only;
        if (light_facing_only && group->planar) {
            const float *n = group->normal;
            float facing = 0.0f;
            for (int r = 0; r < 3; r++) {
                facing += (cof[0][r] * n[0] + cof[1][r] * n[1] + cof[2][r] * n[2]) *
                          light->direction[r];
            }
            if (facing <= 0.0f) continue;          // away from the light: no shadow
            check_winding = false;                 // the whole group faces the light
        }

        for (int i = group->index_start; i < group->index_start + group->index_count; i += 3) {
            int idx[3] = {mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2]};
            bool ok = true;
            for (int k = 0; k < 3; k++) {
                int v = idx[k];
                if (shadow_state[v] == 0) {
                    shadow_state[v] = shadow_project(&m, &mesh->vertices[v], shadow_scr[v]) ? 1 : 2;
                }
                if (shadow_state[v] != 1) ok = false;
            }
            if (!ok) continue;
            if (check_winding &&
                orient * mesh_screen_area2(shadow_scr[idx[0]], shadow_scr[idx[1]],
                                           shadow_scr[idx[2]]) >= 0.0f)
                continue;

            rdpq_triangle(&TRIFMT_ZBUF, shadow_scr[idx[0]], shadow_scr[idx[1]], shadow_scr[idx[2]]);
            tri_count++;
        }
    }

    STATS_ADD(tris_shadow, tri_count);
}
