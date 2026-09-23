# Scene & World Management

The scene system organizes the engine into self-contained units of content. Each scene owns its own camera, lighting, collision world, objects, and textures. A scene manager handles loading, unloading, soft resets and transitions between scenes.

## Architecture

```
SceneManager
├── current: Scene*
│   ├── Camera          (orbital / fixed / follow)
│   ├── LightConfig     (Blinn-Phong parameters)
│   ├── CollisionWorld  (up to 64 colliders)
│   ├── SceneObject[]   (up to 32 objects)
│   ├── Textures        (declared paths/slots, loaded on init)
│   ├── Callbacks       (on_init, on_update, on_draw, on_post_draw, on_cleanup)
│   └── reset_requested (soft reset on the next update)
├── pending: Scene*     (next scene during transitions)
└── transition state    (type, progress, phase)
```

### Ownership Model

| Component | Owner | Lifetime |
|-----------|-------|----------|
| Display, Z-buffer | `main.c` | Application |
| Input, fonts, audio | `main.c` (initialized once) | Application |
| Start menu (global) | built in `main.c`, driven by the demo scene | Application |
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
#include "../input/action.h"
#include "../render/floor.h"

static void my_init(Scene *scene) {
    // Declared textures are already loaded. scene_init() does not set up the
    // camera: every scene calls camera_init().
    camera_init(&scene->camera, &CAMERA_DEFAULT);

    // Ground collider, top at FLOOR_Y
    collision_add_aabb(&scene->collision,
        (vec3_t){-500, -180, -500}, (vec3_t){500, -100, 500},
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
        COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
}

static void my_update(Scene *scene, float dt) {
    action_update();   // poll the joypad: nothing else does (INPUT.md)
    // Game logic
}

static void my_draw(Scene *scene) {
    floor_draw(&scene->camera, &scene->lighting);   // 3D geometry
}

static void my_post_draw(Scene *scene) {
    // After all objects: particles, HUD text, menus
}

static void my_cleanup(Scene *scene) {
    // Free what on_init allocated by hand (declared textures are freed for you)
}

static Scene my_scene = {
    .name = "My Scene",
    .texture_paths = {"rom:/tree.sprite"},
    .texture_slots = {8},
    .texture_count = 1,
    .world_offset = {0, 0, 0},
    .bg_color = {0x10, 0x10, 0x30, 0xFF},
    .on_init = my_init,
    .on_update = my_update,
    .on_draw = my_draw,
    .on_post_draw = my_post_draw,
    .on_cleanup = my_cleanup,
};

Scene *my_scene_get(void) {
    return &my_scene;
}
```

Two things a new scene must do itself:

- **Poll input.** Each scene's `on_update` calls `action_update()` (the demo and benchmark scenes do). Without it no button reaches the game, and the Debug tab's D-Up/D-Down shortcuts stop working too.
- **Drive the Start menu, if it wants one.** The global menu is opened, updated and drawn by `demo_scene.c` only ([MENU_SYSTEM.md](MENU_SYSTEM.md)); the benchmark scene has none.

A scene becomes active through `scene_manager_switch()`: `main.c` starts the demo and switches between demo and benchmark from the Debug tab's Scene item. The full recipe for adding and registering a scene is in [EXTENDING.md](EXTENDING.md).

## Scene Lifecycle

### Initialization

`scene_init()` is called when a scene becomes active:

```
scene_init(scene)
├── collision_world_init()   — Reset collision world
├── lighting_init()          — Default lighting
├── texture_load_slot()      — For each declared texture (texture_paths / texture_slots)
├── scene->on_init()         — Scene-specific setup
└── scene->loaded = true
```

### Per-Frame Update

`scene_update()` runs every frame for the active scene:

```
scene_update(scene, dt)
├── Per-object on_update()   — Active objects with a callback
├── scene->on_update()       — Scene-level logic (input, game state)
├── camera_update()          — Rebuilds matrices only when dirty (or in follow mode)
└── collision_test_all()     — Collision detection; colliders and pairs go to the stats
```

The camera and collision work is timed in the `scene_sys` profiler slot.

### Per-Frame Draw

`scene_draw()` runs after update, inside the RDP frame:

```
scene_draw(scene)
├── Background               — sky_draw() when the sky covers the screen,
│                              otherwise rdpq_clear(bg_color)
├── rdpq_clear_z(ZBUF_MAX)   — Clear Z-buffer
├── scene->on_draw()         — Scene-level rendering (floor, shadows, 3D)
├── Per-object on_draw()     — Visible objects with a callback ("objects" profiler slot)
└── scene->on_post_draw()    — After all 3D: particles, HUD, menu
```

The sky replaces the colour clear rather than drawing over it, so scenes never call `sky_draw()` themselves. `scene_manager_draw()` draws the transition fade on top.

### Cleanup

`scene_cleanup()` is called when switching away from a scene:

```
scene_cleanup(scene)
├── scene->on_cleanup()      — Free scene-specific resources
├── texture_free_slot()      — For each declared texture
├── object_count = 0
└── scene->loaded = false
```

### Soft Reset

Setting `scene->reset_requested = true` makes the next `scene_manager_update()` run `scene_cleanup()` and `scene_init()` on the current scene and skip that frame's update. The demo's Settings → Reset Scene and the Debug tab's Reset Soak use it. Level restarts or a death screen can use the same mechanism.

A scene that frees everything it allocates keeps the heap flat across resets; Reset Soak measures exactly that ([DEBUGGING.md](DEBUGGING.md)).

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

`collider_handle` is not used by the engine yet: the demo keeps its own collider handles.

### Object Management

```c
// Add an object (copied into the scene; returns index or -1 if full)
int idx = scene_add_object(scene, &obj);

// Access by index (NULL if out of range)
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

`main.c` runs a variable-timestep loop: `dt` is the real time since the previous iteration, capped at 0.1 s.

```c
while (1) {
    float dt = /* seconds since the previous iteration, capped at 0.1 */;
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

Scenes declare the textures they need; `scene_init()` loads them before `on_init` and `scene_cleanup()` frees them after `on_cleanup`:

```c
static Scene my_scene = {
    .texture_paths = {"rom:/wall.sprite", "rom:/floor.sprite"},
    .texture_slots = {8, 9},
    .texture_count = 2,
    // ...
};
```

Texture slots are global ([TEXTURES.md](TEXTURES.md)): the demo uses 0–7. A texture loaded by hand in `on_init` with `texture_load_slot()` is **not** freed automatically; free it in `on_cleanup` with `texture_free_slot()` (or `texture_cleanup()`), otherwise every Reset Scene leaks it.

## World Coordinates

Each scene has a `world_offset` field for shared-coordinate systems:

```c
// Independent scene (menu, battle screen)
.world_offset = {0, 0, 0},

// Part of a larger world (overworld sector)
.world_offset = {1000, 0, 2000},
```

This supports both independent scenes (each with their own origin) and scenes that share a global coordinate space (e.g., overworld sectors connected by doors or level transitions). No engine code reads `world_offset` yet.

## Camera Modes

Each scene owns a `Camera` with three available modes ([CAMERA.md](CAMERA.md)). The controls below are the demo scene's:

| Mode | Description | Demo controls |
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

A ray from the look-at point toward the camera snaps the camera in front of the first hit, a small sphere is pushed out of sphere colliders, and the camera is clamped above `min_y`. Use `COLLISION_LAYER_ENV` to only raycast against environment geometry. See [CAMERA.md](CAMERA.md) for details.

## Limits

| Limit | Value |
|-------|-------|
| Max objects per scene | 32 (`SCENE_MAX_OBJECTS`) |
| Max declared textures per scene | 16 (`SCENE_MAX_TEXTURES`) |
| Max colliders per scene | 64 |
| Max collision results | 32 |

## Memory

Shared resources (framebuffers, Z-buffer, fonts, audio) persist across scene transitions; everything a scene loads should be freed by its cleanup. Measured numbers (heap in use after the demo loads, framebuffer and Z-buffer sizes, stack peak) are in [BENCHMARKS.md](BENCHMARKS.md), and the Memory overlay page shows them live, including the heap delta since boot ([PROFILING.md](PROFILING.md)). Design for 4 MB of RDRAM even though the Analogue 3D reports 8 MB.

## Demo Scene

The demo scene (`src/scenes/demo_scene.c`) exercises most of the engine:

- Six mesh objects: textured rotating cube, two pillars, platform, rotating pyramid and a static sphere (the curved-surface test object), plus three billboards (a marker and two trees)
- B spawns (then re-launches) a physics ball and fires particle bursts on the pillar tops; torch flames burn while point lights are on
- Object selection (Z, D-Left/Right) and move/rotate/scale (A cycles the mode, stick and C-buttons manipulate)
- The Start menu: background, lighting, shadows, point lights, atmosphere presets, camera mode and collision, frame rate, sound, control remapping, Reset Scene, and the Debug tab
- Most menu values are applied only when they change, not every frame
- HUD: title, object counts, triangles/uploads/collisions/raycast distance, FPS and CPU time, camera mode and position

The benchmark scene (`src/scenes/benchmark_scene.c`) is a second, menu-less scene; see [BENCHMARKS.md](BENCHMARKS.md).

## Source Files

| File | Purpose |
|------|---------|
| [src/scene/scene.h](../src/scene/scene.h) | Scene, SceneObject, SceneManager types |
| [src/scene/scene.c](../src/scene/scene.c) | Scene lifecycle, background, manager, transitions, soft reset |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | Demo scene implementation |
| [src/scenes/benchmark_scene.c](../src/scenes/benchmark_scene.c) | Benchmark scene |
| [src/main.c](../src/main.c) | Creates the scene manager, switches scenes, runs the frame loop |
