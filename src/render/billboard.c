#include "billboard.h"
#include "mesh.h"
#include "texture.h"
#include <math.h>
#include <string.h>

// Shared unit quad mesh: 4 vertices, 2 triangles, 1 textured material.
// billboard_draw() swaps texture_slot and base_color per instance before
// calling mesh_draw(), so the mesh is declared non-const.
static Mesh bb_mesh;
static bool bb_ready = false;

void billboard_init(void) {
    if (bb_ready) return;

    mesh_init(&bb_mesh);
    bb_mesh.backface_cull = false;  // Both sides visible (camera always faces front)

    // Single textured material (slot overridden per-draw)
    int mat = mesh_add_material(&bb_mesh, (Material){
        .type = MATERIAL_TEXTURED,
        .texture_slot = 0,
        .base_color = {255, 255, 255},
        .alpha_cutout = true,
    });

    mesh_begin_group(&bb_mesh, mat);

    // Unit quad centered on origin, in XY plane, facing +Z
    // UVs in texel space (32x32 sprites) — libdragon expects texel coords, not 0-1
    #define BB_TEX_SIZE 32.0f

    // Vertices: bottom-left, bottom-right, top-right, top-left
    mesh_add_vertex(&bb_mesh, (MeshVertex){
        .position = {-0.5f, -0.5f, 0.0f},
        .normal   = {0.0f, 0.0f, 1.0f},
        .uv       = {0.0f, BB_TEX_SIZE},
    });
    mesh_add_vertex(&bb_mesh, (MeshVertex){
        .position = {0.5f, -0.5f, 0.0f},
        .normal   = {0.0f, 0.0f, 1.0f},
        .uv       = {BB_TEX_SIZE, BB_TEX_SIZE},
    });
    mesh_add_vertex(&bb_mesh, (MeshVertex){
        .position = {0.5f, 0.5f, 0.0f},
        .normal   = {0.0f, 0.0f, 1.0f},
        .uv       = {BB_TEX_SIZE, 0.0f},
    });
    mesh_add_vertex(&bb_mesh, (MeshVertex){
        .position = {-0.5f, 0.5f, 0.0f},
        .normal   = {0.0f, 0.0f, 1.0f},
        .uv       = {0.0f, 0.0f},
    });

    mesh_add_triangle(&bb_mesh, 0, 1, 2);
    mesh_add_triangle(&bb_mesh, 0, 2, 3);

    mesh_end_group(&bb_mesh);
    mesh_compute_bounds(&bb_mesh);

    bb_ready = true;
}

void billboard_cleanup(void) {
    if (!bb_ready) return;
    mesh_cleanup(&bb_mesh);
    bb_ready = false;
}

void billboard_draw(SceneObject *obj, const Camera *cam, const LightConfig *light) {
    if (!bb_ready) return;

    BillboardData *data = (BillboardData *)obj->data;
    if (!data) return;

    // Build billboard model matrix:
    // Columns = [right * width, up * height, forward, position]

    vec3_t pos = obj->position;

    // Direction from billboard to camera (used to orient the quad)
    vec3_t to_cam = vec3_sub(&cam->position, &pos);

    vec3_t right, up, forward;
    vec3_t world_up = {0.0f, 1.0f, 0.0f};

    if (data->mode == BILLBOARD_CYLINDRICAL) {
        // Cylindrical: only rotate around Y axis
        // Project to_cam onto XZ plane
        vec3_t to_cam_xz = {to_cam.x, 0.0f, to_cam.z};
        float len_xz = vec3_length(&to_cam_xz);
        if (len_xz < 0.001f) {
            // Camera directly above — fallback forward
            forward = (vec3_t){0.0f, 0.0f, 1.0f};
        } else {
            forward = vec3_scale(&to_cam_xz, 1.0f / len_xz);
        }
        up = world_up;
        right = vec3_cross(&up, &forward);
        // right should already be normalized (up and forward are unit, perpendicular in XZ)
    } else {
        // Spherical: fully face camera
        float len = vec3_length(&to_cam);
        if (len < 0.001f) {
            forward = (vec3_t){0.0f, 0.0f, 1.0f};
        } else {
            forward = vec3_scale(&to_cam, 1.0f / len);
        }

        // Compute right = cross(world_up, forward)
        right = vec3_cross(&world_up, &forward);
        float right_len = vec3_length(&right);
        if (right_len < 0.001f) {
            // Camera directly above/below — fallback right
            right = (vec3_t){1.0f, 0.0f, 0.0f};
        } else {
            right = vec3_scale(&right, 1.0f / right_len);
        }

        // Recompute up = cross(forward, right) for orthogonal basis
        up = vec3_cross(&forward, &right);
    }

    // Scale right/up by billboard dimensions
    vec3_t scaled_right = vec3_scale(&right, data->width);
    vec3_t scaled_up    = vec3_scale(&up, data->height);

    // Build column-major 4x4 model matrix
    mat4_t model;
    memset(&model, 0, sizeof(mat4_t));

    // Column 0: right * width
    model.m[0][0] = scaled_right.x;
    model.m[0][1] = scaled_right.y;
    model.m[0][2] = scaled_right.z;

    // Column 1: up * height
    model.m[1][0] = scaled_up.x;
    model.m[1][1] = scaled_up.y;
    model.m[1][2] = scaled_up.z;

    // Column 2: forward (normal direction, unit scale)
    model.m[2][0] = forward.x;
    model.m[2][1] = forward.y;
    model.m[2][2] = forward.z;

    // Column 3: position
    model.m[3][0] = pos.x;
    model.m[3][1] = pos.y;
    model.m[3][2] = pos.z;
    model.m[3][3] = 1.0f;

    // Override shared mesh material for this instance
    bb_mesh.materials[0].texture_slot = data->texture_slot;
    bb_mesh.materials[0].base_color[0] = data->color[0];
    bb_mesh.materials[0].base_color[1] = data->color[1];
    bb_mesh.materials[0].base_color[2] = data->color[2];

    mesh_draw(&bb_mesh, &model, cam, light);
}
