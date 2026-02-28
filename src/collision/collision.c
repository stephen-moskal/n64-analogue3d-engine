#include "collision.h"
#include <string.h>
#include <math.h>

// --- Internal helpers ---

// Quick AABB overlap test (broadphase) — 6 comparisons
static inline bool aabb_overlap(const ColliderAABB *a, const ColliderAABB *b) {
    return a->min.x <= b->max.x && a->max.x >= b->min.x &&
           a->min.y <= b->max.y && a->max.y >= b->min.y &&
           a->min.z <= b->max.z && a->max.z >= b->min.z;
}

// Compute broadphase AABB for a sphere
static inline ColliderAABB sphere_to_aabb(const ColliderSphere *s) {
    return (ColliderAABB){
        .min = {s->center.x - s->radius, s->center.y - s->radius, s->center.z - s->radius},
        .max = {s->center.x + s->radius, s->center.y + s->radius, s->center.z + s->radius},
    };
}

// --- World lifecycle ---

void collision_world_init(CollisionWorld *world) {
    memset(world, 0, sizeof(CollisionWorld));
}

// --- Collider management ---

static int find_free_slot(CollisionWorld *world) {
    for (int i = 0; i < COLLISION_MAX_COLLIDERS; i++) {
        if (!world->colliders[i].active) return i;
    }
    return -1;
}

int collision_add_sphere(CollisionWorld *world, vec3_t center, float radius,
                         uint16_t layer, uint16_t mask, void *userdata) {
    int slot = find_free_slot(world);
    if (slot < 0) return -1;

    Collider *c = &world->colliders[slot];
    c->type = COLLIDER_SPHERE;
    c->shape.sphere.center = center;
    c->shape.sphere.radius = radius;
    c->bounds = sphere_to_aabb(&c->shape.sphere);
    c->layer = layer;
    c->mask = mask;
    c->is_static = false;
    c->is_trigger = false;
    c->active = true;
    c->userdata = userdata;
    world->count++;
    return slot;
}

int collision_add_aabb(CollisionWorld *world, vec3_t min, vec3_t max,
                       uint16_t layer, uint16_t mask, void *userdata) {
    int slot = find_free_slot(world);
    if (slot < 0) return -1;

    Collider *c = &world->colliders[slot];
    c->type = COLLIDER_AABB;
    c->shape.aabb.min = min;
    c->shape.aabb.max = max;
    c->bounds = c->shape.aabb;  // Broadphase AABB = shape AABB
    c->layer = layer;
    c->mask = mask;
    c->is_static = false;
    c->is_trigger = false;
    c->active = true;
    c->userdata = userdata;
    world->count++;
    return slot;
}

void collision_remove(CollisionWorld *world, int handle) {
    if (handle < 0 || handle >= COLLISION_MAX_COLLIDERS) return;
    if (!world->colliders[handle].active) return;
    world->colliders[handle].active = false;
    world->count--;
}

void collision_set_static(CollisionWorld *world, int handle, bool is_static) {
    if (handle < 0 || handle >= COLLISION_MAX_COLLIDERS) return;
    world->colliders[handle].is_static = is_static;
}

void collision_set_trigger(CollisionWorld *world, int handle, bool is_trigger) {
    if (handle < 0 || handle >= COLLISION_MAX_COLLIDERS) return;
    world->colliders[handle].is_trigger = is_trigger;
}

// --- Position updates ---

void collision_update_sphere(CollisionWorld *world, int handle, vec3_t center) {
    if (handle < 0 || handle >= COLLISION_MAX_COLLIDERS) return;
    Collider *c = &world->colliders[handle];
    c->shape.sphere.center = center;
    c->bounds = sphere_to_aabb(&c->shape.sphere);
}

void collision_update_aabb(CollisionWorld *world, int handle, vec3_t min, vec3_t max) {
    if (handle < 0 || handle >= COLLISION_MAX_COLLIDERS) return;
    Collider *c = &world->colliders[handle];
    c->shape.aabb.min = min;
    c->shape.aabb.max = max;
    c->bounds = c->shape.aabb;
}

// --- Individual shape tests ---

