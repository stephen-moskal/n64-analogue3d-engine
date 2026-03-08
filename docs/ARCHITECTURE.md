# Architecture Overview

N64 hardware fundamentals, the libdragon software stack, and engine design.

## N64 Hardware

### CPU: VR4300

- **Architecture:** MIPS III 64-bit
- **Clock:** 93.75 MHz
- **Cache:** 16KB instruction, 8KB data
- **Memory:** 4MB RDRAM (8MB with Expansion Pak)

The CPU handles game logic, 3D math (transforms, projection, lighting, culling), and orchestrates the RCP.

### RCP (Reality Co-Processor)

#### RSP (Reality Signal Processor)
- **Purpose:** Geometry transformation, lighting, audio
- **Clock:** 62.5 MHz
- **Memory:** 4KB instruction, 4KB data (DMEM)
- **Architecture:** Vector processor (8x 16-bit SIMD)

The RSP runs microcode for vertex transformation and audio mixing. The engine currently does vertex transforms on the CPU, but RSP-accelerated rendering (via tiny3d or custom microcode) is a future option.

#### RDP (Reality Display Processor)
- **Purpose:** Rasterization, texturing, blending
- **TMEM:** 4KB texture cache
- **Features:** Triangle rasterization, texture mapping, Z-buffering, anti-aliasing, alpha blending

### Memory Map

```
0x00000000 - 0x003FFFFF  RDRAM (4MB/8MB)
0x04000000 - 0x040FFFFF  RSP DMEM/IMEM
0x04400000 - 0x044FFFFF  Video Interface
0x04600000 - 0x046FFFFF  Peripheral Interface
0x10000000 - 0x1FBFFFFF  Cartridge ROM
```

### Display

| Parameter | Value |
|-----------|-------|
| Resolution | 320x240 (LO) or 640x480 (HI) |
| Color depth | 16-bit (RGBA5551) or 32-bit (RGBA8888) |
| Framebuffer | In RDRAM |
| Refresh | 60Hz (NTSC) / 50Hz (PAL) |

## libdragon Stack

### Key Subsystems

#### Display

```c
display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
```

Manages framebuffers, V-blank synchronization, triple buffering.

#### RDPQ (RDP Queue)

```c
rdpq_init();
rdpq_attach(framebuffer, depth_buffer);
// ... draw commands ...
rdpq_detach_show();
```

High-level RDP command interface with automatic state management and command batching.

#### Joypad

```c
joypad_init();
joypad_poll();
joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
```

Supports N64 and GameCube controllers. Provides analog stick values, held/pressed/released button states.

#### ROM Filesystem (DFS)

```c
dfs_init(DFS_DEFAULT_LOCATION);
sprite_t *spr = sprite_load("rom:/texture.sprite");
```

Assets in `filesystem/` are packed into a DFS archive and appended to the ROM. Accessed at runtime via `rom:/` prefix.

### Memory Management

```c
void *ptr = malloc(size);              // Cached memory (general use)
void *ptr = malloc_uncached(size);     // Uncached (for DMA buffers)
surface_t zbuf = surface_alloc(FMT_RGBA16, 320, 240);  // Surface allocation
```

DMA buffers (used by RSP) must be uncached and 8-byte aligned.

## Engine Architecture

### Module Dependency Graph

```
main.c
├── input/input         [controller polling]
├── ui/text             [font rendering]
├── ui/menu             [settings menu overlay]
│   └── ui/text
├── scene/scene         [scene manager, lifecycle, transitions]
│   ├── render/camera   [multi-mode camera, 3D math, frustum, collision]
│   ├── render/lighting [Blinn-Phong calculation]
│   ├── collision/collision [collision detection, raycasting]
│   └── render/texture  [dynamic texture slot management]
├── audio/audio         [audio mixer, SFX/BGM playback] (WIP)
│   └── audio/sound_bank [sound event definitions]
└── scenes/demo_scene   [demo scene: multi-object, selection, manipulation]
    ├── render/cube     [cube geometry definition (textured)]
    │   └── render/mesh [generic mesh rendering]
    │       ├── render/camera
    │       ├── render/lighting
    │       └── render/texture
    ├── render/mesh_defs [shape library: pillar, platform, pyramid]
    │   └── render/mesh
    ├── render/billboard [camera-facing textured quads]
    │   └── render/mesh
    └── scene/scene
```

### Initialization Order

