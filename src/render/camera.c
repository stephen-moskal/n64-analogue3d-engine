#include "camera.h"
#include "../collision/collision.h"
#include <math.h>
#include <string.h>

#define MAX_ELEVATION  1.4835f   // ~85 degrees
#define MIN_DISTANCE   100.0f
#define MAX_DISTANCE   1500.0f
#define COLLISION_OFFSET 5.0f    // Pull camera forward from hit point

const CameraConfig CAMERA_DEFAULT = {
    .mode       = CAMERA_MODE_ORBITAL,
    .azimuth    = 0.0f,
    .elevation  = 0.3f,
    .distance   = 300.0f,
    .target     = {0.0f, 0.0f, 0.0f},
    .fov_y      = 1.0472f,     // 60 degrees
    .near_plane = 20.0f,
    .far_plane  = 2000.0f,
    .follow_offset    = {0.0f, 200.0f, -300.0f},
    .follow_smoothing = 0.1f,
    .fixed_position   = {0.0f, 200.0f, 300.0f},
    .fixed_target     = {0.0f, 0.0f, 0.0f},
};

// vec3 functions now in math/vec3.h (included via camera.h)

// --- Matrix utilities ---

void mat4_perspective(mat4_t *out, float fov_y, float aspect,
                      float near, float far) {
    float f = 1.0f / tanf(fov_y * 0.5f);
    float nf = 1.0f / (near - far);

    memset(out, 0, sizeof(mat4_t));
    out->m[0][0] = f / aspect;
    out->m[1][1] = f;
    out->m[2][2] = (far + near) * nf;
    out->m[2][3] = -1.0f;
    out->m[3][2] = 2.0f * far * near * nf;
}

void mat4_lookat(mat4_t *out, const vec3_t *eye, const vec3_t *target,
                 const vec3_t *up) {
    vec3_t f = vec3_sub(target, eye);
    f = vec3_normalize(&f);

    vec3_t s = vec3_cross(&f, up);
    s = vec3_normalize(&s);

    vec3_t u = vec3_cross(&s, &f);

    memset(out, 0, sizeof(mat4_t));

    // Column-major: m[col][row]
    out->m[0][0] =  s.x;
    out->m[0][1] =  u.x;
    out->m[0][2] = -f.x;

    out->m[1][0] =  s.y;
    out->m[1][1] =  u.y;
    out->m[1][2] = -f.y;

    out->m[2][0] =  s.z;
    out->m[2][1] =  u.z;
    out->m[2][2] = -f.z;

    out->m[3][0] = -vec3_dot(&s, eye);
    out->m[3][1] = -vec3_dot(&u, eye);
    out->m[3][2] =  vec3_dot(&f, eye);
    out->m[3][3] =  1.0f;
}

void mat4_mul(mat4_t *out, const mat4_t *a, const mat4_t *b) {
    mat4_t tmp;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            tmp.m[col][row] = a->m[0][row] * b->m[col][0] +
                              a->m[1][row] * b->m[col][1] +
                              a->m[2][row] * b->m[col][2] +
                              a->m[3][row] * b->m[col][3];
        }
    }
    *out = tmp;
}

void mat4_mul_vec3(vec4_t *out, const mat4_t *m, const vec3_t *v) {
    // Treat vec3 as vec4 with w=1
    out->x = m->m[0][0] * v->x + m->m[1][0] * v->y + m->m[2][0] * v->z + m->m[3][0];
    out->y = m->m[0][1] * v->x + m->m[1][1] * v->y + m->m[2][1] * v->z + m->m[3][1];
    out->z = m->m[0][2] * v->x + m->m[1][2] * v->y + m->m[2][2] * v->z + m->m[3][2];
    out->w = m->m[0][3] * v->x + m->m[1][3] * v->y + m->m[2][3] * v->z + m->m[3][3];
}

void mat4_from_srt(mat4_t *out, const vec3_t *scale, float rx, float ry, float rz,
                   const vec3_t *translate) {
    float cx = cosf(rx), sx = sinf(rx);
    float cy = cosf(ry), sy = sinf(ry);
    float cz = cosf(rz), sz = sinf(rz);

    // R = Ry * Rx * Rz, then scale each column, then set translation
    // Column-major: m[col][row]
    out->m[0][0] = scale->x * (cy * cz + sy * sx * sz);
    out->m[0][1] = scale->x * (cx * sz);
    out->m[0][2] = scale->x * (-sy * cz + cy * sx * sz);
    out->m[0][3] = 0.0f;

    out->m[1][0] = scale->y * (-cy * sz + sy * sx * cz);
    out->m[1][1] = scale->y * (cx * cz);
    out->m[1][2] = scale->y * (sy * sz + cy * sx * cz);
    out->m[1][3] = 0.0f;

    out->m[2][0] = scale->z * (sy * cx);
    out->m[2][1] = scale->z * (-sx);
    out->m[2][2] = scale->z * (cy * cx);
    out->m[2][3] = 0.0f;

    out->m[3][0] = translate->x;
    out->m[3][1] = translate->y;
    out->m[3][2] = translate->z;
    out->m[3][3] = 1.0f;
}

