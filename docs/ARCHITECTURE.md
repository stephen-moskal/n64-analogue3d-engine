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
├── input/action        [action mapping, joypad polling, context management]
├── input/input         [camera input adapter, reads from action API]
├── ui/text             [font rendering]
├── ui/menu             [settings menu overlay]
│   └── ui/text
├── scene/scene         [scene manager, lifecycle, transitions]
│   ├── render/camera   [multi-mode camera, 3D math, frustum, collision]
│   ├── render/lighting [Blinn-Phong calculation]
│   ├── collision/collision [collision detection, raycasting]
│   └── render/texture  [dynamic texture slot management]
├── audio/audio         [audio mixer, SFX/BGM playback]
│   └── audio/sound_bank [sound event definitions]
├── physics/physics     [physics simulation, semi-fixed timestep, gravity, bounce]
│   └── collision/collision
└── scenes/demo_scene   [demo scene: multi-object, selection, manipulation]
    ├── render/cube     [cube geometry definition (textured)]
    │   └── render/mesh [generic mesh rendering]
    │       ├── render/camera
    │       ├── render/lighting
    │       ├── render/texture
    │       └── render/atmosphere
    ├── render/mesh_defs [shape library: pillar, platform, pyramid, sphere]
    │   └── render/mesh
    ├── render/billboard [camera-facing textured quads]
    │   └── render/mesh
    ├── render/shadow    [blob + projected shadow casting]
    │   ├── render/camera
    │   ├── render/lighting
    │   └── render/mesh
    ├── render/particle  [particle system, direct RDP renderer]
    │   ├── render/camera
    │   └── render/atmosphere
    ├── render/atmosphere [fog config, sky gradient, 7 presets]
    ├── physics/physics   [physics bodies, gravity, bounce response]
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
action_init();              // Joypad + action mapping (replaces input_init)
text_init();                // Load fonts
menu_init(&menu, title);    // Menu state
snd_init();                 // Audio mixer
atmosphere_init();          // Fog/sky global state
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
  physics_world_init();       // Physics world (optional, scene-local)
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
    //      -> scene->on_post_draw() [particles, HUD, overlays]
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

Per-face Blinn-Phong lighting computed on the CPU with configurable directional sun, up to 4 point lights, and shadow casting.

### Lighting Formula

```
color = ambient
      + sun_color * sun_intensity * max(0, dot(normal, light_dir))          // diffuse
      + specular_intensity * fast_pow_int(max(0, dot(normal, half_vec)), shininess)  // specular
      + Σ point_light_contribution                                          // point lights
```

Where `half_vec = normalize(light_dir + view_dir)` and `fast_pow_int` uses binary exponentiation (5 multiplies for shininess=32 vs costly `powf`).

### Point Lights

Up to 4 point lights per scene (`MAX_POINT_LIGHTS`). Each has position, color, intensity, radius, and active flag. Smooth quadratic attenuation reaching zero at the radius boundary:

```
attenuation = (1 - (dist/radius)²)² * intensity
```

Optimizations: early-out when `distance² >= radius²` (before `sqrtf`), diffuse only (no specular per point light to stay within CPU budget).

### Default Light Configuration

| Component | Value | Notes |
|-----------|-------|-------|
| Sun Color | (0.85, 0.80, 0.70) | Warm white |
| Sun Intensity | 1.0 | Brightness multiplier [0.0, 2.0] |
| Direction | normalized(1, 1, 1) | Upper-right-front |
| Ambient | (0.15, 0.15, 0.20) | Slightly blue tint |
| Specular | 0.5 intensity, shininess 8 | Blinn-Phong |
| Point Lights | 0 active | Up to 4 per scene |
| Shadows | OFF | Blob or projected modes available |

The lit color modulates the per-face base color and texture:
```c
final_pixel = texture_sample * (face_base_color * lighting) / 255
```

## Shadow System

Two shadow modes rendered after the floor and before objects. Configured via `ShadowConfig` in `LightConfig`.

