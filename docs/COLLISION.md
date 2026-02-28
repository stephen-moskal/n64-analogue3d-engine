# Collision Detection

A layer-based collision system supporting sphere and AABB colliders, overlap queries, and raycasting. Runs entirely on the CPU.

## Overview

The collision system manages a world of up to 64 colliders. Each frame, `collision_test_all()` performs broadphase AABB culling followed by narrowphase shape tests for all active pairs. Results are stored in a flat buffer for game logic to query.

```
collision_test_all()
├── For each pair (i, j):
│   ├── Skip inactive, skip static-static
│   ├── Layer mask test: (a.layer & b.mask) && (b.layer & a.mask)
│   ├── Broadphase: AABB overlap test
│   └── Narrowphase: shape-specific test
└── Results stored in world->results[] (up to 32)
```

## Collider Types

### Sphere

```c
typedef struct {
    vec3_t center;
    float radius;
} ColliderSphere;
```

Defined by center point and radius. Ideal for characters, projectiles, and objects with roughly uniform extent.

### AABB (Axis-Aligned Bounding Box)

```c
typedef struct {
    vec3_t min;
    vec3_t max;
} ColliderAABB;
```

Defined by min/max corners. Ideal for walls, floors, platforms, and rectangular regions.

## Collision Layers

Layers use 16-bit bitmasks to control which colliders interact:

| Constant | Value | Purpose |
|----------|-------|---------|
| `COLLISION_LAYER_DEFAULT` | `0x0001` | General game objects |
| `COLLISION_LAYER_ENV` | `0x0002` | Environment geometry (walls, floors) |
| `COLLISION_LAYER_ALL` | `0xFFFF` | Match everything |

Each collider has two bitmasks:
- **`layer`** — What layer(s) this collider belongs to
- **`mask`** — What layer(s) this collider tests against

A pair is tested only if `(a.layer & b.mask) && (b.layer & a.mask)`.

### Example: Camera Collision

The camera collision system uses layers to avoid colliding with the object being orbited:

```c
// Cube: DEFAULT only (camera ignores it)
collision_add_sphere(&world, center, radius,
    COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);

// Ground: DEFAULT + ENV (camera collides with it)
collision_add_aabb(&world, min, max,
    COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
    COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);

// Camera tests only against ENV layer
camera_set_collision(&cam, &world, COLLISION_LAYER_ENV);
```

## Collision Results

```c
typedef struct {
    bool hit;
    vec3_t point;      // Contact point (world space)
    vec3_t normal;     // Contact normal (from B toward A)
    float depth;       // Penetration depth (overlap tests)
    float distance;    // Hit distance (raycasts)
    int id_a;          // Collider handle A
    int id_b;          // Collider handle B
} CollisionResult;
```

After `collision_test_all()`, results are in `world->results[]` with count in `world->result_count`.

## Raycasting

Cast a ray against all colliders matching a layer mask. Returns the closest hit.

```c
Ray ray;
ray.origin = camera.position;
ray.direction = camera.view_dir;  // Must be normalized
ray.max_distance = 1000.0f;

CollisionResult result;
if (collision_raycast(&world, &ray, COLLISION_LAYER_ALL, &result)) {
    debugf("Hit at distance %.1f\n", result.distance);
}
```

### Supported Ray Tests

| Function | Shape |
|----------|-------|
| `collision_ray_sphere()` | Ray vs sphere |
| `collision_ray_aabb()` | Ray vs AABB |
| `collision_ray_triangle()` | Ray vs triangle (Moller-Trumbore) |

## Overlap Queries

Find all colliders overlapping a sphere region:

```c
int handles[8];
int count = collision_overlap_sphere(&world, center, radius,
    COLLISION_LAYER_ALL, handles, 8);
```

## API Reference

### World Lifecycle

```c
void collision_world_init(CollisionWorld *world);
```

### Collider Management

```c
// Add colliders (returns handle or -1 if full)
int collision_add_sphere(CollisionWorld *world, vec3_t center, float radius,
                         uint16_t layer, uint16_t mask, void *userdata);
int collision_add_aabb(CollisionWorld *world, vec3_t min, vec3_t max,
                       uint16_t layer, uint16_t mask, void *userdata);

// Remove and configure
void collision_remove(CollisionWorld *world, int handle);
void collision_set_static(CollisionWorld *world, int handle, bool is_static);
void collision_set_trigger(CollisionWorld *world, int handle, bool is_trigger);
```

### Position Updates

```c
void collision_update_sphere(CollisionWorld *world, int handle, vec3_t center);
void collision_update_aabb(CollisionWorld *world, int handle, vec3_t min, vec3_t max);
```

### Queries

```c
int  collision_test_all(CollisionWorld *world);        // All-pairs test
bool collision_test_pair(const Collider *a, const Collider *b, CollisionResult *out);
int  collision_overlap_sphere(const CollisionWorld *world, vec3_t center, float radius,
                              uint16_t mask, int *out_handles, int max_results);
bool collision_raycast(const CollisionWorld *world, const Ray *ray,
                       uint16_t mask, CollisionResult *out);
```

### Utility

```c
ColliderAABB collision_aabb_from_points(const vec3_t *points, int count);
ColliderSphere collision_sphere_from_aabb(const ColliderAABB *aabb);
```

## Collider Properties

| Property | Description |
|----------|-------------|
| `is_static` | Static-static pairs are skipped (optimization) |
| `is_trigger` | Reports overlap but no physical response |
| `active` | Inactive colliders are ignored |
| `userdata` | Opaque pointer back to game object |

## Integration with Scene System

Each `Scene` owns a `CollisionWorld`. The scene system calls `collision_test_all()` automatically after `on_update`:

```c
// In scene_update():
scene->on_update(scene, dt);     // Game logic, input
camera_update(&scene->camera);   // Camera matrices
collision_test_all(&scene->collision);  // Collision detection
```

Scenes add/remove colliders in their `on_init` and `on_cleanup` callbacks.

## Limits

| Limit | Value |
|-------|-------|
| Max colliders per world | 64 |
| Max results per frame | 32 |

## Source Files

| File | Purpose |
|------|---------|
| [src/collision/collision.h](../src/collision/collision.h) | Types, constants, full API |
| [src/collision/collision.c](../src/collision/collision.c) | All collision detection logic |
| [src/math/vec3.h](../src/math/vec3.h) | Vector math used by collision |