// --- Frustum extraction ---

static void extract_frustum(const mat4_t *vp, float frustum[6][4]) {
    // Extract rows from column-major VP for Griess-Hartmann method
    float row[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            row[i][j] = vp->m[j][i];

    for (int j = 0; j < 4; j++) {
        frustum[FRUSTUM_LEFT][j]   = row[3][j] + row[0][j];
        frustum[FRUSTUM_RIGHT][j]  = row[3][j] - row[0][j];
        frustum[FRUSTUM_BOTTOM][j] = row[3][j] + row[1][j];
        frustum[FRUSTUM_TOP][j]    = row[3][j] - row[1][j];
        frustum[FRUSTUM_NEAR][j]   = row[3][j] + row[2][j];
        frustum[FRUSTUM_FAR][j]    = row[3][j] - row[2][j];
    }

    // Normalize each plane
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(frustum[i][0] * frustum[i][0] +
                          frustum[i][1] * frustum[i][1] +
                          frustum[i][2] * frustum[i][2]);
        if (len > 0.0001f) {
            float inv = 1.0f / len;
            for (int j = 0; j < 4; j++)
                frustum[i][j] *= inv;
        }
    }
}

// --- Camera API ---

void camera_init(Camera *cam, const CameraConfig *config) {
    cam->mode       = config->mode;
    cam->azimuth    = config->azimuth;
    cam->elevation  = config->elevation;
    cam->distance   = config->distance;
    cam->target     = config->target;
    cam->fov_y      = config->fov_y;
    cam->aspect     = 320.0f / 240.0f;
    cam->near_plane = config->near_plane;
    cam->far_plane  = config->far_plane;
    cam->up         = (vec3_t){0.0f, 1.0f, 0.0f};

    // Follow mode
    cam->follow_offset    = config->follow_offset;
    cam->follow_target    = NULL;
    cam->follow_smoothing = config->follow_smoothing;

    // Fixed mode
    cam->fixed_position = config->fixed_position;
    cam->fixed_target   = config->fixed_target;

    // Collision
    cam->collision_enabled = false;
    cam->collision_world   = NULL;
    cam->collision_mask    = 0xFFFF;
    cam->collision_radius  = 0.0f;
    cam->min_y             = -1e18f;

    cam->dirty = true;
    camera_update(cam);
}

void camera_orbit(Camera *cam, float d_azimuth, float d_elevation) {
    cam->azimuth   += d_azimuth;
    cam->elevation += d_elevation;
    cam->dirty = true;
}

void camera_zoom(Camera *cam, float d_distance) {
    cam->distance += d_distance;
    cam->dirty = true;
}

void camera_shift_target_y(Camera *cam, float d_y) {
    cam->target.y += d_y;
    cam->dirty = true;
}

// --- Camera collision helper ---

static void apply_camera_collision(Camera *cam, const vec3_t *look_at) {
    if (!cam->collision_enabled || !cam->collision_world) return;

    // --- Layer 1: Raycast from look-at toward camera (line-of-sight) ---
    // Prevents camera from being behind walls/floors relative to target.
    // Tests against collision_mask (typically ENV layer).
    vec3_t to_cam = vec3_sub(&cam->position, look_at);
    float cam_dist = vec3_length(&to_cam);
    if (cam_dist > 1.0f) {
        vec3_t ray_dir = vec3_scale(&to_cam, 1.0f / cam_dist);
        Ray ray;
        ray.origin = *look_at;
        ray.direction = ray_dir;
        ray.max_distance = cam_dist;

        CollisionResult result;
        if (collision_raycast(cam->collision_world, &ray,
                              cam->collision_mask, &result)) {
            float safe_dist = result.distance - COLLISION_OFFSET;
            if (safe_dist < cam->near_plane) safe_dist = cam->near_plane;
            vec3_t offset = vec3_scale(&ray_dir, safe_dist);
            cam->position = vec3_add(look_at, &offset);
        }
    }

    // --- Layer 2: Sphere overlap pushout (sphere colliders only) ---
    // Prevents camera from being inside bounding spheres (e.g., cube).
    // Tests against ALL sphere colliders regardless of layer mask.
    // Skips AABBs — their pushout direction is ambiguous for wide boxes.
    if (cam->collision_radius > 0.0f) {
        ColliderSphere cam_sphere = {cam->position, cam->collision_radius};

        for (int i = 0; i < COLLISION_MAX_COLLIDERS; i++) {
            const Collider *c = &cam->collision_world->colliders[i];
            if (!c->active) continue;
            if (c->type != COLLIDER_SPHERE) continue;

            CollisionResult result;
            if (collision_sphere_sphere(&cam_sphere, &c->shape.sphere, &result)) {
                vec3_t push = vec3_scale(&result.normal, result.depth);
                cam->position = vec3_add(&cam->position, &push);
                cam_sphere.center = cam->position;
            }
        }
    }

    // --- Layer 3: Hard Y floor clamp ---
    // Simpler and more robust than AABB overlap for a flat ground plane.
    if (cam->position.y < cam->min_y) {
        cam->position.y = cam->min_y;
    }
}