### Shadow Modes

| Mode | Cost | Visual |
|------|------|--------|
| `SHADOW_OFF` | 0 tris | No shadows |
| `SHADOW_BLOB` | 2 tris/object | Dark quad under each object, scaled by bounding radius |
| `SHADOW_PROJECTED` | ~10 tris/object | Mesh silhouette projected onto floor plane along light direction |

### RDP State

```c
rdpq_set_mode_standard();           // 1-cycle mode (CRITICAL for hardware)
rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
rdpq_mode_zbuf(true, false);        // Z-read ON, Z-write OFF
```

- **Z-read ON**: shadows respect floor depth, don't render through objects
- **Z-write OFF**: shadows don't occlude objects drawn afterward
- Shadow color derived from `ShadowConfig.darkness` (0.0 = invisible, 1.0 = fully black)

### Projected Shadow Math

Per-vertex planar projection along the light direction onto `y = floor_y`:

```c
float t = (world_vertex.y - floor_y) / light_direction.y;
shadow_x = world_vertex.x - light_direction.x * t;
shadow_z = world_vertex.z - light_direction.z * t;
// Render at (shadow_x, floor_y + 0.01, shadow_z)
```

Draw order: floor → shadows → objects → particles → HUD

## Particle System

Emitter-based particle system with pool-based allocation and a direct RDP batch renderer that bypasses the `mesh_draw()` pipeline for maximum throughput.

### Architecture

```
ParticleEmitterDef (static const, data-driven)
    ↓ particle_emitter_create()
ParticleEmitter (runtime: position, pool slice, spawn state)
    ↓ particle_emitter_burst() or continuous spawn
Particle pool[128] (global, contiguous slices per emitter)
    ↓ particle_update(dt)
Physics: gravity, drag, position integration, color/scale interpolation
    ↓ particle_draw(cam)
Direct RDP emission: camera-facing quads, batch by blend mode
```

### Renderer (Direct RDP Emission)

The particle renderer follows the `floor_draw()` pattern — not `mesh_draw()`. This is critical for performance:

| Approach | `rdpq_set_mode_standard()` calls | Why |
|----------|----------------------------------|-----|
| `mesh_draw()` per particle | 1 per particle (up to 128) | Mode reset per material boundary |
| Direct RDP emission | 1 total (all particles) | Mode set once, batch all triangles |

Steps:
1. Compute camera basis vectors (right, up) once per frame
2. Set RDP mode once: `rdpq_set_mode_standard()`, `RDPQ_COMBINER_FLAT`, Z-read ON / Z-write OFF, `RDPQ_BLENDER_ADDITIVE`
3. Per alive particle: compute 4 quad corners from `position ± right*scale ± up*scale`
4. Transform corners through `cam->vp` → screen space (same clip/guard-band/depth-clamp as mesh_draw)
5. Emit 2 triangles via `rdpq_triangle(&TRIFMT_ZBUF, ...)` (3 floats: X, Y, Z — no texture)

### ParticleEmitterDef (Effect Definition)

Data-driven struct — define effects as `static const` and reuse across scenes:

| Field | Type | Description |
|-------|------|-------------|
| `burst_count` | int | Particles spawned per burst call |
| `spawn_rate` | float | Particles/sec for continuous mode |
| `lifetime_min/max` | float | Random lifetime range (seconds) |
| `velocity_min/max` | vec3_t | Per-axis random initial velocity |
| `gravity` | vec3_t | Acceleration per second² |
| `drag` | float | Velocity damping (0=none, 1=full stop) |
| `color_start/end` | uint8_t[4] | RGBA at birth/death, linearly interpolated |
| `scale_start/end` | float | Quad half-size at birth/death |
| `spawn_shape` | enum | POINT or SPHERE (with radius) |
| `blend_mode` | enum | ADDITIVE or ALPHA |

### API

