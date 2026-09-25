# Scene & World Management

The scene system organizes the engine into self-contained units of content. Each scene owns its own camera, lighting, collision world, physics world, objects, and textures. A scene manager handles loading, unloading, soft resets and transitions between scenes.

## Architecture

```
SceneManager
├── current: Scene*
│   ├── Camera          (orbital / fixed / follow)
│   ├── LightConfig     (Blinn-Phong parameters)
│   ├── CollisionWorld  (up to 64 colliders)
│   ├── PhysicsWorld    (up to 32 bodies; raycasts the collision world)
│   ├── SceneObject[]   (up to 32 objects; each can own a collider and a body)
│   ├── Textures        (declared paths/slots, loaded on init)
│   ├── Callbacks       (on_init, on_update, on_draw, on_post_draw, on_cleanup)
│   └── reset_requested (soft reset on the next update)
├── pending: Scene*     (next scene during transitions)
└── transition state    (type, progress, phase)
```

### Ownership Model

| Component | Owner | Lifetime |
|-----------|-------|----------|
| Display, Z-buffer | `engine_init()` (`src/engine/engine.c`) | Application |
| Input, fonts, audio | `engine_init()` (initialized once) | Application |
| Start menu (global) | built in `main.c`, driven by the demo scene | Application |
| Camera, lighting, collision, physics | Scene | Scene load/unload |
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
    // The engine polled the controllers just before (input_poll): read actions
    if (action_pressed(0, ACT_JUMP)) jump();
}

static void my_draw(Scene *scene) {
    floor_draw(scene_view_camera(), scene_view_light());   // 3D geometry
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

- **Push its controls.** Input is polled by the engine before every scene update (since S9), but a scene's actions only fire once it has pushed its contexts (`action_push_context`, usually in `on_init`, popped in `on_cleanup`; [INPUT.md](INPUT.md)). The Debug tab's D-Up/D-Down shortcuts work in every scene.
- **Drive the Start menu, if it wants one.** The global menu is opened, updated and drawn by `demo_scene.c` only ([MENU_SYSTEM.md](MENU_SYSTEM.md)); the benchmark scene has none.

A scene becomes active through `scene_manager_switch()`: `main.c` starts the demo and switches between demo and benchmark from the Debug tab's Scene item. The full recipe for adding and registering a scene is in [EXTENDING.md](EXTENDING.md).

## Scene Lifecycle

### Initialization

`scene_init()` is called when a scene becomes active:

```
scene_init(scene)
├── scene_objects_init()     — No objects; empty collision and physics worlds
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
├── physics_world_update()   — Fixed steps, only when the scene has bodies
├── scene_sync_bodies()      — Objects move to their bodies
├── camera_update()          — Rebuilds matrices only when dirty (or in follow mode)
├── scene_sync_colliders()   — Colliders move to their objects
└── collision_test_all()     — Collision detection; colliders and pairs go to the stats
```

Physics and the body sync are timed in the `physics` profiler slot, the camera, collider sync and collision work in `scene_sys`. Physics runs after `on_update`, so game code sees the bodies as the previous frame left them: an impulse applied in `on_update` takes effect in the same frame's steps, and code that reacts to physics (the demo's bounce sound) reads the bodies in `on_update`, one frame after the step that moved them.

### Per-Frame Draw

`scene_draw()` runs after update, inside the RDP frame:

```
scene_draw(scene)
├── View copies              — scene->camera and scene->lighting copied to the
│                              pinned view (scene_view_camera/light)
├── Background               — sky_draw() when the sky covers the screen,
│                              otherwise rdpq_clear(bg_color)
├── rdpq_clear_z(ZBUF_MAX)   — Clear Z-buffer
├── scene->on_draw()         — Scene-level rendering (floor, shadows, 3D)
├── Per-object on_draw()     — Visible objects with a callback ("objects" profiler slot)
└── scene->on_post_draw()    — After all 3D: particles, HUD, menu
```

The sky replaces the colour clear rather than drawing over it, so scenes never call `sky_draw()` themselves. `scene_manager_draw()` draws the transition fade on top.

