#ifndef CAMERA_H
#define CAMERA_H

#include <libdragon.h>
#include <stdbool.h>

// Simple 3D math types (fgeom.h not available in toolchain)
typedef struct { float x, y, z; } vec3_t;
typedef struct { float x, y, z, w; } vec4_t;
typedef struct { float m[4][4]; } mat4_t;  // Column-major: m[col][row]

#define FRUSTUM_LEFT    0
#define FRUSTUM_RIGHT   1
#define FRUSTUM_BOTTOM  2
#define FRUSTUM_TOP     3
#define FRUSTUM_NEAR    4
#define FRUSTUM_FAR     5

typedef struct {
    // Orbital parameters
    float azimuth;
    float elevation;
    float distance;
    vec3_t target;

    // Derived camera state
    vec3_t position;
    vec3_t up;
    vec3_t view_dir;

    // Projection parameters
    float fov_y;
    float aspect;
    float near_plane;
    float far_plane;

    // Cached matrices
    mat4_t view;
    mat4_t proj;
    mat4_t vp;

    // Frustum planes: {A, B, C, D} where Ax+By+Cz+D >= 0 is inside
    float frustum[6][4];

    bool dirty;
} Camera;

// Scene/level camera preset
typedef struct {
    float azimuth;
    float elevation;
    float distance;
    vec3_t target;
    float fov_y;
    float near_plane;
    float far_plane;
} CameraConfig;

extern const CameraConfig CAMERA_DEFAULT;

void camera_init(Camera *cam, const CameraConfig *config);
void camera_orbit(Camera *cam, float d_azimuth, float d_elevation);
void camera_zoom(Camera *cam, float d_distance);
void camera_shift_target_y(Camera *cam, float d_y);
void camera_update(Camera *cam);

// Matrix math utilities
void mat4_perspective(mat4_t *out, float fov_y, float aspect, float near, float far);
void mat4_lookat(mat4_t *out, const vec3_t *eye, const vec3_t *target, const vec3_t *up);
void mat4_mul(mat4_t *out, const mat4_t *a, const mat4_t *b);
void mat4_mul_vec3(vec4_t *out, const mat4_t *m, const vec3_t *v);
void mat4_from_srt(mat4_t *out, const vec3_t *scale, float rx, float ry, float rz,
                   const vec3_t *translate);

// Frustum culling
bool camera_sphere_visible(const Camera *cam, const vec3_t *center, float radius);

#endif