```c
void particle_init(void);
void particle_cleanup(void);

int  particle_emitter_create(const ParticleEmitterDef *def, vec3_t pos, int pool_size);
void particle_emitter_destroy(int handle);
void particle_emitter_set_position(int handle, vec3_t position);
void particle_emitter_burst(int handle);           // Spawn burst_count particles
void particle_emitter_set_active(int handle, bool); // Enable continuous spawning

void particle_update(float dt);                    // Physics + interpolation
void particle_draw(const Camera *cam);             // Direct RDP batch render
int  particle_alive_count(void);                   // Active particle count
```

### Pool Compaction

When an emitter is destroyed, `particle_emitter_destroy()` reclaims pool space by scanning active emitters for the highest pool endpoint and resetting `pool_allocated` to that value. This prevents permanent pool fragmentation when emitters are repeatedly created and destroyed (e.g., torch particles toggling with point light settings). Only 8 iterations (PARTICLE_MAX_EMITTERS) — negligible cost.

### Performance

- **Pool**: 128 particles max, 8 emitters max, zero heap allocation
- **Triangles**: 2 per alive particle (worst case 256 tris at full pool)
- **RDP mode calls**: 1 per frame (all particles share one blend mode pass)
- **TMEM cost**: Zero (flat-colored quads, no textures)
- **Per-particle frustum culling** via `camera_sphere_visible()`
- **Color batching**: `rdpq_set_prim_color()` only called when color changes
- **Fog integration**: CPU-side fog dimming (multiply RGBA by `1 - fog_factor`), since hardware fog conflicts with additive blender

## Fog & Atmosphere System

Distance-based fog and configurable sky gradients for atmospheric depth cues and mood setting. Uses a hybrid approach: hardware RDP fog for mesh geometry, CPU-side fog for floor tiles and particles.

### Architecture

```
AtmospherePreset (static const, data-driven)
    ↓ atmosphere_apply_preset()
FogConfig (global: enabled, color, near, far)
SkyConfig (global: enabled, band_count, band_colors[5])
    ↓
Renderers query atmosphere state per frame:
  mesh_draw()     → hardware fog (RDPQ_FOG_STANDARD via shade alpha)
  floor_draw()    → CPU fog (per-tile color blend toward fog color)
  particle_draw() → CPU fog (RGBA dimming for additive blend compat)
  sky_draw()      → gradient fill rectangles (interpolated strips)
```

### Hardware Fog (mesh_draw)

The RDP's built-in fog blender (`RDPQ_FOG_STANDARD`) uses the shade alpha channel as a per-vertex fog mix factor:

```
output = vertex_color * shade_alpha + fog_color * (1 - shade_alpha)
```

When fog is enabled, `mesh_draw()` switches vertex formats and combiners:

| Fog | Material | Format | Floats/vert | Combiner |
|-----|----------|--------|-------------|----------|
| OFF | Flat | `TRIFMT_ZBUF_TEX` | 6 | `RDPQ_COMBINER_FLAT` |
| OFF | Textured | `TRIFMT_ZBUF_TEX` | 6 | `RDPQ_COMBINER_TEX_FLAT` |
| ON | Flat | `TRIFMT_ZBUF_SHADE` | 7 | `RDPQ_COMBINER_SHADE` |
| ON | Textured | `TRIFMT_ZBUF_SHADE_TEX` | 10 | `RDPQ_COMBINER_TEX_SHADE` |

Per-vertex shade data: `{R, G, B, A}` where RGB = lit color (0.0-1.0 range), A = `1.0 - fog_factor` (1.0 = fully visible, 0.0 = fully fogged). The fog factor is computed from `clip.w` (camera-space depth) using linear interpolation between fog near and far planes.

### CPU Fog (floor + particles)

The floor grid shares vertices between adjacent tiles (prevents sub-pixel gaps). Shade-based formats require per-vertex color, but adjacent tiles need different checker colors — incompatible. Solution: per-tile average depth → `fog_blend_color()` → `rdpq_set_prim_color()`.

