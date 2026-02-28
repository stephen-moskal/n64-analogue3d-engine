# Camera System

A multi-mode camera system with perspective projection, frustum culling, collision detection, and a self-contained 3D math library. Supports orbital, fixed, and follow modes.

## Coordinate System

The engine uses a **right-handed** coordinate system:

```
        +Y (up)
         |
         |
         +------ +X (right)
        /
       /
     +Z (toward camera)
```

- Origin at world center (0, 0, 0)
- Camera looks toward -Z by default
- Matrices are **column-major**: `m[col][row]`

## Camera Modes

### Orbital (Default)

Orbits around a target point using spherical coordinates. Controlled by the analog stick and C-buttons.

```
          Camera
         /|
        / | sin(elevation) * distance
       /  |
      /   |
     / elevation
    /_____|_________ Target
       cos(elevation) * distance
```

```c
// Position from spherical coordinates:
x = target.x + distance * cos(elevation) * sin(azimuth);
y = target.y + distance * sin(elevation);
z = target.z + distance * cos(elevation) * cos(azimuth);
```

#### Orbital Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| `azimuth` | unbounded | 0.0 | Horizontal angle (radians) |
| `elevation` | -85 to +85 degrees | 0.3 rad (~17 degrees) | Vertical angle |
| `distance` | 50 to 1000 | 300 | Distance from target |
| `target` | any | (0, 0, 0) | World-space orbit pivot |

#### Orbital Controls

| Input | Action | Scale |
|-------|--------|-------|
| Analog stick X | Orbit azimuth | -stick_x * 0.002 |
| Analog stick Y | Orbit elevation | stick_y * 0.002 |
| C-Up | Zoom in (closer) | -5.0 per frame |
| C-Down | Zoom out (farther) | +5.0 per frame |
| C-Left | Shift target down | -2.0 per frame |
| C-Right | Shift target up | +2.0 per frame |

### Fixed

Camera placed at a fixed position, looking at a fixed target. Both position and look-at target can be adjusted at runtime via controller input.

```c
camera_set_fixed(&cam,
    (vec3_t){250.0f, 200.0f, 250.0f},   // position
    (vec3_t){0.0f, 0.0f, 0.0f});        // look-at target
```

#### Fixed Controls

| Input | Action | Scale |
|-------|--------|-------|
| Analog stick X | Translate camera X | azimuth * 150.0 |
| Analog stick Y | Translate camera Z | elevation * 150.0 |
| C-Up | Move camera up | 5.0 per frame |
| C-Down | Move camera down | 5.0 per frame |
| C-Left | Shift look-at target down | -2.0 per frame |
| C-Right | Shift look-at target up | +2.0 per frame |

### Follow

Camera follows a target object (via pointer) with a configurable offset. Position is smoothly interpolated using lerp.

```c
camera_set_follow_target(&cam, &object_position,
    (vec3_t){0.0f, 200.0f, -350.0f});   // offset from target
```

The camera computes `desired_position = *follow_target + follow_offset`, then lerps toward it each frame. The look-at point is always the follow target.

#### Follow Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `follow_offset` | (0, 200, -300) | Offset from target in world space |
| `follow_smoothing` | 0.1 | Lerp factor (0.0 = instant, 0.9 = very smooth) |

#### Follow Controls

| Input | Action | Scale |
|-------|--------|-------|
| Analog stick X | Adjust offset X | azimuth * 150.0 |
| Analog stick Y | Adjust offset Z | elevation * 150.0 |
| C-Up | Adjust offset up | 5.0 per frame |
| C-Down | Adjust offset down | 5.0 per frame |

## Camera Collision

Prevents the camera from clipping through geometry using raycasting.

```c
camera_set_collision(&cam, &collision_world, COLLISION_LAYER_ENV);
```

### How It Works

1. After computing camera position (any mode), raycast from the look-at point toward the camera
2. If a collider is hit closer than the camera distance:
   - Snap camera to `hit_point - 5.0 * ray_direction` (offset avoids z-fighting)
   - Clamp to at least `near_plane` distance
3. Uses `collision_mask` to filter layers — typically `COLLISION_LAYER_ENV` to only collide with environment geometry (not the object being orbited/followed)

### Layer Strategy

```c
// Object being viewed: DEFAULT layer only
collision_add_sphere(&world, center, radius,
    COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL);

// Environment: DEFAULT + ENV layers
collision_add_aabb(&world, min, max,
    COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
    COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);

// Camera collision tests only ENV — won't hit the viewed object
camera_set_collision(&cam, &world, COLLISION_LAYER_ENV);
```

See [COLLISION.md](COLLISION.md) for full collision system documentation.

## Camera Configuration

Presets for different scenes/levels:

```c
typedef struct {
    CameraMode mode;
    float azimuth, elevation, distance;
    vec3_t target;
    float fov_y, near_plane, far_plane;
    vec3_t follow_offset;
    float follow_smoothing;
    vec3_t fixed_position;
    vec3_t fixed_target;
} CameraConfig;

const CameraConfig CAMERA_DEFAULT = {
    .mode       = CAMERA_MODE_ORBITAL,
    .azimuth    = 0.0f,
    .elevation  = 0.3f,
    .distance   = 300.0f,
    .target     = {0, 0, 0},
    .fov_y      = 1.0472f,     // 60 degrees
    .near_plane = 10.0f,
    .far_plane  = 1000.0f,
    .follow_offset    = {0, 200, -300},
    .follow_smoothing = 0.1f,
    .fixed_position   = {0, 200, 300},
    .fixed_target     = {0, 0, 0},
};
```

## API Reference

### Initialization

```c
void camera_init(Camera *cam, const CameraConfig *config);
```

### Orbital Controls

```c
void camera_orbit(Camera *cam, float d_azimuth, float d_elevation);
void camera_zoom(Camera *cam, float d_distance);
void camera_shift_target_y(Camera *cam, float d_y);
```

### Mode Switching

```c
void camera_set_mode(Camera *cam, CameraMode mode);
void camera_set_follow_target(Camera *cam, const vec3_t *target, vec3_t offset);
void camera_set_fixed(Camera *cam, vec3_t position, vec3_t target);
void camera_set_collision(Camera *cam, const struct CollisionWorld *world, uint16_t mask);
```

### Update & Query

```c
void camera_update(Camera *cam);  // Recompute matrices (only if dirty)
bool camera_sphere_visible(const Camera *cam, const vec3_t *center, float radius);
```

## Matrix Pipeline

```
camera_update() computes:

1. Position (from mode: spherical / fixed / follow+lerp)
2. Camera collision (raycast snap)
3. View direction (normalized)
4. View matrix (mat4_lookat)
5. Projection matrix (mat4_perspective)
6. VP = Projection * View
7. Frustum planes from VP

Per object:
8.  MVP = VP * Model
9.  clip = MVP * vertex    (vec4, homogeneous)
10. ndc = clip.xyz / clip.w  (perspective divide)
11. screen = viewport_map(ndc)
```

## Frustum Culling

Six planes are extracted from the VP matrix using the Griess-Hartmann method:

```c
// Each plane: {A, B, C, D} where Ax + By + Cz + D >= 0 is inside
float frustum[6][4];  // Left, Right, Bottom, Top, Near, Far
```

Sphere test: for each plane, compute `dist = A*cx + B*cy + C*cz + D`. If `dist < -radius` for any plane, the sphere is fully outside.

## Math Library

The engine includes a self-contained 3D math library in `src/math/vec3.h`:

### Types

```c
typedef struct { float x, y, z; } vec3_t;    // in math/vec3.h
typedef struct { float x, y, z, w; } vec4_t;  // in camera.h
typedef struct { float m[4][4]; } mat4_t;      // Column-major: m[col][row]
```

### Vector Functions (vec3.h)

| Function | Purpose |
|----------|---------|
| `vec3_add/sub/scale/negate` | Basic arithmetic |
| `vec3_dot/cross` | Dot and cross product |
| `vec3_length/length_sq` | Magnitude |
| `vec3_distance/distance_sq` | Distance between points |
| `vec3_normalize` | Unit vector |
| `vec3_min/max` | Component-wise min/max |
| `vec3_lerp` | Linear interpolation |
| `vec3_clampf` | Scalar clamp utility |

### Matrix Functions (camera.h)

| Function | Purpose |
|----------|---------|
| `mat4_perspective()` | Perspective projection matrix |
| `mat4_lookat()` | View matrix from eye/target/up |
| `mat4_mul()` | 4x4 matrix multiply |
| `mat4_mul_vec3()` | Transform vec3 by mat4 (w=1), returns vec4 |
| `mat4_from_srt()` | Scale-Rotate-Translate (Euler, R = Ry * Rx * Rz) |

All functions operate on column-major matrices matching OpenGL conventions.

## Dirty Flag Optimization

The camera uses a dirty flag to avoid recomputing matrices when nothing has changed:

```c
void camera_orbit(Camera *cam, float da, float de) {
    cam->azimuth += da;
    cam->elevation += de;
    cam->dirty = true;
}

void camera_update(Camera *cam) {
    if (!cam->dirty) return;
    // ... recompute everything ...
    cam->dirty = false;
}
```

`camera_update()` is called every frame but only does work when parameters change.

## Source Files

| File | Purpose |
|------|---------|
| [src/render/camera.h](../src/render/camera.h) | Types, Camera struct, CameraConfig, API |
| [src/render/camera.c](../src/render/camera.c) | Math, camera logic, frustum, collision |
| [src/math/vec3.h](../src/math/vec3.h) | Vector math library |