**Draw with the view copies.** `scene_draw()` copies the scene's camera and lighting once per frame to a pinned D-cache colour, and object `on_draw` callbacks receive the copies as `cam` and `light`. In `on_draw` and `on_post_draw`, pass the renderers `scene_view_camera()` and `scene_view_light()` rather than `&scene->camera` / `&scene->lighting`. The renderers read them per vertex, per object and per face group, and inside the `Scene` struct their colours move whenever the struct or the static data before it changes size: S7 grew `SceneObject` by 16 bytes, which moved them onto the render stack's cache lines and cost every mesh step 6–9 % (D35). Changes made to `scene->camera` during a draw reach the renderers the next frame; update code keeps using `scene->camera` as before.

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

Objects within a scene have transform, state flags, an optional collider and physics body, and optional callbacks:

```c
typedef struct SceneObject {
    vec3_t position;
    vec3_t rotation;         // Euler angles (radians)
    vec3_t scale;

    bool active;             // Participates in update
    bool visible;            // Participates in draw
    uint16_t flags;          // SCENE_OBJ_* bits

    int collider_handle;     // in the scene's CollisionWorld (-1 = none)
    int body_handle;         // in the scene's PhysicsWorld (-1 = none)
    vec3_t collider_offset;  // collider centre - position, set on attach

    void *data;              // Type-specific data pointer
    void (*on_update)(struct SceneObject *obj, float dt);
    void (*on_draw)(struct SceneObject *obj, const Camera *cam,
                    const LightConfig *light);
} SceneObject;
```

### Object Management

```c
// Add an object (copied into the scene; returns index or -1 if full). The
// copy starts without a collider or body, whatever the handles held.
int idx = scene_add_object(scene, &obj);

// Access by index (NULL if out of range)
SceneObject *obj = scene_get_object(scene, idx);

// Next / previous object whose flags include all of SCENE_OBJ_SELECTABLE,
// wrapping; from = -1 starts at the first (or last); -1 if none matches
int next = scene_find_object(scene, current, +1, SCENE_OBJ_SELECTABLE);

// Remove: removes the object's collider and body, shifts later objects down
scene_remove_object(scene, idx);
```

Object management, attachments and the per-frame sync live in `src/scene/scene_objects.c`, which has no rendering code; `tests/host/test_scene.c` covers it on the host.

### Colliders and Physics Bodies

An object can own one collider and one physics body. Create them in the scene's worlds, then attach them:

```c
int idx = scene_add_object(scene, &obj);
scene_object_set_collider(scene, idx, collision_add_sphere(&scene->collision,
    obj.position, 20.0f, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL));
scene_object_set_body(scene, idx, physics_body_add(&scene->physics, &PHYSICS_DEF_BALL, obj.position));
```