The floor also computes per-tile point light contributions (`floor_point_light_add()`). When point lights and/or fog are active, the floor uses a per-tile rendering path that computes lighting at each tile center and blends fog per-tile. When neither is active, a fast batched path renders all light/dark tiles in two passes with only 2 `rdpq_set_prim_color()` calls total.

Particles use `RDPQ_BLENDER_ADDITIVE`. Combining with `RDPQ_FOG_STANDARD` requires a 2-pass blender (assertion failure). Also, adding fog color via additive blend brightens distant particles (wrong). Solution: multiply RGBA by `1 - fog_factor` — distant particles fade to black (invisible in additive).

### Sky Gradient

`sky_draw()` renders a smooth vertical gradient using 60 horizontal fill rectangle strips (4px each). Band colors from `SkyConfig` are treated as evenly-spaced gradient stops; each strip's color is linearly interpolated between the two nearest stops. This produces smooth transitions instead of hard-edged flat bands.

Critical design rule: **bottom sky band = fog color = bg_color** in every preset. This creates seamless blending from sky → fog → background clear color.

### Presets

7 built-in presets, each configuring fog + sky + background color + linked lighting hints:

| Preset | Fog Near | Fog Far | Sky Bands | Lighting Hint | Mood |
|--------|----------|---------|-----------|---------------|------|
| Clear Day | 400 | 1400 | 4 (deep blue → pale blue) | Full sun, neutral | Bright, open |
| Overcast | 250 | 1000 | 3 (grey tones) | Dim sun, cool ambient | Muted |
| Foggy | 100 | 600 | 2 (grey) | Low sun, high ambient | Low visibility |
| Dense Fog | 50 | 350 | 2 (white-grey) | Minimal sun, fog ambient | Very close |
| Sunset | 300 | 1200 | 5 (purple → orange) | Golden sun, warm ambient | Warm dramatic |
| Dusk | 200 | 900 | 4 (dark purple) | Dim sun, cool-purple | Twilight |
| Night | 300 | 1000 | 3 (near-black) | Near-zero sun, dark blue | Dark |

Each preset includes a `LightingHint` with `sun_intensity`, `ambient` color, and `sun_color`. When applied, the demo scene reads these hints and adjusts the `LightConfig` accordingly, creating cohesive atmosphere-lighting combinations (e.g., Night mode dims the sun to 5% and shifts ambient to dark blue).

### Performance

- **Fog OFF**: Zero overhead — identical code paths, formats, and combiners as before
- **Fog ON (mesh)**: +1 float per vertex (shade alpha), 2-cycle mode (RDP throughput halved, but low tri count)
- **Fog ON (floor)**: ~100 `rdpq_set_prim_color()` calls vs 2 unfogged (color dedup reduces actual calls)
- **Fog ON (particles)**: +1 VP multiply per particle for fog factor
- **Sky**: 60 fill rectangles per frame — negligible RDP cost

### Menu Integration

ENVIRON tab (tab 3) with 6 items: Preset (8 options), Fog On/Off, Fog Near, Fog Far, Fog Color, Sky On/Off. Named presets auto-enable fog+sky and sync menu toggles. Custom mode allows individual control.

## Action Mapping System

Data-driven input abstraction that decouples game logic from physical button assignments. Game code queries named actions instead of raw buttons, enabling runtime remapping and per-scene control schemes.

### Architecture

```
joypad_poll()  →  action_update()  →  action_pressed/held/released()
                      ↓                         ↑
              PhysicalButton → GameAction    scene logic queries
              (via ActionContext bindings)    actions, not buttons
```

### Key Types

| Type | Purpose |
|------|---------|
| `PhysicalButton` | Enum of 13 N64 buttons (A, B, Z, L, R, D-pad×4, C-buttons×4) |
| `GameAction` | Enum of 11 remappable actions (Confirm, Cancel, Select, Camera, Cycle, Zoom, Shift) |
| `ActionContext` | Named binding set with per-context analog deadzone and sensitivity |

### Design Rules

