#ifndef VEC3_H
#define VEC3_H

#include <math.h>

typedef struct { float x, y, z; } vec3_t;

static inline vec3_t vec3_add(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){a->x + b->x, a->y + b->y, a->z + b->z};
}

static inline vec3_t vec3_sub(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){a->x - b->x, a->y - b->y, a->z - b->z};
}

static inline vec3_t vec3_scale(const vec3_t *v, float s) {
    return (vec3_t){v->x * s, v->y * s, v->z * s};
}

static inline vec3_t vec3_negate(const vec3_t *v) {
    return (vec3_t){-v->x, -v->y, -v->z};
}

static inline float vec3_dot(const vec3_t *a, const vec3_t *b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

static inline vec3_t vec3_cross(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){
        a->y * b->z - a->z * b->y,
        a->z * b->x - a->x * b->z,
        a->x * b->y - a->y * b->x
    };
}

static inline float vec3_length_sq(const vec3_t *v) {
    return v->x * v->x + v->y * v->y + v->z * v->z;
}

static inline float vec3_length(const vec3_t *v) {
    return sqrtf(vec3_length_sq(v));
}

static inline float vec3_distance_sq(const vec3_t *a, const vec3_t *b) {
    vec3_t d = vec3_sub(a, b);
    return vec3_length_sq(&d);
}

static inline float vec3_distance(const vec3_t *a, const vec3_t *b) {
    return sqrtf(vec3_distance_sq(a, b));
}

static inline vec3_t vec3_normalize(const vec3_t *v) {
    float len = vec3_length(v);
    if (len < 0.0001f) return (vec3_t){0.0f, 0.0f, 0.0f};
    float inv = 1.0f / len;
    return (vec3_t){v->x * inv, v->y * inv, v->z * inv};
}

static inline vec3_t vec3_min(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){
        a->x < b->x ? a->x : b->x,
        a->y < b->y ? a->y : b->y,
        a->z < b->z ? a->z : b->z
    };
}

static inline vec3_t vec3_max(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){
        a->x > b->x ? a->x : b->x,
        a->y > b->y ? a->y : b->y,
        a->z > b->z ? a->z : b->z
    };
}

static inline vec3_t vec3_lerp(const vec3_t *a, const vec3_t *b, float t) {
    return (vec3_t){
        a->x + (b->x - a->x) * t,
        a->y + (b->y - a->y) * t,
        a->z + (b->z - a->z) * t
    };
}

static inline float vec3_clampf(float val, float lo, float hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// --- Constants ---

#define VEC3_ZERO    ((vec3_t){0, 0, 0})
#define VEC3_ONE     ((vec3_t){1, 1, 1})
#define VEC3_UP      ((vec3_t){0, 1, 0})
#define VEC3_DOWN    ((vec3_t){0, -1, 0})
#define VEC3_RIGHT   ((vec3_t){1, 0, 0})
#define VEC3_FORWARD ((vec3_t){0, 0, -1})

// --- Physics math utilities ---

static inline vec3_t vec3_mul(const vec3_t *a, const vec3_t *b) {
    return (vec3_t){a->x * b->x, a->y * b->y, a->z * b->z};
}

static inline vec3_t vec3_reflect(const vec3_t *incident, const vec3_t *normal) {
    float d = 2.0f * vec3_dot(incident, normal);
    return (vec3_t){
        incident->x - d * normal->x,
        incident->y - d * normal->y,
        incident->z - d * normal->z
    };
}

static inline vec3_t vec3_project(const vec3_t *a, const vec3_t *b) {
    float d = vec3_dot(a, b);
    float len_sq = vec3_length_sq(b);
    if (len_sq < 0.0001f) return VEC3_ZERO;
    float s = d / len_sq;
    return (vec3_t){b->x * s, b->y * s, b->z * s};
}

static inline vec3_t vec3_reject(const vec3_t *a, const vec3_t *b) {
    vec3_t proj = vec3_project(a, b);
    return vec3_sub(a, &proj);
}

static inline vec3_t vec3_clamp_length(const vec3_t *v, float max_length) {
    float len_sq = vec3_length_sq(v);
    if (len_sq <= max_length * max_length) return *v;
    float inv = max_length / sqrtf(len_sq);
    return (vec3_t){v->x * inv, v->y * inv, v->z * inv};
}

static inline vec3_t vec3_move_toward(const vec3_t *current, const vec3_t *target, float max_delta) {
    vec3_t diff = vec3_sub(target, current);
    float dist_sq = vec3_length_sq(&diff);
    if (dist_sq <= max_delta * max_delta || dist_sq < 0.0001f) return *target;
    float inv = max_delta / sqrtf(dist_sq);
    return (vec3_t){
        current->x + diff.x * inv,
        current->y + diff.y * inv,
        current->z + diff.z * inv
    };
}

static inline vec3_t vec3_abs(const vec3_t *v) {
    return (vec3_t){
        v->x < 0 ? -v->x : v->x,
        v->y < 0 ? -v->y : v->y,
        v->z < 0 ? -v->z : v->z
    };
}

static inline vec3_t vec3_sign(const vec3_t *v) {
    return (vec3_t){
        v->x > 0 ? 1.0f : (v->x < 0 ? -1.0f : 0.0f),
        v->y > 0 ? 1.0f : (v->y < 0 ? -1.0f : 0.0f),
        v->z > 0 ? 1.0f : (v->z < 0 ? -1.0f : 0.0f)
    };
}

#endif
