# Scene & World Management

The scene system organizes the engine into self-contained units of content. Each scene owns its own camera, lighting, collision world, objects, and textures. A scene manager handles loading, unloading, and transitions between scenes.

## Architecture

```
SceneManager
├── current: Scene*
│   ├── Camera        (orbital / fixed / follow)
│   ├── LightConfig   (Blinn-Phong parameters)
│   ├── CollisionWorld (up to 64 colliders)
│   ├── SceneObject[]  (up to 32 objects)
│   ├── Textures      (per-scene, loaded on init)
│   └── Callbacks     (on_init, on_update, on_draw, on_cleanup)
├── pending: Scene*   (next scene during transitions)
└── transition state  (type, progress, phase)
```

### Ownership Model

| Component | Owner | Lifetime |
|-----------|-------|----------|
| Display, Z-buffer | `main.c` | Application |
| Input, fonts | `main.c` | Application |
| Menu (global overlay) | `main.c` | Application |
| Camera, lighting, collision | Scene | Scene load/unload |
| Objects, textures | Scene | Scene load/unload |

## Defining a Scene

Scenes are code-defined C files with callback functions. Each scene is a static `Scene` struct returned via a getter function.

### Scene Header

```c
// src/scenes/my_scene.h
#ifndef MY_SCENE_H
#define MY_SCENE_H
#include "../scene/scene.h"
Scene *my_scene_get(void);
#endif
```

### Scene Implementation

```c
// src/scenes/my_scene.c
#include "my_scene.h"

static void my_init(Scene *scene) {
    // Load textures, create colliders, configure camera
    camera_init(&scene->camera, &CAMERA_DEFAULT);

    // Add a ground collider
    collision_add_aabb(&scene->collision,
        (vec3_t){-500, -100, -500}, (vec3_t){500, -50, 500},
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
}

static void my_update(Scene *scene, float dt) {
    // Handle input, update game logic
}

static void my_draw(Scene *scene) {
    // Render geometry, HUD text
}

static void my_cleanup(Scene *scene) {
    // Free scene-specific resources
}

static Scene my_scene = {
    .name = "My Scene",
    .object_count = 0,
    .texture_count = 0,
    .world_offset = {0, 0, 0},
    .bg_color = {0x10, 0x10, 0x30, 0xFF},
    .on_init = my_init,
    .on_update = my_update,
    .on_draw = my_draw,
    .on_cleanup = my_cleanup,
    .loaded = false,
};

Scene *my_scene_get(void) {
    return &my_scene;
}
```

## Scene Lifecycle

### Initialization

`scene_init()` is called when a scene becomes active:

```
scene_init(scene)
├── collision_world_init()   — Reset collision world
├── lighting_init()          — Set default lighting
├── Load textures            — texture_load_slot() for each
├── scene->on_init()         — Scene-specific setup
└── scene->loaded = true
```

### Per-Frame Update

`scene_update()` runs every frame for the active scene:

```
scene_update(scene, dt)
├── Per-object on_update()   — Object-level callbacks
├── scene->on_update()       — Scene-level logic (input, game state)
├── camera_update()          — Recompute matrices if dirty
└── collision_test_all()     — Run collision detection
```

### Per-Frame Draw

`scene_draw()` runs after update, inside the RDP frame:

```
scene_draw(scene)
├── rdpq_clear(bg_color)     — Clear to scene background color
├── rdpq_clear_z(ZBUF_MAX)   — Clear Z-buffer
├── scene->on_draw()         — Scene-level rendering
└── Per-object on_draw()     — Object-level draw callbacks
```

### Cleanup

`scene_cleanup()` is called when switching away from a scene:

```
scene_cleanup(scene)
├── scene->on_cleanup()      — Free scene-specific resources
├── texture_free_slot()      — Free per-scene textures
├── object_count = 0
└── scene->loaded = false
```

## Scene Objects

Objects within a scene have transform, state flags, and optional callbacks:

```c
typedef struct SceneObject {
    vec3_t position;
    vec3_t rotation;      // Euler angles (radians)
    vec3_t scale;

    bool active;          // Participates in update
    bool visible;         // Participates in draw

    int collider_handle;  // Handle in scene's CollisionWorld (-1 = none)

    void *data;           // Type-specific data pointer
    void (*on_update)(struct SceneObject *obj, float dt);
    void (*on_draw)(struct SceneObject *obj, const Camera *cam,
                    const LightConfig *light);
} SceneObject;
```

### Object Management

```c
// Add an object (returns index or -1 if full)
int idx = scene_add_object(scene, &obj);

// Access by index
SceneObject *obj = scene_get_object(scene, idx);

// Remove (shifts remaining objects down)
scene_remove_object(scene, idx);
```

## Scene Manager

The scene manager controls which scene is active and handles transitions.

### Setup

```c
SceneManager scene_mgr;
scene_manager_init(&scene_mgr);
scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);
```

### Game Loop Integration

```c
while (1) {
    float dt = 1.0f / 30.0f;
    scene_manager_update(&scene_mgr, dt);

    surface_t *fb = display_get();
    rdpq_attach(fb, &zbuf);
    scene_manager_draw(&scene_mgr);
    rdpq_detach_show();
}
```

### Switching Scenes

