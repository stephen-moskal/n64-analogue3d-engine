#ifndef COLLISION_H
#define COLLISION_H

#include <stdbool.h>
#include <stdint.h>
#include "../math/vec3.h"

// --- Limits ---

#define COLLISION_MAX_COLLIDERS  64
#define COLLISION_MAX_RESULTS    32
#define COLLISION_LAYER_DEFAULT  0x0001
#define COLLISION_LAYER_ENV      0x0002  // Environment/geometry (walls, floors)
#define COLLISION_LAYER_ALL      0xFFFF

// --- Shape types ---

typedef enum {
    COLLIDER_SPHERE,
    COLLIDER_AABB,
} ColliderType;

typedef struct {
    vec3_t center;
    float radius;
} ColliderSphere;

typedef struct {
    vec3_t min;
    vec3_t max;
} ColliderAABB;

typedef struct {
    vec3_t origin;
    vec3_t direction;     // Must be normalized
    float max_distance;
} Ray;

// --- Collision result ---

typedef struct {
    bool hit;
    vec3_t point;         // Contact point (world space)
    vec3_t normal;        // Contact normal (from B toward A)
    float depth;          // Penetration depth (overlap tests)
    float distance;       // Hit distance (raycasts)
    int id_a;             // Collider handle A
    int id_b;             // Collider handle B
} CollisionResult;

// --- Collider ---

typedef struct {
    ColliderType type;
    union {
        ColliderSphere sphere;
        ColliderAABB aabb;
    } shape;
    ColliderAABB bounds;  // World-space AABB for broadphase
    uint16_t layer;       // What layer this collider is on (bitmask)
    uint16_t mask;        // What layers it tests against (bitmask)
    bool is_static;       // Static-static pairs are skipped
    bool is_trigger;      // Triggers report overlap but no physical response
    bool active;          // Slot in use
    void *userdata;       // Pointer back to game object (opaque)
} Collider;

// --- Collision world ---

typedef struct CollisionWorld {
    Collider colliders[COLLISION_MAX_COLLIDERS];
    int count;
    CollisionResult results[COLLISION_MAX_RESULTS];
    int result_count;
} CollisionWorld;

// --- World lifecycle ---
void collision_world_init(CollisionWorld *world);

// --- Collider management ---
// Returns handle (index) or -1 if full
int  collision_add_sphere(CollisionWorld *world, vec3_t center, float radius,
                          uint16_t layer, uint16_t mask, void *userdata);
int  collision_add_aabb(CollisionWorld *world, vec3_t min, vec3_t max,
                        uint16_t layer, uint16_t mask, void *userdata);
void collision_remove(CollisionWorld *world, int handle);
void collision_set_static(CollisionWorld *world, int handle, bool is_static);
void collision_set_trigger(CollisionWorld *world, int handle, bool is_trigger);

// --- Position updates ---
void collision_update_sphere(CollisionWorld *world, int handle, vec3_t center);
void collision_update_aabb(CollisionWorld *world, int handle, vec3_t min, vec3_t max);

// --- Queries ---
// Test all pairs, fill results buffer. Returns number of collisions.
int  collision_test_all(CollisionWorld *world);

// Test a single pair
bool collision_test_pair(const Collider *a, const Collider *b, CollisionResult *out);

// Overlap query: find all colliders overlapping a sphere
int  collision_overlap_sphere(const CollisionWorld *world, vec3_t center, float radius,
                              uint16_t mask, int *out_handles, int max_results);

// --- Raycasting ---
// Cast ray against all colliders matching mask. Returns closest hit.
bool collision_raycast(const CollisionWorld *world, const Ray *ray,
                       uint16_t mask, CollisionResult *out);

// Cast ray against a single collider
bool collision_raycast_single(const Collider *collider, const Ray *ray,
                              CollisionResult *out);

// --- Individual shape tests (usable standalone) ---
bool collision_sphere_sphere(const ColliderSphere *a, const ColliderSphere *b,
                             CollisionResult *out);
bool collision_aabb_aabb(const ColliderAABB *a, const ColliderAABB *b,
                         CollisionResult *out);
bool collision_sphere_aabb(const ColliderSphere *s, const ColliderAABB *b,
                           CollisionResult *out);
bool collision_ray_sphere(const Ray *ray, const ColliderSphere *sphere,
                          CollisionResult *out);
bool collision_ray_aabb(const Ray *ray, const ColliderAABB *aabb,
                        CollisionResult *out);
bool collision_ray_triangle(const Ray *ray, const vec3_t *v0, const vec3_t *v1,
                            const vec3_t *v2, CollisionResult *out);

// --- Utility ---
ColliderAABB collision_aabb_from_points(const vec3_t *points, int count);
ColliderSphere collision_sphere_from_aabb(const ColliderAABB *aabb);

#endif