```c
// System init (main.c)
debug_init_isviewer();      // Debug output (ISViewer)
debug_init_usblog();        // Debug output (USB)
display_init(...);          // Framebuffers
rdpq_init();                // RDP command queue
dfs_init(...);              // ROM filesystem
input_init();               // Joypad
text_init();                // Load fonts
menu_init(&menu, title);    // Menu state
surface_alloc(...);         // Z-buffer (shared across scenes)

// Scene manager init
scene_manager_init(&mgr);
scene_manager_switch(&mgr, demo_scene_get(), TRANSITION_CUT, 0);

// Inside scene_init() (called by manager):
collision_world_init();     // Reset collision world
lighting_init(&config);     // Light parameters
texture_load_slot();        // Per-scene textures
scene->on_init();           // Scene-specific setup
  camera_init(&cam, &preset); // Camera matrices
  cube_init();                // Model geometry (demo scene)
  collision_add_*();          // Add colliders
```

### Frame Loop (Variable Timestep)

Game logic runs once per rendered frame using the actual elapsed time (`dt`). Frame rate is selectable via menu (30 or 60 FPS). At 60 FPS, objects update 60 times per second for smooth motion; at 30 FPS, a busy-wait limiter skips every other VBlank.

```c
uint32_t last_ticks = TICKS_READ();

while (1) {
    // Measure real elapsed time
    uint32_t now = TICKS_READ();
    float dt = TICKS_DISTANCE(last_ticks, now) / (float)TICKS_PER_SECOND;
    last_ticks = now;

    // Update game logic once per frame
    scene_manager_update(&mgr, dt);
    //   -> scene_update(current, dt)
    //      -> per-object on_update(dt)
    //      -> scene->on_update(dt) [input, game logic]
    //      -> camera_update()
    //      -> collision_test_all()

    // Render
    surface_t *fb = display_get();    // Blocks until VBlank (60Hz cap)
    rdpq_attach(fb, &zbuf);
    scene_manager_draw(&mgr);
    //   -> scene_draw(current)
    //      -> rdpq_clear(bg_color), rdpq_clear_z(ZBUF_MAX)
    //      -> scene->on_draw() [floor, 3D geometry]
    //      -> per-object on_draw()
    //      -> scene->on_post_draw() [HUD, overlays]
    //   -> transition overlay (if transitioning)
    rdpq_detach_show();

    // Busy-wait frame limiter (for 30 FPS target)
    if (engine_target_fps > 0) { /* spin until target frame time */ }
}
```

**Why variable timestep:** A previous fixed-timestep accumulator (30Hz logic) caused every other frame at 60 FPS to be an identical duplicate — the accumulator hadn't reached the 33ms threshold, so no logic update ran. Motion was effectively 30Hz regardless of display rate, making 30 and 60 FPS feel identical. Variable timestep ensures every rendered frame has a unique logic update.

## Rendering Pipeline

See [RENDERING.md](RENDERING.md) for the full pipeline documentation.

### Summary

```
CPU: Model Matrix -> MVP = VP * Model -> Per-face: Cull + Light + Transform
RDP: Rasterize -> Texture Sample -> Z-Test -> Framebuffer
```

