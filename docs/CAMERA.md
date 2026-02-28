# Camera System

An orbital camera system with perspective projection, frustum culling, and a self-contained 3D math library. The camera orbits around a target point using spherical coordinates controlled by the analog stick and C-buttons.

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

## Orbital Camera

The camera is defined by spherical coordinates relative to a target point:

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

### Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| `azimuth` | unbounded | 0.0 | Horizontal angle (radians) |
| `elevation` | -85 to +85 degrees | 0.3 rad (~17 degrees) | Vertical angle |
| `distance` | 50 to 1000 | 300 | Distance from target |
| `target` | any | (0, 0, 0) | World-space orbit pivot |

### Controls

| Input | Camera Action | Scale |
|-------|--------------|-------|
| Analog stick X | Orbit azimuth | -stick_x * 0.002 |
| Analog stick Y | Orbit elevation | stick_y * 0.002 |
| C-Up | Zoom in (closer) | -5.0 per frame |
| C-Down | Zoom out (farther) | +5.0 per frame |
| C-Left | Shift target down | -2.0 per frame |
| C-Right | Shift target up | +2.0 per frame |

## Camera Configuration

Presets for different scenes/levels:

```c
typedef struct {
    float azimuth, elevation, distance;
    vec3_t target;
    float fov_y, near_plane, far_plane;
} CameraConfig;

// Default preset:
const CameraConfig CAMERA_DEFAULT = {
    .azimuth    = 0.0f,
    .elevation  = 0.3f,
    .distance   = 300.0f,
    .target     = {0.0f, 0.0f, 0.0f},
    .fov_y      = 1.0472f,     // 60 degrees
    .near_plane = 10.0f,
    .far_plane  = 1000.0f,
};
```

Create custom presets for different scenes:
```c
const CameraConfig OVERHEAD_CAM = {
    .azimuth = 0.0f, .elevation = 1.2f, .distance = 500.0f,
    .target = {0, 0, 0}, .fov_y = 0.7854f, .near_plane = 10.0f, .far_plane = 2000.0f,
};
```

## API Reference

### `camera_init(Camera *cam, const CameraConfig *config)`

Initialize camera from a preset. Computes initial matrices.

### `camera_orbit(Camera *cam, float d_azimuth, float d_elevation)`

Add to azimuth and elevation. Marks camera dirty.

### `camera_zoom(Camera *cam, float d_distance)`

Add to distance (positive = farther). Marks camera dirty.

### `camera_shift_target_y(Camera *cam, float d_y)`

Shift the orbit target vertically. Marks camera dirty.

### `camera_update(Camera *cam)`

Recomputes position, view matrix, projection matrix, VP matrix, and frustum planes. Only runs if dirty flag is set.

### `camera_sphere_visible(const Camera *cam, const vec3_t *center, float radius)`

Returns `true` if a sphere is at least partially inside the view frustum. Used for object-level culling before rendering.

## Matrix Pipeline

```
camera_update() computes:

1. Position from spherical coords
2. View matrix (mat4_lookat)
3. Projection matrix (mat4_perspective)
4. VP = Projection * View
5. Frustum planes from VP

Per object:
6. MVP = VP * Model
7. clip = MVP * vertex    (vec4, homogeneous)
8. ndc = clip.xyz / clip.w  (perspective divide)
9. screen = viewport_map(ndc)
```

## Frustum Culling

Six planes are extracted from the VP matrix using the Griess-Hartmann method:

```c
// Each plane: {A, B, C, D} where Ax + By + Cz + D >= 0 is inside
float frustum[6][4];  // Left, Right, Bottom, Top, Near, Far
```

Sphere test: for each plane, compute `dist = A*cx + B*cy + C*cz + D`. If `dist < -radius` for any plane, the sphere is fully outside.

## Math Library

The engine includes a self-contained 3D math library (the Docker toolchain's libdragon version lacks `fgeom.h`):

### Types

```c
typedef struct { float x, y, z; } vec3_t;
typedef struct { float x, y, z, w; } vec4_t;
typedef struct { float m[4][4]; } mat4_t;  // Column-major: m[col][row]
```

### Functions

| Function | Purpose |
|----------|---------|
| `mat4_perspective()` | Perspective projection matrix |
| `mat4_lookat()` | View matrix from eye/target/up |
| `mat4_mul()` | 4x4 matrix multiply |
| `mat4_mul_vec3()` | Transform vec3 by mat4 (w=1), returns vec4 |
| `mat4_from_srt()` | Scale-Rotate-Translate matrix (Euler angles, R = Ry * Rx * Rz) |
| `camera_sphere_visible()` | Sphere-vs-frustum test |

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
| [src/render/camera.h](../src/render/camera.h) | Types (vec3/vec4/mat4), Camera struct, API |
| [src/render/camera.c](../src/render/camera.c) | Math implementation, camera logic, frustum extraction |
