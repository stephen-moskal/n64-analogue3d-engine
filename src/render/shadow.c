#include "shadow.h"
#include "texture.h"
#include <math.h>

// Shadow sits slightly above floor to avoid Z-fighting.
// Floor uses Z_BIAS=0.005 pushing it deeper, so shadow at floor_y + small
// offset will naturally be in front of floor in the Z-buffer.
#define SHADOW_Y_OFFSET  0.01f

// Guard band (same as mesh.c / floor.c)
#define GUARD_X_MIN  -1024.0f
#define GUARD_X_MAX   1344.0f
#define GUARD_Y_MIN  -1024.0f
#define GUARD_Y_MAX   1264.0f

// Compute shadow color from darkness setting
static color_t shadow_color_from_darkness(float darkness) {
    // darkness 0.0 = very light shadow, 1.0 = nearly black
    uint8_t v = (uint8_t)((1.0f - darkness) * 40.0f + 5.0f);
    return RGBA32(v, v, v + 4, 255);
}

void shadow_begin(const Camera *cam, const LightConfig *light) {
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

void shadow_draw_blob(const Camera *cam, const LightConfig *light,
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
    float screen[4][3];
    for (int i = 0; i < 4; i++) {
        vec3_t pos = {verts[i][0], verts[i][1], verts[i][2]};
        vec4_t clip;
        mat4_mul_vec3(&clip, &cam->vp, &pos);

        if (clip.w < 1.0f) return;  // Behind camera — skip entire blob

        float inv_w = 1.0f / clip.w;
        screen[i][0] = (clip.x * inv_w * 0.5f + 0.5f) * 320.0f;
        screen[i][1] = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * 240.0f;

        if (screen[i][0] < GUARD_X_MIN || screen[i][0] > GUARD_X_MAX ||
            screen[i][1] < GUARD_Y_MIN || screen[i][1] > GUARD_Y_MAX)
            return;  // Guard band — skip

        float depth = clip.z * inv_w * 0.5f + 0.5f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        screen[i][2] = depth;
    }

    // Two triangles: (0,1,2) and (0,2,3)
    rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[1], screen[2]);
    rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[2], screen[3]);

    texture_stats_add_triangles(2);
}

// ============================================================
// Projected Shadows — mesh silhouette flattened onto floor
// ============================================================

void shadow_draw_projected(const Camera *cam, const LightConfig *light,
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

    float inv_ly = 1.0f / ly;
    int tri_count = 0;

    // Iterate all mesh groups — NO backface culling for shadows
    // (want all faces to project onto floor)
    for (int g = 0; g < mesh->group_count; g++) {
        const MeshFaceGroup *group = &mesh->groups[g];
        if (group->index_count == 0) continue;

        for (int i = group->index_start;
             i < group->index_start + group->index_count;
             i += 3) {

            float screen[3][3];  // {X, Y, Z} per vertex
            bool reject = false;

            for (int v = 0; v < 3; v++) {
                const MeshVertex *mv = &mesh->vertices[mesh->indices[i + v]];

                // Transform vertex to world space
                vec3_t local = {mv->position[0], mv->position[1], mv->position[2]};
                vec4_t world;
                mat4_mul_vec3(&world, model, &local);

                // Project along light direction to floor plane
                float t = (world.y - floor_y) * inv_ly;
                float sx = world.x - lx * t;
                float sz = world.z - lz * t;

                // Project shadow vertex through VP to screen
                vec3_t shadow_pos = {sx, sy, sz};
                vec4_t clip;
                mat4_mul_vec3(&clip, &cam->vp, &shadow_pos);

                if (clip.w < 1.0f) { reject = true; break; }

                float inv_w = 1.0f / clip.w;
                float scr_x = (clip.x * inv_w * 0.5f + 0.5f) * 320.0f;
                float scr_y = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * 240.0f;

                if (scr_x < GUARD_X_MIN || scr_x > GUARD_X_MAX ||
                    scr_y < GUARD_Y_MIN || scr_y > GUARD_Y_MAX) {
                    reject = true; break;
                }

                float depth = clip.z * inv_w * 0.5f + 0.5f;
                if (depth < 0.0f) depth = 0.0f;
                if (depth > 1.0f) depth = 1.0f;

                screen[v][0] = scr_x;
                screen[v][1] = scr_y;
                screen[v][2] = depth;
            }

            if (reject) continue;

            rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[1], screen[2]);
            tri_count++;
        }
    }

    texture_stats_add_triangles(tri_count);
}
