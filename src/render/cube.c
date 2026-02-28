#include "cube.h"
#include "texture.h"
#include <math.h>

#define CUBE_SIZE 80.0f
#define TEX_SIZE  32.0f

#define AUTO_ROTATE_Y 0.01f
#define AUTO_ROTATE_X 0.005f

// World-space position
static vec3_t cube_position = {0.0f, 0.0f, 0.0f};

// Model rotation angles (auto-rotating)
static float model_angle_x = 0.3f;
static float model_angle_y = 0.5f;

// Model matrix
static mat4_t model_matrix;

// Bounding sphere radius: sqrt(3) * CUBE_SIZE
static const float BOUNDING_RADIUS = 138.56f;

// Face normals (local space)
static const float face_normals[6][3] = {
    { 0.0f,  0.0f,  1.0f},  // Front  +Z
    { 0.0f,  0.0f, -1.0f},  // Back   -Z
    { 0.0f,  1.0f,  0.0f},  // Top    +Y
    { 0.0f, -1.0f,  0.0f},  // Bottom -Y
    { 1.0f,  0.0f,  0.0f},  // Right  +X
    {-1.0f,  0.0f,  0.0f},  // Left   -X
};

// Base face colors (modulated by lighting)
static const uint8_t face_base_colors[6][3] = {
    {255, 100, 100},  // Front  - Red
    {100, 255, 100},  // Back   - Green
    {100, 100, 255},  // Top    - Blue
    {255, 255, 100},  // Bottom - Yellow
    {255, 100, 255},  // Right  - Magenta
    {100, 255, 255},  // Left   - Cyan
};

// Vertex positions for each face (BL, BR, TR, TL)
static const float face_verts[6][4][3] = {
    {{-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}},   // Front
    {{ 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}},   // Back
    {{-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}},   // Top
    {{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}},   // Bottom
    {{ 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}},   // Right
    {{-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}},   // Left
};

// UV coordinates for quad corners mapped to 32x32 texture
static const float face_uvs[4][2] = {
    {0.0f,     TEX_SIZE},   // BL
    {TEX_SIZE, TEX_SIZE},   // BR
    {TEX_SIZE, 0.0f},       // TR
    {0.0f,     0.0f},       // TL
};

// Face-to-texture-slot mapping
static const int face_tex_slot[6] = {
    TEX_CUBE_FRONT, TEX_CUBE_BACK, TEX_CUBE_TOP,
    TEX_CUBE_BOTTOM, TEX_CUBE_RIGHT, TEX_CUBE_LEFT
};

static void update_model_matrix(void) {
    vec3_t scale = {CUBE_SIZE, CUBE_SIZE, CUBE_SIZE};
    mat4_from_srt(&model_matrix, &scale, model_angle_x, model_angle_y, 0.0f, &cube_position);
}

void cube_init(void) {
    update_model_matrix();
}

void cube_update(void) {
    model_angle_y += AUTO_ROTATE_Y;
    model_angle_x += AUTO_ROTATE_X;
    update_model_matrix();
}

void cube_draw(const Camera *cam, const LightConfig *light) {
    // Frustum culling: check bounding sphere
    if (!camera_sphere_visible(cam, &cube_position, BOUNDING_RADIUS)) {
        return;
    }

    // Build MVP = VP * Model
    mat4_t mvp;
    mat4_mul(&mvp, &cam->vp, &model_matrix);

    // Camera direction for backface culling
    float tcx = cam->position.x - cube_position.x;
    float tcy = cam->position.y - cube_position.y;
    float tcz = cam->position.z - cube_position.z;
    float tc_len = sqrtf(tcx * tcx + tcy * tcy + tcz * tcz);
    if (tc_len > 0.001f) {
        float inv = 1.0f / tc_len;
        tcx *= inv; tcy *= inv; tcz *= inv;
    }

    // View direction for lighting
    float view_dir[3] = {cam->view_dir.x, cam->view_dir.y, cam->view_dir.z};

    // Set up textured rendering with Z-buffer
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_persp(true);
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_zbuf(true, true);

    // Draw all 6 faces (Z-buffer handles depth ordering)
    for (int f = 0; f < 6; f++) {
        // Transform normal by model matrix upper 3x3 (column-major: m[col][row])
        float nx = model_matrix.m[0][0] * face_normals[f][0] +
                   model_matrix.m[1][0] * face_normals[f][1] +
                   model_matrix.m[2][0] * face_normals[f][2];
        float ny = model_matrix.m[0][1] * face_normals[f][0] +
                   model_matrix.m[1][1] * face_normals[f][1] +
                   model_matrix.m[2][1] * face_normals[f][2];
        float nz = model_matrix.m[0][2] * face_normals[f][0] +
                   model_matrix.m[1][2] * face_normals[f][1] +
                   model_matrix.m[2][2] * face_normals[f][2];

        // Re-normalize (model matrix includes scale)
        float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen > 0.001f) {
            float inv = 1.0f / nlen;
            nx *= inv; ny *= inv; nz *= inv;
        }

        // Backface culling: dot(world_normal, to_camera) < 0 means facing away
        float facing = nx * tcx + ny * tcy + nz * tcz;
        if (facing < 0.0f) continue;

        // Lighting
        float normal_f[3] = {nx, ny, nz};
        color_t lit_color = lighting_calculate(light, normal_f, view_dir);
        uint8_t r = (uint8_t)((face_base_colors[f][0] * lit_color.r) / 255);
        uint8_t g = (uint8_t)((face_base_colors[f][1] * lit_color.g) / 255);
        uint8_t b = (uint8_t)((face_base_colors[f][2] * lit_color.b) / 255);
        rdpq_set_prim_color(RGBA32(r, g, b, 255));

        // Upload face texture
        texture_upload(face_tex_slot[f], TILE0);

        // Transform and project vertices
        // TRIFMT_ZBUF_TEX: {X, Y, Z, S, T, INV_W}
        float verts[4][6];
        for (int v = 0; v < 4; v++) {
            vec3_t vert_local = {face_verts[f][v][0],
                                 face_verts[f][v][1],
                                 face_verts[f][v][2]};
            vec4_t clip;
            mat4_mul_vec3(&clip, &mvp, &vert_local);

            // Perspective divide
            float inv_w = 1.0f / clip.w;
            float ndc_x = clip.x * inv_w;
            float ndc_y = clip.y * inv_w;
            float ndc_z = clip.z * inv_w;

            // NDC to screen coordinates
            verts[v][0] = (ndc_x * 0.5f + 0.5f) * 320.0f;
            verts[v][1] = (1.0f - (ndc_y * 0.5f + 0.5f)) * 240.0f;

            // Z mapped to [0,1] for Z-buffer
            verts[v][2] = ndc_z * 0.5f + 0.5f;

            // Texture coordinates
            verts[v][3] = face_uvs[v][0];
            verts[v][4] = face_uvs[v][1];
            verts[v][5] = inv_w;
        }

        // Draw as two triangles
        rdpq_triangle(&TRIFMT_ZBUF_TEX, verts[0], verts[1], verts[2]);
        rdpq_triangle(&TRIFMT_ZBUF_TEX, verts[0], verts[2], verts[3]);

        texture_stats_add_triangles(2);
    }
}

void cube_cleanup(void) {
    // Nothing to clean up
}