bool collision_sphere_sphere(const ColliderSphere *a, const ColliderSphere *b,
                             CollisionResult *out) {
    float dist_sq = vec3_distance_sq(&a->center, &b->center);
    float sum_r = a->radius + b->radius;

    if (dist_sq >= sum_r * sum_r) {
        if (out) out->hit = false;
        return false;
    }

    if (out) {
        out->hit = true;
        float dist = sqrtf(dist_sq);
        out->depth = sum_r - dist;

        if (dist > 0.0001f) {
            vec3_t diff = vec3_sub(&a->center, &b->center);
            out->normal = vec3_scale(&diff, 1.0f / dist);
        } else {
            out->normal = (vec3_t){0.0f, 1.0f, 0.0f};
        }

        // Contact point: midpoint of overlap on the line between centers
        vec3_t offset = vec3_scale(&out->normal, b->radius - out->depth * 0.5f);
        out->point = vec3_add(&b->center, &offset);
        out->distance = 0.0f;
    }
    return true;
}

bool collision_aabb_aabb(const ColliderAABB *a, const ColliderAABB *b,
                         CollisionResult *out) {
    // Separating axis test
    if (a->min.x > b->max.x || a->max.x < b->min.x ||
        a->min.y > b->max.y || a->max.y < b->min.y ||
        a->min.z > b->max.z || a->max.z < b->min.z) {
        if (out) out->hit = false;
        return false;
    }

    if (out) {
        out->hit = true;

        // Find axis of minimum penetration
        float overlaps[6];
        overlaps[0] = a->max.x - b->min.x;  // +X
        overlaps[1] = b->max.x - a->min.x;  // -X
        overlaps[2] = a->max.y - b->min.y;  // +Y
        overlaps[3] = b->max.y - a->min.y;  // -Y
        overlaps[4] = a->max.z - b->min.z;  // +Z
        overlaps[5] = b->max.z - a->min.z;  // -Z

        int min_axis = 0;
        float min_overlap = overlaps[0];
        for (int i = 1; i < 6; i++) {
            if (overlaps[i] < min_overlap) {
                min_overlap = overlaps[i];
                min_axis = i;
            }
        }

        out->depth = min_overlap;

        // Normal points from B toward A along minimum penetration axis
        out->normal = (vec3_t){0, 0, 0};
        switch (min_axis) {
            case 0: out->normal.x =  1.0f; break;
            case 1: out->normal.x = -1.0f; break;
            case 2: out->normal.y =  1.0f; break;
            case 3: out->normal.y = -1.0f; break;
            case 4: out->normal.z =  1.0f; break;
            case 5: out->normal.z = -1.0f; break;
        }

        // Contact point: center of overlap region
        vec3_t overlap_min = vec3_max(&a->min, &b->min);
        vec3_t overlap_max = vec3_min(&a->max, &b->max);
        out->point = vec3_lerp(&overlap_min, &overlap_max, 0.5f);
        out->distance = 0.0f;
    }
    return true;
}

bool collision_sphere_aabb(const ColliderSphere *s, const ColliderAABB *b,
                           CollisionResult *out) {
    // Find closest point on AABB to sphere center
    vec3_t closest;
    closest.x = vec3_clampf(s->center.x, b->min.x, b->max.x);
    closest.y = vec3_clampf(s->center.y, b->min.y, b->max.y);
    closest.z = vec3_clampf(s->center.z, b->min.z, b->max.z);

    float dist_sq = vec3_distance_sq(&s->center, &closest);

    if (dist_sq >= s->radius * s->radius) {
        if (out) out->hit = false;
        return false;
    }

    if (out) {
        out->hit = true;
        float dist = sqrtf(dist_sq);
        out->depth = s->radius - dist;

        if (dist > 0.0001f) {
            vec3_t diff = vec3_sub(&s->center, &closest);
            out->normal = vec3_scale(&diff, 1.0f / dist);
        } else {
            // Sphere center is inside AABB — push out along shortest axis
            float dx_min = s->center.x - b->min.x;
            float dx_max = b->max.x - s->center.x;
            float dy_min = s->center.y - b->min.y;
            float dy_max = b->max.y - s->center.y;
            float dz_min = s->center.z - b->min.z;
            float dz_max = b->max.z - s->center.z;

            float min_d = dx_min;
            out->normal = (vec3_t){-1, 0, 0};

            if (dx_max < min_d) { min_d = dx_max; out->normal = (vec3_t){1, 0, 0}; }
            if (dy_min < min_d) { min_d = dy_min; out->normal = (vec3_t){0, -1, 0}; }
            if (dy_max < min_d) { min_d = dy_max; out->normal = (vec3_t){0, 1, 0}; }
            if (dz_min < min_d) { min_d = dz_min; out->normal = (vec3_t){0, 0, -1}; }
            if (dz_max < min_d) { min_d = dz_max; out->normal = (vec3_t){0, 0, 1}; }

            out->depth = s->radius + min_d;
        }

        out->point = closest;
        out->distance = 0.0f;
    }
    return true;
}

