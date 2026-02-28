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

#endif