- **Start button**: Always toggles menu — hardcoded, not remappable (system-level)
- **Menu navigation**: D-pad, A/B, L/R in `menu.c` stay hardcoded (standard UI convention)
- **Analog stick**: Sensitivity/deadzone configurable per context, but not remapped to buttons
- **InputState preserved**: `input_update()` is a thin adapter reading from the action API — camera code unchanged

### Contexts

An `ActionContext` defines a complete set of button-to-action bindings:

```c
const ActionContext ACTION_CTX_EXPLORATION = {
    .name = "Exploration",
    .bindings = {
        [ACTION_CONFIRM]       = BTN_A,
        [ACTION_CANCEL]        = BTN_B,
        [ACTION_SELECT_MODE]   = BTN_Z,
        [ACTION_CAM_MODE_NEXT] = BTN_R,
        [ACTION_CAM_MODE_PREV] = BTN_L,
        [ACTION_CYCLE_NEXT]    = BTN_D_RIGHT,
        [ACTION_CYCLE_PREV]    = BTN_D_LEFT,
        [ACTION_ZOOM_IN]       = BTN_C_UP,
        [ACTION_ZOOM_OUT]      = BTN_C_DOWN,
        [ACTION_SHIFT_UP]      = BTN_C_RIGHT,
        [ACTION_SHIFT_DOWN]    = BTN_C_LEFT,
    },
    .analog_deadzone = 8.0f,
    .analog_sensitivity = 0.002f,
};
```

Developers define new contexts as `static const` data arrays — no code changes needed. Call `action_set_context()` to switch on scene init.

### Runtime Remapping

The Controls menu tab (tab 4) lists all 11 game actions. Each action's option list contains all 13 physical buttons. Menu option indices match `PhysicalButton` enum order, so remapping is:

```c
for (int i = 0; i < ACTION_COUNT; i++) {
    int btn_idx = menu_get_value(&start_menu, TAB_CONTROLS, i);
    action_set_binding((GameAction)i, (PhysicalButton)btn_idx);
}
```

Cancel (B button) reverts all bindings to pre-menu-open values via the menu snapshot system.

### API

```c
// Lifecycle
void action_init(void);                          // joypad_init + default context
void action_update(void);                        // joypad_poll + map buttons→actions

// Query (called by game logic instead of raw joypad)
bool  action_pressed(GameAction action);          // Edge-triggered
bool  action_held(GameAction action);             // Continuous
bool  action_released(GameAction action);         // Edge-triggered

// Analog stick
float action_analog_x(void);                      // Filtered by deadzone/sensitivity
float action_analog_y(void);
bool  action_has_analog(void);

// Context/remapping
void action_set_context(const ActionContext *ctx);
void action_set_binding(GameAction action, PhysicalButton button);
PhysicalButton action_get_binding(GameAction action);
```

### Performance

- O(ACTION_COUNT=11) per `action_update()` call — negligible
- Zero heap allocation — all state is static arrays
- No overhead when not remapped — default context matches previous hardcoded behavior

## Collision Detection

See [COLLISION.md](COLLISION.md) for full documentation.

### Summary

- Layer-based collision with bitmask filtering
- Sphere and AABB collider types
- Broadphase AABB culling + narrowphase shape tests
- Raycasting (sphere, AABB, triangle)
- Overlap queries
- Up to 64 colliders, 32 results per frame

## Physics System

See [PHYSICS.md](PHYSICS.md) for full documentation.

### Summary

- Semi-fixed timestep (1/60s) with accumulator pattern — deterministic simulation independent of display frame rate
- Euler integration: gravity → external forces → damping → position
- Ground detection via downward raycasts through the collision system
- Bounce response: velocity decomposition into normal (reflected with restitution) and tangent (damped with friction) components
- Rest detection: bodies stop micro-bouncing when velocity falls below threshold
- Data-driven `PhysicsBodyDef` for material presets (mass, restitution, friction, damping, gravity scale, radius)
- 3 built-in presets: Ball (bouncy), Heavy (low bounce), Floaty (low gravity)
- Scene-local `PhysicsWorld` — opt-in per scene, no changes to Scene struct
- Up to 32 bodies, max 4 steps per frame (spiral-of-death protection)
- Impulse and force application APIs for knockback, jumping, and projectile launch