// --- Raycasting ---

bool collision_ray_sphere(const Ray *ray, const ColliderSphere *sphere,
                          CollisionResult *out) {
    vec3_t oc = vec3_sub(&ray->origin, &sphere->center);
    float b = vec3_dot(&oc, &ray->direction);
    float c = vec3_dot(&oc, &oc) - sphere->radius * sphere->radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0f) {
        if (out) out->hit = false;
        return false;
    }

    float sqrt_d = sqrtf(discriminant);
    float t = -b - sqrt_d;  // Near intersection

    // If near intersection is behind ray, try far intersection
    if (t < 0.0f) {
        t = -b + sqrt_d;
        if (t < 0.0f) {
            if (out) out->hit = false;
            return false;
        }
    }

    if (t > ray->max_distance) {
        if (out) out->hit = false;
        return false;
    }

    if (out) {
        out->hit = true;
        out->distance = t;
        vec3_t scaled_dir = vec3_scale(&ray->direction, t);
        out->point = vec3_add(&ray->origin, &scaled_dir);
        vec3_t diff = vec3_sub(&out->point, &sphere->center);
        out->normal = vec3_normalize(&diff);
        out->depth = 0.0f;
    }
    return true;
}

bool collision_ray_aabb(const Ray *ray, const ColliderAABB *aabb,
                        CollisionResult *out) {
    // Slab method (Kay-Kajiya)
    float tmin = 0.0f;
    float tmax = ray->max_distance;
    int hit_axis = 0;
    bool hit_min_side = true;

    // X slab
    if (fabsf(ray->direction.x) > 0.0001f) {
        float inv_d = 1.0f / ray->direction.x;
        float t1 = (aabb->min.x - ray->origin.x) * inv_d;
        float t2 = (aabb->max.x - ray->origin.x) * inv_d;
        bool swap = false;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; swap = true; }
        if (t1 > tmin) { tmin = t1; hit_axis = 0; hit_min_side = !swap; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) { if (out) out->hit = false; return false; }
    } else {
        if (ray->origin.x < aabb->min.x || ray->origin.x > aabb->max.x) {
            if (out) out->hit = false;
            return false;
        }
    }

    // Y slab
    if (fabsf(ray->direction.y) > 0.0001f) {
        float inv_d = 1.0f / ray->direction.y;
        float t1 = (aabb->min.y - ray->origin.y) * inv_d;
        float t2 = (aabb->max.y - ray->origin.y) * inv_d;
        bool swap = false;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; swap = true; }
        if (t1 > tmin) { tmin = t1; hit_axis = 1; hit_min_side = !swap; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) { if (out) out->hit = false; return false; }
    } else {
        if (ray->origin.y < aabb->min.y || ray->origin.y > aabb->max.y) {
            if (out) out->hit = false;
            return false;
        }
    }

    // Z slab
    if (fabsf(ray->direction.z) > 0.0001f) {
        float inv_d = 1.0f / ray->direction.z;
        float t1 = (aabb->min.z - ray->origin.z) * inv_d;
        float t2 = (aabb->max.z - ray->origin.z) * inv_d;
        bool swap = false;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; swap = true; }
        if (t1 > tmin) { tmin = t1; hit_axis = 2; hit_min_side = !swap; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) { if (out) out->hit = false; return false; }
    } else {
        if (ray->origin.z < aabb->min.z || ray->origin.z > aabb->max.z) {
            if (out) out->hit = false;
            return false;
        }
    }

    if (out) {
        out->hit = true;
        out->distance = tmin;
        vec3_t scaled_dir = vec3_scale(&ray->direction, tmin);
        out->point = vec3_add(&ray->origin, &scaled_dir);
        out->normal = (vec3_t){0, 0, 0};
        float sign = hit_min_side ? -1.0f : 1.0f;
        switch (hit_axis) {
            case 0: out->normal.x = sign; break;
            case 1: out->normal.y = sign; break;
            case 2: out->normal.z = sign; break;
        }
        out->depth = 0.0f;
    }
    return true;
}