```c
// Instant switch
scene_manager_switch(&mgr, next_scene, TRANSITION_CUT, 0);

// Fade to black (speed = progress per second, e.g., 2.0 = 0.5s fade)
scene_manager_switch(&mgr, next_scene, TRANSITION_FADE_BLACK, 2.0f);

// Fade to white
scene_manager_switch(&mgr, next_scene, TRANSITION_FADE_WHITE, 1.5f);
```

## Transitions

### Transition Types

| Type | Effect |
|------|--------|
| `TRANSITION_CUT` | Instant switch (cleanup old, init new) |
| `TRANSITION_FADE_BLACK` | Fade out to black, switch, fade in |
| `TRANSITION_FADE_WHITE` | Fade out to white, switch, fade in |

### Fade Transition Phases

```
Phase 0: Fade Out
├── Current scene still updates
├── Alpha overlay: 0 → 255
└── When progress >= 1.0: cleanup current, init next

Phase 2: Fade In
├── New scene updates
├── Alpha overlay: 255 → 0
└── When progress >= 1.0: transition complete
```

The fade overlay is drawn as a fullscreen quad (two triangles in 1-cycle mode) with alpha blending. This is hardware-safe — fill mode only works with rectangles on real N64 hardware.

### Querying Transition State

```c
if (scene_manager_is_transitioning(&mgr)) {
    // Transition in progress — avoid triggering another
}

Scene *current = scene_manager_current(&mgr);
```

## Per-Scene Textures

Scenes can declare textures to be loaded on init and freed on cleanup:

```c
static Scene my_scene = {
    .texture_paths = {"rom:/wall.sprite", "rom:/floor.sprite"},
    .texture_slots = {0, 1},
    .texture_count = 2,
    // ...
};
```

Or load textures manually in `on_init`:

```c
static void my_init(Scene *scene) {
    texture_load_slot(0, "rom:/wall.sprite");
    texture_load_slot(1, "rom:/floor.sprite");
}
```

Textures in declared slots are automatically freed by `scene_cleanup()`.

## World Coordinates

Each scene has a `world_offset` field for shared-coordinate systems:

```c
// Independent scene (menu, battle screen)
.world_offset = {0, 0, 0},

// Part of a larger world (overworld sector)
.world_offset = {1000, 0, 2000},
```

This supports both independent scenes (each with their own origin) and scenes that share a global coordinate space (e.g., overworld sectors connected by doors or level transitions).

## Camera Modes

Each scene owns a `Camera` with three available modes:

| Mode | Description | Controls |
|------|-------------|----------|
| `CAMERA_MODE_ORBITAL` | Orbit around a target point | Stick: orbit, C-up/down: zoom |
| `CAMERA_MODE_FIXED` | Fixed position + look-at | Stick: translate XZ, C: move Y |
| `CAMERA_MODE_FOLLOW` | Follow a target with offset | Stick: adjust offset, C: offset Y |

Camera mode can be set in `on_init` or changed dynamically:

```c
// Set to orbital (default)
camera_set_mode(&scene->camera, CAMERA_MODE_ORBITAL);

// Set to fixed position
camera_set_fixed(&scene->camera,
    (vec3_t){250, 200, 250},   // position
    (vec3_t){0, 0, 0});        // look-at

// Set to follow an object
camera_set_follow_target(&scene->camera, &object_position,
    (vec3_t){0, 200, -350});   // offset from target
```

### Camera Collision

Camera collision prevents the camera from clipping through geometry:

```c
camera_set_collision(&scene->camera, &scene->collision,
                     COLLISION_LAYER_ENV);
```

Uses raycasting from the look-at point toward the camera. If a hit is detected closer than the camera distance, the camera snaps to the hit point (minus a small offset). Use `COLLISION_LAYER_ENV` to only collide with environment geometry.

See [CAMERA.md](CAMERA.md) for full camera documentation.

## Limits

| Limit | Value |
|-------|-------|
| Max objects per scene | 32 |
| Max textures per scene | 16 |
| Max colliders per scene | 64 |
| Max collision results | 32 |

## Memory Budget

```
4MB RDRAM total
├── Framebuffers (3x 320x240x2B) .... 450KB
├── Z-buffer (320x240x2B) ........... 150KB
├── Code + BSS ...................... ~270KB
├── Audio buffers ................... ~64KB
└── Available for scenes ........... ~3MB
```

Each scene's budget: ~3MB minus persistent assets (UI textures, fonts). Shared resources (Z-buffer, framebuffers) persist across scene transitions.

## Demo Scene

The included demo scene (`src/scenes/demo_scene.c`) demonstrates:

- Textured rotating cube with Blinn-Phong lighting
- Three camera modes switchable via Start menu
- Camera collision toggle
- Sphere collider on cube, AABB collider on ground plane
- Raycast from camera with hit distance on HUD
- Debug HUD: FPS, triangle/upload counts, collision count, camera position

## Source Files

| File | Purpose |
|------|---------|
| [src/scene/scene.h](../src/scene/scene.h) | Scene, SceneObject, SceneManager types |
| [src/scene/scene.c](../src/scene/scene.c) | Scene lifecycle, manager, transitions |
| [src/scenes/demo_scene.h](../src/scenes/demo_scene.h) | Demo scene header |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | Demo scene implementation |