// --- Mode switching ---

void camera_set_mode(Camera *cam, CameraMode mode) {
    cam->mode = mode;
    cam->dirty = true;
}

void camera_set_follow_target(Camera *cam, const vec3_t *target, vec3_t offset) {
    cam->follow_target = target;
    cam->follow_offset = offset;
    cam->mode = CAMERA_MODE_FOLLOW;
    cam->dirty = true;
}

void camera_set_fixed(Camera *cam, vec3_t position, vec3_t target) {
    cam->fixed_position = position;
    cam->fixed_target = target;
    cam->mode = CAMERA_MODE_FIXED;
    cam->dirty = true;
}

void camera_set_collision(Camera *cam, const struct CollisionWorld *world, uint16_t mask) {
    cam->collision_world = world;
    cam->collision_mask = mask;
    cam->collision_enabled = (world != NULL);
}

// --- Update ---

void camera_update(Camera *cam) {
    if (!cam->dirty) return;

    vec3_t look_at;

    switch (cam->mode) {
    case CAMERA_MODE_ORBITAL:
        // Clamp orbital parameters
        if (cam->elevation > MAX_ELEVATION) cam->elevation = MAX_ELEVATION;
        if (cam->elevation < -MAX_ELEVATION) cam->elevation = -MAX_ELEVATION;
        if (cam->distance < MIN_DISTANCE) cam->distance = MIN_DISTANCE;
        if (cam->distance > MAX_DISTANCE) cam->distance = MAX_DISTANCE;

        // Compute camera position from spherical coordinates
        {
            float cos_elev = cosf(cam->elevation);
            cam->position.x = cam->target.x + cam->distance * cos_elev * sinf(cam->azimuth);
            cam->position.y = cam->target.y + cam->distance * sinf(cam->elevation);
            cam->position.z = cam->target.z + cam->distance * cos_elev * cosf(cam->azimuth);
        }
        look_at = cam->target;
        break;

    case CAMERA_MODE_FIXED:
        cam->position = cam->fixed_position;
        look_at = cam->fixed_target;
        break;

    case CAMERA_MODE_FOLLOW:
        if (cam->follow_target) {
            vec3_t desired = vec3_add(cam->follow_target, &cam->follow_offset);
            // Smooth interpolation toward desired position
            float t = 1.0f - cam->follow_smoothing;
            cam->position = vec3_lerp(&cam->position, &desired, t);
            look_at = *cam->follow_target;
        } else {
            // No target set, fall back to current position
            look_at = cam->target;
        }
        break;

    default:
        look_at = cam->target;
        break;
    }

    // Apply camera collision (all modes)
    apply_camera_collision(cam, &look_at);

    // View direction (camera toward target, normalized)
    vec3_t diff = vec3_sub(&look_at, &cam->position);
    cam->view_dir = vec3_normalize(&diff);

    // Build view matrix
    cam->up = (vec3_t){0.0f, 1.0f, 0.0f};
    mat4_lookat(&cam->view, &cam->position, &look_at, &cam->up);

    // Build projection matrix
    mat4_perspective(&cam->proj, cam->fov_y, cam->aspect,
                     cam->near_plane, cam->far_plane);

    // Combined VP = proj * view
    mat4_mul(&cam->vp, &cam->proj, &cam->view);

    // Extract frustum planes
    extract_frustum(&cam->vp, cam->frustum);

    cam->dirty = false;
}

bool camera_sphere_visible(const Camera *cam, const vec3_t *center,
                           float radius) {
    for (int i = 0; i < 6; i++) {
        float dist = cam->frustum[i][0] * center->x +
                     cam->frustum[i][1] * center->y +
                     cam->frustum[i][2] * center->z +
                     cam->frustum[i][3];
        if (dist < -radius) return false;
    }
    return true;
}