- Software transforms on CPU, hardware rasterization on RDP
- Hardware 16-bit Z-buffer (replaced painter's algorithm)
- `TRIFMT_ZBUF_TEX` vertex format: `{X, Y, Z, S, T, INV_W}`

## Lighting Model

Per-face Blinn-Phong lighting computed on the CPU:

```
color = ambient
      + diffuse * max(0, dot(normal, light_dir))
      + specular * pow(max(0, dot(normal, half_vec)), 32)
```

Where `half_vec = normalize(light_dir + view_dir)`.

### Default Light Configuration

| Component | Value | Notes |
|-----------|-------|-------|
| Ambient | (0.15, 0.15, 0.20) | Slightly blue tint |
| Diffuse | (0.85, 0.80, 0.70) | Warm white |
| Direction | normalized(1, 1, 1) | Upper-right-front |
| Specular | 0.5 intensity, shininess ~32 | Blinn-Phong |

The lit color modulates the per-face base color and texture:
```c
final_pixel = texture_sample * (face_base_color * lighting) / 255
```

## Collision Detection

See [COLLISION.md](COLLISION.md) for full documentation.

### Summary

- Layer-based collision with bitmask filtering
- Sphere and AABB collider types
- Broadphase AABB culling + narrowphase shape tests
- Raycasting (sphere, AABB, triangle)
- Overlap queries
- Up to 64 colliders, 32 results per frame

## Scene System

See [SCENE_SYSTEM.md](SCENE_SYSTEM.md) for full documentation.

### Summary

- Code-defined scenes with callback lifecycle (init/update/draw/post_draw/cleanup)
- Each scene owns Camera, LightConfig, CollisionWorld
- Scene manager with transitions (cut, fade-black, fade-white)
- Up to 32 objects and 16 textures per scene
- Per-object update/draw callbacks via SceneObject
- Draw order: on_draw (floor/3D) → per-object on_draw → on_post_draw (HUD/overlays)
- Support for both independent and shared-coordinate scenes

## Camera System

See [CAMERA.md](CAMERA.md) for full documentation.

### Summary

- Three modes: orbital, fixed, follow
- Camera collision via raycasting against environment layer
- Perspective projection with frustum culling
- Dirty flag optimization

## Text Rendering

Built on libdragon's `rdpq_text` system:

```c
typedef struct {
    float x, y;
    int16_t width, height;
    uint8_t font_id;
    color_t color;
    rdpq_align_t align;
    rdpq_valign_t valign;
    rdpq_textwrap_t wrap;
} TextBoxConfig;
```

### Available Fonts

| ID | Constant | Type |
|----|----------|------|
| 1 | `FONT_DEBUG_MONO` | Monospace (debug, stats) |
| 2 | `FONT_DEBUG_VAR` | Variable-width (titles) |

### API

```c
text_draw(&config, "static string");
text_draw_fmt(&config, "formatted %d", value);
```

## Memory Budget (4MB)

| Resource | Size | Notes |
|----------|------|-------|
| Framebuffer x3 | ~450KB | 320x240 x 2 bytes x 3 |
| Z-buffer | ~150KB | 320x240 x 2 bytes |
| Textures (sprites) | ~12KB | 6 x 32x32 RGBA16 |
| TMEM per frame | 4KB max | RDP on-chip texture cache |
| Code + data | ~288KB | Current ROM size |
| Audio buffers | ~64KB | Reserved for future |
| **Available** | **~3MB** | For game assets and logic |

With Expansion Pak (8MB), an additional 4MB is available. Shared resources (framebuffers, Z-buffer) persist across scene transitions. Per-scene textures load/unload with the scene.

## Optimization Notes

### CPU
- Dirty flag on camera (skip matrix recompute when unchanged)
- Frustum culling rejects entire objects before per-face work
- Backface culling skips ~50% of faces on convex objects
- Static-static collision pairs skipped
- Broadphase AABB culling before narrowphase shape tests

### RDP
- Batch triangles by render state to minimize mode changes
- One texture upload per face (6 per cube) — batch by texture for multiple objects
- Z-buffer eliminates need for CPU-side depth sorting
- Transition overlays use 1-cycle mode triangles (hardware-safe)

### Memory
- Use `surface_alloc()` for Z-buffer (allocated once, reused every frame)
- Sprite slots are loaded once at scene init, uploaded to TMEM per-frame as needed
- Per-scene texture load/free prevents accumulation
- Menu/text configs are stack-allocated or static

## Source Files

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, display/input/menu init, variable-timestep game loop |
| `src/render/camera.c/h` | Multi-mode camera, 3D math, frustum culling, collision |
| `src/render/mesh.c/h` | Generic mesh type, builder API, universal draw function |
| `src/render/mesh_defs.c/h` | Shape library: pillar, platform, pyramid factory functions |
| `src/render/cube.c/h` | Cube geometry (textured, built on Mesh) |
| `src/render/floor.c/h` | Checkered floor grid (dynamic, Z-biased) |
| `src/render/lighting.c/h` | Blinn-Phong lighting |
| `src/render/texture.c/h` | Texture loading, TMEM management, dynamic slots |
| `src/render/billboard.c/h` | Billboard system: camera-facing textured quads |
| `src/math/vec3.h` | Vector math library (header-only) |
| `src/collision/collision.c/h` | Collision detection, raycasting, overlap queries |
| `src/scene/scene.c/h` | Scene lifecycle, manager, transitions, per-object callbacks |
| `src/scenes/demo_scene.c/h` | Demo scene: mesh objects, billboards, selection, HUD |
| `src/input/input.c/h` | Controller input |
| `src/ui/text.c/h` | Text rendering |
| `src/ui/menu.c/h` | Tabbed menu system (L/R tabs, snapshot/revert) |
| `src/audio/audio.c/h` | Audio mixer, SFX/BGM playback (WIP) |
| `src/audio/sound_bank.c/h` | Sound event definitions and path mapping |

## Documentation Index

| Document | Contents |
|----------|----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This file — system overview |
| [RENDERING.md](RENDERING.md) | Rendering pipeline details |
| [TEXTURES.md](TEXTURES.md) | Texture pipeline and TMEM |
| [CAMERA.md](CAMERA.md) | Camera modes, math, frustum, collision |
| [COLLISION.md](COLLISION.md) | Collision detection and raycasting |
| [SCENE_SYSTEM.md](SCENE_SYSTEM.md) | Scene/world management |
| [MENU_SYSTEM.md](MENU_SYSTEM.md) | Menu overlay system |
| [INPUT.md](INPUT.md) | Controller input handling |
| [MESH_SYSTEM.md](MESH_SYSTEM.md) | Mesh/model abstraction |
| [ROADMAP.md](ROADMAP.md) | Development roadmap and milestones |
| [SETUP.md](SETUP.md) | Environment setup guide |
| [WORKFLOW.md](WORKFLOW.md) | Development workflow |