## Scene System

See [SCENE_SYSTEM.md](SCENE_SYSTEM.md) for full documentation.

### Summary

- Code-defined scenes with callback lifecycle (init/update/draw/post_draw/cleanup)
- Each scene owns Camera, LightConfig, CollisionWorld
- Scene manager with transitions (cut, fade-black, fade-white)
- **Soft reset**: set `scene->reset_requested = true` to trigger cleanup + reinit next frame (reusable for game logic: level restarts, death screens, debug reset)
- Up to 32 objects and 16 textures per scene
- Per-object update/draw callbacks via SceneObject
- Draw order: sky_draw → on_draw (floor/3D) → per-object on_draw → on_post_draw (particles → HUD/overlays)
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
| Code + data | ~337KB | Current ROM size |
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
| `src/render/mesh_defs.c/h` | Shape library: pillar, platform, pyramid, sphere factory functions |
| `src/render/cube.c/h` | Cube geometry (textured, built on Mesh) |
| `src/render/floor.c/h` | Checkered floor grid (dynamic, Z-biased, point light illumination) |
| `src/render/lighting.c/h` | Blinn-Phong lighting, point lights, configurable sun |
| `src/render/texture.c/h` | Texture loading, TMEM management, dynamic slots |
| `src/render/billboard.c/h` | Billboard system: camera-facing textured quads |
| `src/render/shadow.c/h` | Shadow casting (blob + projected planar shadows) |
| `src/render/particle.c/h` | Particle system: pool-based emitters, direct RDP batch renderer |
| `src/render/atmosphere.c/h` | Fog config, sky gradient renderer, 7 atmosphere presets |
| `src/math/vec3.h` | Vector math library (header-only) |
| `src/collision/collision.c/h` | Collision detection, raycasting, overlap queries |
| `src/physics/physics.c/h` | Physics simulation: gravity, impulse, bounce, ground detection |
| `src/scene/scene.c/h` | Scene lifecycle, manager, transitions, per-object callbacks, soft reset |
| `src/scenes/demo_scene.c/h` | Demo scene: mesh objects, billboards, selection, HUD |
| `src/input/action.c/h` | Action mapping: remappable bindings, contexts, pressed/held/released |
| `src/input/input.c/h` | Camera input adapter (reads from action API) |
| `src/ui/text.c/h` | Text rendering |
| `src/ui/menu.c/h` | Tabbed menu system (5 tabs: Settings, Sound, Lighting, Environ, Controls), scrollable, disabled items |
| `src/audio/audio.c/h` | Audio mixer, SFX/BGM playback |
| `src/audio/sound_bank.c/h` | Sound event definitions and path mapping |

## Documentation Index

| Document | Contents |
|----------|----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This file — system overview |
| [RENDERING.md](RENDERING.md) | Rendering pipeline details |
| [TEXTURES.md](TEXTURES.md) | Texture pipeline and TMEM |
| [CAMERA.md](CAMERA.md) | Camera modes, math, frustum, collision |
| [COLLISION.md](COLLISION.md) | Collision detection and raycasting |
| [PHYSICS.md](PHYSICS.md) | Physics engine: gravity, bounce, impulse, timestep |
| [SCENE_SYSTEM.md](SCENE_SYSTEM.md) | Scene/world management |
| [MENU_SYSTEM.md](MENU_SYSTEM.md) | Menu overlay system |
| [INPUT.md](INPUT.md) | Controller input handling |
| [MESH_SYSTEM.md](MESH_SYSTEM.md) | Mesh/model abstraction |
| [ROADMAP.md](ROADMAP.md) | Development roadmap and milestones |
| [SETUP.md](SETUP.md) | Environment setup guide |
| [WORKFLOW.md](WORKFLOW.md) | Development workflow |