bool collision_ray_triangle(const Ray *ray, const vec3_t *v0, const vec3_t *v1,
                            const vec3_t *v2, CollisionResult *out) {
    // Moller-Trumbore algorithm
    vec3_t edge1 = vec3_sub(v1, v0);
    vec3_t edge2 = vec3_sub(v2, v0);

    vec3_t h = vec3_cross(&ray->direction, &edge2);
    float a = vec3_dot(&edge1, &h);

    // Ray parallel to triangle
    if (fabsf(a) < 0.0001f) {
        if (out) out->hit = false;
        return false;
    }

    float f = 1.0f / a;
    vec3_t s = vec3_sub(&ray->origin, v0);
    float u = f * vec3_dot(&s, &h);

    if (u < 0.0f || u > 1.0f) {
        if (out) out->hit = false;
        return false;
    }

    vec3_t q = vec3_cross(&s, &edge1);
    float v = f * vec3_dot(&ray->direction, &q);

    if (v < 0.0f || u + v > 1.0f) {
        if (out) out->hit = false;
        return false;
    }

    float t = f * vec3_dot(&edge2, &q);

    if (t < 0.0f || t > ray->max_distance) {
        if (out) out->hit = false;
        return false;
    }

    if (out) {
        out->hit = true;
        out->distance = t;
        vec3_t scaled_dir = vec3_scale(&ray->direction, t);
        out->point = vec3_add(&ray->origin, &scaled_dir);
        out->normal = vec3_cross(&edge1, &edge2);
        out->normal = vec3_normalize(&out->normal);
        // Ensure normal faces toward ray origin
        if (vec3_dot(&out->normal, &ray->direction) > 0.0f) {
            out->normal = vec3_negate(&out->normal);
        }
        out->depth = 0.0f;
    }
    return true;
}

// --- Pair test dispatcher ---

bool collision_test_pair(const Collider *a, const Collider *b, CollisionResult *out) {
    if (a->type == COLLIDER_SPHERE && b->type == COLLIDER_SPHERE) {
        return collision_sphere_sphere(&a->shape.sphere, &b->shape.sphere, out);
    }
    if (a->type == COLLIDER_AABB && b->type == COLLIDER_AABB) {
        return collision_aabb_aabb(&a->shape.aabb, &b->shape.aabb, out);
    }
    if (a->type == COLLIDER_SPHERE && b->type == COLLIDER_AABB) {
        return collision_sphere_aabb(&a->shape.sphere, &b->shape.aabb, out);
    }
    if (a->type == COLLIDER_AABB && b->type == COLLIDER_SPHERE) {
        bool hit = collision_sphere_aabb(&b->shape.sphere, &a->shape.aabb, out);
        if (hit && out) {
            // Flip normal since we swapped A and B
            out->normal = vec3_negate(&out->normal);
        }
        return hit;
    }
    if (out) out->hit = false;
    return false;
}

// --- Raycast against single collider ---

