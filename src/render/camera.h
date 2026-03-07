#ifndef CAMERA_H
#define CAMERA_H

#include <libdragon.h>
#include <stdbool.h>
#include "../math/vec3.h"

// Additional math types (vec3_t comes from math/vec3.h)
typedef struct { float x, y, z, w; } vec4_t;
typedef struct { float m[4][4]; } mat4_t;  // Column-major: m[col][row]

#define FRUSTUM_LEFT    0
#define FRUSTUM_RIGHT   1
#define FRUSTUM_BOTTOM  2
#define FRUSTUM_TOP     3
#define FRUSTUM_NEAR    4
#define FRUSTUM_FAR     5

// Forward-declare CollisionWorld (avoid circular include)
struct CollisionWorld;

typedef enum {
    CAMERA_MODE_ORBITAL,    // Orbit around target point (default)
    CAMERA_MODE_FIXED,      // Fixed position + fixed look-at
    CAMERA_MODE_FOLLOW,     // Follow a target position with offset
} CameraMode;

typedef struct {
    CameraMode mode;

    // Orbital parameters (used in ORBITAL mode)
    float azimuth;
    float elevation;
    float distance;
    vec3_t target;

    // Follow mode state
    vec3_t follow_offset;           // Offset from target (world space)
    const vec3_t *follow_target;    // Pointer to tracked position
    float follow_smoothing;         // Lerp factor per frame (0=instant, 0.9=smooth)

    // Fixed mode state
    vec3_t fixed_position;
    vec3_t fixed_target;

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

    // Camera collision
    bool collision_enabled;
    const struct CollisionWorld *collision_world;
    uint16_t collision_mask;    // Which layers to test against (raycast)
    float collision_radius;     // Camera sphere radius for overlap pushout (0 = disabled)
    float min_y;                // Hard Y floor clamp (applied when collision enabled)

    bool dirty;
} Camera;

// Scene/level camera preset
typedef struct {
    CameraMode mode;
    float azimuth;
    float elevation;
    float distance;
    vec3_t target;
    float fov_y;
    float near_plane;
    float far_plane;
    // Follow mode defaults
    vec3_t follow_offset;
    float follow_smoothing;
    // Fixed mode defaults
    vec3_t fixed_position;
    vec3_t fixed_target;
} CameraConfig;

extern const CameraConfig CAMERA_DEFAULT;

void camera_init(Camera *cam, const CameraConfig *config);
void camera_orbit(Camera *cam, float d_azimuth, float d_elevation);
void camera_zoom(Camera *cam, float d_distance);
void camera_shift_target_y(Camera *cam, float d_y);
void camera_update(Camera *cam);

// Mode switching
void camera_set_mode(Camera *cam, CameraMode mode);
void camera_set_follow_target(Camera *cam, const vec3_t *target, vec3_t offset);
void camera_set_fixed(Camera *cam, vec3_t position, vec3_t target);
void camera_set_collision(Camera *cam, const struct CollisionWorld *world, uint16_t mask);

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
