#include "cube.h"
#include "mesh.h"
#include "texture.h"
#include <math.h>

#define CUBE_SIZE 80.0f
#define TEX_SIZE  32.0f

#define ROTATE_SPEED_Y 0.3f    // radians per second
#define ROTATE_SPEED_X 0.15f   // radians per second

// World-space position
static vec3_t cube_position = {0.0f, 0.0f, 0.0f};

// Model rotation angles (auto-rotating)
static float model_angle_x = 0.3f;
static float model_angle_y = 0.5f;

// Model matrix
static mat4_t model_matrix;

// The cube mesh
static Mesh cube_mesh;

// Face normals (local space, unit cube)
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

// Vertex positions for each face (BL, BR, TR, TL) — unit cube [-1,1]
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
    mesh_init(&cube_mesh);
    cube_mesh.backface_cull = true;

    // Add 6 materials (one per face, each with a different texture + color)
    for (int f = 0; f < 6; f++) {
        mesh_add_material(&cube_mesh, (Material){
            .type = MATERIAL_TEXTURED,
            .texture_slot = face_tex_slot[f],
            .base_color = {face_base_colors[f][0],
                           face_base_colors[f][1],
                           face_base_colors[f][2]}
        });
    }

    // Add 6 face groups (one quad = 4 verts + 2 triangles each)
    for (int f = 0; f < 6; f++) {
        mesh_begin_group(&cube_mesh, f);
        int base = cube_mesh.vertex_count;
        for (int v = 0; v < 4; v++) {
            mesh_add_vertex(&cube_mesh, (MeshVertex){
                .position = {face_verts[f][v][0],
                             face_verts[f][v][1],
                             face_verts[f][v][2]},
                .normal   = {face_normals[f][0],
                             face_normals[f][1],
                             face_normals[f][2]},
                .uv       = {face_uvs[v][0],
                             face_uvs[v][1]}
            });
        }
        mesh_add_triangle(&cube_mesh, base + 0, base + 1, base + 2);
        mesh_add_triangle(&cube_mesh, base + 0, base + 2, base + 3);
        mesh_end_group(&cube_mesh);
    }

    mesh_compute_bounds(&cube_mesh);
    update_model_matrix();
}

void cube_update(float dt) {
    model_angle_y += ROTATE_SPEED_Y * dt;
    model_angle_x += ROTATE_SPEED_X * dt;
    update_model_matrix();
}

void cube_draw(const Camera *cam, const LightConfig *light) {
    mesh_draw(&cube_mesh, &model_matrix, cam, light);
}

void cube_cleanup(void) {
    mesh_cleanup(&cube_mesh);
}