- **The object follows its body.** After the physics step, `scene_sync_bodies()` copies each body's position to its object ([PHYSICS.md](PHYSICS.md)). To move a body by hand, set `body->kinematic` and write its position: the simulation leaves it alone and the object still follows it. The demo holds the ball this way while it is being moved.
- **The collider follows its object.** `scene_sync_colliders()` puts the collider's centre at `position + collider_offset`, the offset measured when it was attached. A box keeps its size and is only rewritten when the object moved. Rotation and scale are not applied: resize a collider yourself with `collision_update_*()`.
- **Ownership.** Removing the object removes both. Attaching another collider or body removes the previous one; attaching -1 removes it. A collider or body belongs to one object at most: attach each only once. Colliders that belong to no object (the demo's ground) stay where they were added.
- **Handles start empty.** `scene_add_object()` sets both handles to -1: a zero-initialised object would otherwise own collider 0 and body 0 and remove them when it goes.
- **Layers.** A body raycasts the layers in its `collision_layer_mask` (`COLLISION_LAYER_ENV` by default) to find the ground. Put an object's own collider on another layer, or the body's ray hits its own collider: the demo's ball has a sphere on `COLLISION_LAYER_DEFAULT` only.

### Object Flags

`flags` says which passes and queries include the object. The engine defines:

| Flag | Meaning |
|------|---------|
| `SCENE_OBJ_CASTS_SHADOW` | drawn by the scene's shadow pass |
| `SCENE_OBJ_SELECTABLE` | can be picked (the demo's object mode) |
| `SCENE_OBJ_FLAG_USER` and up | free for game-specific use |

The scene draws shadows itself in `on_draw` (the demo: after the floor, for every visible object with `SCENE_OBJ_CASTS_SHADOW`), and cycles a selection with `scene_find_object()`. Objects added at run time take part as soon as they carry the flag: before S7 the demo drew shadows and cycled selection over the first N objects, so its physics ball, added later, had neither (D20).

## Scene Manager

The scene manager controls which scene is active and handles transitions.

### Setup

```c
SceneManager scene_mgr;
scene_manager_init(&scene_mgr);
scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);
```

### Game Loop Integration

`engine_run()` (`src/engine/engine.c`, [ENGINE.md](ENGINE.md)) runs a variable-timestep loop: `dt` is the display's time between presented frames (`display_get_delta_time()`), capped at 0.1 s. Simplified:

```c
while (1) {
    float dt = display_get_delta_time();       // capped at ENGINE_MAX_DT (0.1 s)
    surface_t *fb = display_get();             // a free framebuffer (triple buffering)
    snd_update(dt);                            // the audio mix (poll point: AUDIO.md)
    input_poll(...);                           // this vblank's controller read (INPUT.md)
    scene_manager_update(&scene_mgr, dt);      // on_update, physics, colliders, camera

    rdpq_attach(fb, zbuf);                     // zbuf = engine_zbuf()
    scene_manager_draw(&scene_mgr);            // scene_draw(), then the transition fade
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
| Max physics bodies per scene | 32 (`PHYSICS_MAX_BODIES`) |
| Max collision results | 32 |

## Memory

Shared resources (framebuffers, Z-buffer, fonts, audio) persist across scene transitions; everything a scene loads should be freed by its cleanup. Measured numbers (heap in use after the demo loads, framebuffer and Z-buffer sizes, stack peak) are in [BENCHMARKS.md](BENCHMARKS.md), and the Memory overlay page shows them live, including the heap delta since boot ([PROFILING.md](PROFILING.md)). Design for 4 MB of RDRAM even though the Analogue 3D reports 8 MB.

## Demo Scene

The demo scene (`src/scenes/demo_scene.c`) exercises most of the engine:

- Six mesh objects: textured rotating cube, two pillars, platform, rotating pyramid and a static sphere (the curved-surface test object), plus three billboards (a marker and two trees)
- B spawns (then re-launches) a physics ball, a scene object with a body and a sphere collider that casts a shadow and can be selected, and fires particle bursts on the pillar tops; torch flames burn while point lights are on
- Object selection (Z, D-Left/Right) and move/rotate/scale (A cycles the mode, stick and C-buttons manipulate). Colliders move with their objects: the platform's collider is its box, so the ball lands on the platform wherever it is moved. A ball being transformed is held in place (kinematic) and drops when the mode ends
- The Start menu: background, lighting, shadows, point lights, atmosphere presets, camera mode and collision, frame rate, sound, control remapping, Reset Scene, and the Debug tab
- Its options (`src/ui/settings.c`) are applied when they change, and all of them after every (re)init, so the scene always matches the menu (D27)
- HUD: title, object counts, triangles/uploads/collisions/raycast distance, FPS and CPU time, camera mode and position

The benchmark scene (`src/scenes/benchmark_scene.c`) is a second, menu-less scene; see [BENCHMARKS.md](BENCHMARKS.md).

## Source Files

| File | Purpose |
|------|---------|
| [src/scene/scene.h](../src/scene/scene.h) | Scene, SceneObject, SceneManager types |
| [src/scene/scene.c](../src/scene/scene.c) | Scene lifecycle, background, manager, transitions, soft reset |
| [src/scene/scene_objects.c](../src/scene/scene_objects.c) | Objects, their colliders and bodies, flags, the per-frame sync (host-tested) |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | Demo scene implementation |
| [src/scenes/benchmark_scene.c](../src/scenes/benchmark_scene.c) | Benchmark scene |
| [src/main.c](../src/main.c) | Creates the scene manager and the Start menu, switches scenes; `engine_run()` ([src/engine/engine.c](../src/engine/engine.c)) runs the frame loop |