bool collision_raycast_single(const Collider *collider, const Ray *ray,
                              CollisionResult *out) {
    // Quick broadphase: ray vs AABB bounds
    CollisionResult aabb_hit;
    if (!collision_ray_aabb(ray, &collider->bounds, &aabb_hit)) {
        if (out) out->hit = false;
        return false;
    }

    // Narrowphase
    if (collider->type == COLLIDER_SPHERE) {
        return collision_ray_sphere(ray, &collider->shape.sphere, out);
    }
    if (collider->type == COLLIDER_AABB) {
        return collision_ray_aabb(ray, &collider->shape.aabb, out);
    }

    if (out) out->hit = false;
    return false;
}

// --- World queries ---

int collision_test_all(CollisionWorld *world) {
    world->result_count = 0;

    for (int i = 0; i < COLLISION_MAX_COLLIDERS && world->result_count < COLLISION_MAX_RESULTS; i++) {
        if (!world->colliders[i].active) continue;

        for (int j = i + 1; j < COLLISION_MAX_COLLIDERS && world->result_count < COLLISION_MAX_RESULTS; j++) {
            if (!world->colliders[j].active) continue;

            const Collider *a = &world->colliders[i];
            const Collider *b = &world->colliders[j];

            // Skip static-static pairs
            if (a->is_static && b->is_static) continue;

            // Layer filtering
            if (!(a->layer & b->mask) || !(b->layer & a->mask)) continue;

            // Broadphase: AABB overlap
            if (!aabb_overlap(&a->bounds, &b->bounds)) continue;

            // Narrowphase
            CollisionResult result;
            if (collision_test_pair(a, b, &result)) {
                result.id_a = i;
                result.id_b = j;
                world->results[world->result_count++] = result;
            }
        }
    }

    return world->result_count;
}

bool collision_raycast(const CollisionWorld *world, const Ray *ray,
                       uint16_t mask, CollisionResult *out) {
    bool any_hit = false;
    float closest = ray->max_distance;

    for (int i = 0; i < COLLISION_MAX_COLLIDERS; i++) {
        if (!world->colliders[i].active) continue;
        if (!(world->colliders[i].layer & mask)) continue;

        CollisionResult result;
        if (collision_raycast_single(&world->colliders[i], ray, &result)) {
            if (result.distance < closest) {
                closest = result.distance;
                if (out) {
                    *out = result;
                    out->id_a = -1;
                    out->id_b = i;
                }
                any_hit = true;
            }
        }
    }

    if (!any_hit && out) out->hit = false;
    return any_hit;
}

int collision_overlap_sphere(const CollisionWorld *world, vec3_t center, float radius,
                             uint16_t mask, int *out_handles, int max_results) {
    ColliderSphere query = {center, radius};
    ColliderAABB query_bounds = sphere_to_aabb(&query);
    int count = 0;

    for (int i = 0; i < COLLISION_MAX_COLLIDERS && count < max_results; i++) {
        if (!world->colliders[i].active) continue;
        if (!(world->colliders[i].layer & mask)) continue;

        // Broadphase
        if (!aabb_overlap(&query_bounds, &world->colliders[i].bounds)) continue;

        // Narrowphase
        const Collider *c = &world->colliders[i];
        bool hit = false;
        if (c->type == COLLIDER_SPHERE) {
            hit = collision_sphere_sphere(&query, &c->shape.sphere, NULL);
        } else if (c->type == COLLIDER_AABB) {
            hit = collision_sphere_aabb(&query, &c->shape.aabb, NULL);
        }

        if (hit) {
            out_handles[count++] = i;
        }
    }

    return count;
}

// --- Utility ---

ColliderAABB collision_aabb_from_points(const vec3_t *points, int count) {
    if (count <= 0) {
        return (ColliderAABB){{0, 0, 0}, {0, 0, 0}};
    }

    ColliderAABB result;
    result.min = points[0];
    result.max = points[0];

    for (int i = 1; i < count; i++) {
        result.min = vec3_min(&result.min, &points[i]);
        result.max = vec3_max(&result.max, &points[i]);
    }

    return result;
}

ColliderSphere collision_sphere_from_aabb(const ColliderAABB *aabb) {
    ColliderSphere result;
    result.center = vec3_lerp(&aabb->min, &aabb->max, 0.5f);
    result.radius = vec3_distance(&result.center, &aabb->max);
    return result;
}
