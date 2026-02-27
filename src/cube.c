#include "cube.h"
#include <math.h>

#define CUBE_SIZE 80.0f

// Rotation angles
static float angle_x = 0.3f;
static float angle_y = 0.5f;

// View direction (camera looking at origin from +Z)
static float view_dir[3] = {0.0f, 0.0f, 1.0f};

// Face normals (before rotation)
static const float face_normals[6][3] = {
    { 0.0f,  0.0f,  1.0f},  // Front  +Z
    { 0.0f,  0.0f, -1.0f},  // Back   -Z
    { 0.0f,  1.0f,  0.0f},  // Top    +Y
    { 0.0f, -1.0f,  0.0f},  // Bottom -Y
    { 1.0f,  0.0f,  0.0f},  // Right  +X
    {-1.0f,  0.0f,  0.0f},  // Left   -X
};

// Base face colors (will be modulated by lighting)
static const uint8_t face_base_colors[6][3] = {
    {255, 100, 100},  // Front  - Red
    {100, 255, 100},  // Back   - Green
    {100, 100, 255},  // Top    - Blue
    {255, 255, 100},  // Bottom - Yellow
    {255, 100, 255},  // Right  - Magenta
    {100, 255, 255},  // Left   - Cyan
};

// Vertex positions for each face (4 corners per face)
static const float face_verts[6][4][3] = {
    // Front (+Z)
    {{-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}},
    // Back (-Z)
    {{ 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}},
    // Top (+Y)
    {{-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}},
    // Bottom (-Y)
    {{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}},
    // Right (+X)
    {{ 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}},
    // Left (-X)
    {{-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}},
};

// Rotation matrix
static float rot_matrix[3][3];

static void update_rotation_matrix(void) {
    float cx = cosf(angle_x);
    float sx = sinf(angle_x);
    float cy = cosf(angle_y);
    float sy = sinf(angle_y);

    // Combined Y * X rotation
    rot_matrix[0][0] = cy;
    rot_matrix[0][1] = sx * sy;
    rot_matrix[0][2] = -cx * sy;

    rot_matrix[1][0] = 0;
    rot_matrix[1][1] = cx;
    rot_matrix[1][2] = sx;

    rot_matrix[2][0] = sy;
    rot_matrix[2][1] = -sx * cy;
    rot_matrix[2][2] = cx * cy;
}

static void transform_point(const float in[3], float out[3]) {
    out[0] = rot_matrix[0][0] * in[0] + rot_matrix[0][1] * in[1] + rot_matrix[0][2] * in[2];
    out[1] = rot_matrix[1][0] * in[0] + rot_matrix[1][1] * in[1] + rot_matrix[1][2] * in[2];
    out[2] = rot_matrix[2][0] * in[0] + rot_matrix[2][1] * in[1] + rot_matrix[2][2] * in[2];
}

static void project_point(const float p[3], float *sx, float *sy) {
    // Simple perspective projection
    float z = p[2] + 300.0f;  // Camera distance
    if (z < 1.0f) z = 1.0f;

    float scale = 200.0f / z;
    *sx = 160.0f + p[0] * scale * CUBE_SIZE;
    *sy = 120.0f - p[1] * scale * CUBE_SIZE;
}

void cube_init(void) {
    update_rotation_matrix();
}

void cube_update(float rot_x, float rot_y) {
    angle_x += rot_x;
    angle_y += rot_y;
    update_rotation_matrix();
}

void cube_draw(const LightConfig *light) {
    // Calculate face depths for sorting (painter's algorithm)
    float face_depths[6];
    int face_order[6] = {0, 1, 2, 3, 4, 5};

    for (int f = 0; f < 6; f++) {
        float center[3] = {0, 0, 0};
        for (int v = 0; v < 4; v++) {
            float transformed[3];
            transform_point(face_verts[f][v], transformed);
            center[0] += transformed[0];
            center[1] += transformed[1];
            center[2] += transformed[2];
        }
        face_depths[f] = center[2] / 4.0f;
    }

    // Sort faces by depth (back to front)
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 6; j++) {
            if (face_depths[face_order[i]] > face_depths[face_order[j]]) {
                int tmp = face_order[i];
                face_order[i] = face_order[j];
                face_order[j] = tmp;
            }
        }
    }

    // Set up 1-cycle mode with flat shading for triangles
    // (fill mode only works with rectangles on real N64 hardware)
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    // Draw faces back to front
    for (int i = 0; i < 6; i++) {
        int f = face_order[i];

        // Transform normal
        float normal[3];
        transform_point(face_normals[f], normal);

        // Backface culling (skip faces pointing away from camera)
        if (normal[2] < 0) continue;

        // Calculate lighting
        color_t lit_color = lighting_calculate(light, normal, view_dir);

        // Modulate with base face color
        uint8_t r = (uint8_t)((face_base_colors[f][0] * lit_color.r) / 255);
        uint8_t g = (uint8_t)((face_base_colors[f][1] * lit_color.g) / 255);
        uint8_t b = (uint8_t)((face_base_colors[f][2] * lit_color.b) / 255);
        color_t final_color = RGBA32(r, g, b, 255);

        // Set face color
        rdpq_set_prim_color(final_color);

        // Transform and project vertices
        float sx[4], sy[4];
        for (int v = 0; v < 4; v++) {
            float transformed[3];
            transform_point(face_verts[f][v], transformed);
            project_point(transformed, &sx[v], &sy[v]);
        }

        // Draw as two triangles using rdpq
        // Triangle 1: vertices 0, 1, 2
        rdpq_triangle(&TRIFMT_FILL,
            (float[]){sx[0], sy[0]},
            (float[]){sx[1], sy[1]},
            (float[]){sx[2], sy[2]}
        );

        // Triangle 2: vertices 0, 2, 3
        rdpq_triangle(&TRIFMT_FILL,
            (float[]){sx[0], sy[0]},
            (float[]){sx[2], sy[2]},
            (float[]){sx[3], sy[3]}
        );
    }
}

void cube_cleanup(void) {
    // Nothing to clean up for software rendering
}
