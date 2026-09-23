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
├── ui/text             [font rendering]
├── ui/menu             [global start menu, built here (6 tabs)]
│   └── ui/text
├── debug/*             [debug_menu, profiler, stats, memstats, frametime, overlay,
│                        rdp_debug, testbed: DEBUGGING.md, PROFILING.md]
├── audio/audio         [audio mixer, SFX/BGM playback]
│   └── audio/sound_bank [sound event definitions]
├── render/atmosphere   [fog config, sky gradient, 7 presets]
├── scene/scene         [scene manager, lifecycle, background, transitions]
│   ├── render/camera   [multi-mode camera, 3D math, frustum, collision]
│   ├── render/lighting [Blinn-Phong calculation]
│   ├── render/texture  [texture slots, per-scene loading]
│   ├── render/atmosphere [sky background]
│   └── collision/collision [collision detection, raycasting]
├── scenes/demo_scene   [demo scene: objects, selection, menu semantics, HUD]
│   ├── input/input     [camera input adapter, reads from action API]
│   ├── render/cube     [cube geometry definition (textured)]
│   │   └── render/mesh [mesh_build.c builder, mesh.c mesh_draw()]
│   │       ├── render/camera
│   │       ├── render/lighting
│   │       ├── render/texture
│   │       └── render/atmosphere
│   ├── render/mesh_defs [shape library: pillar, platform, pyramid, sphere]
│   ├── render/floor     [checkered floor, point-lit and fogged per tile]
│   ├── render/billboard [camera-facing textured quads, drawn with render/mesh]
│   ├── render/shadow    [blob + projected shadow casting, uses render/mesh helpers]
│   ├── render/particle  [particle.c simulation, particle_draw.c renderer]
│   ├── physics/physics  [physics bodies, gravity, bounce response]
│   │   └── collision/collision
│   └── audio/audio
└── scenes/benchmark_scene [stress test: meshes, particles, shadows, floor, fill rate]

engine/hot.h, engine/hot_text.ld   [I-cache placement of the render hot path (HARDWARE.md)]
```

### Initialization Order

```c
// System init (main.c)
debug_init_isviewer();      // Debug output (ISViewer)
debug_init_usblog();        // Debug output (USB)
display_init(...);          // Framebuffers
memstats_init(...);         // RDRAM size, stack painting
rdpq_init();                // RDP command queue (validator off; Debug tab)
dfs_init(...);              // ROM filesystem
action_init();              // Joypad + action mapping
text_init();                // Load fonts
menu_init(&start_menu, ...);// Global start menu; tabs and items added here,
debug_menu_init(...);       //   the Debug tab by debug_menu_init()
snd_init();                 // Audio mixer, SFX preload
atmosphere_init();          // Fog/sky global state
surface_alloc(...);         // Z-buffer (shared across scenes)

// Scene manager init
scene_manager_init(&mgr);
testbed_init(&mgr, &start_menu);   // Reset Soak / Menu Sweep
scene_manager_switch(&mgr, demo_scene_get(), TRANSITION_CUT, 0);  // benchmark in a BENCH=1 build
profiler_init();

// Inside scene_init() (called by manager):
collision_world_init();     // Reset collision world
lighting_init(&config);     // Light parameters
texture_load_slot();        // Declared per-scene textures
scene->on_init();           // Scene-specific setup
  camera_init(&cam, &preset); // Camera matrices
  cube_init();                // Model geometry (demo scene)
  collision_add_*();          // Add colliders
  physics_world_init();       // Physics world (optional, scene-local)
```

### Frame Loop (Variable Timestep)

Game logic runs once per rendered frame using the actual elapsed time (`dt`, capped at 0.1 s). Frame rate is selectable via menu (30 or 60 FPS). At 60 FPS there is no limiter; at 30 FPS a busy-wait holds each loop iteration to 1/30 s.

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
    //      -> scene->on_update(dt) [input, menu, game logic]
    //      -> camera_update()
    //      -> collision_test_all()
    debug_menu_update();               // Debug tab, D-Up/D-Down shortcuts
    testbed_update();                  // Reset Soak / Menu Sweep

    // Render
    surface_t *fb = display_get();    // Waits for a free framebuffer
    rdpq_attach(fb, &zbuf);
    scene_manager_draw(&mgr);
    //   -> scene_draw(current)
    //      -> sky_draw() or rdpq_clear(bg_color); rdpq_clear_z(ZBUF_MAX)
    //      -> scene->on_draw() [floor, shadows]
    //      -> per-object on_draw() [meshes, billboards]
    //      -> scene->on_post_draw() [particles, HUD, menu]
    //   -> transition overlay (if transitioning)
    overlay_draw(budget_ms);           // Debug overlay page
    rdpq_detach_show();
    snd_update();                      // Feed the audio mixer

    // Busy-wait frame limiter (for 30 FPS target)
    if (engine_target_fps > 0) { /* spin until target frame time */ }
}
```

With triple buffering, `display_get()` waits for a free framebuffer rather than for vsync, so loop times alternate short and long (about 12.5 / 21 ms) at a steady 60 FPS, and `dt` inherits that jitter (defect D19, [PROFILING.md](PROFILING.md)). The profiler, stats and memory hooks around this loop are described in PROFILING.md.

**Why variable timestep:** A previous fixed-timestep accumulator (30Hz logic) caused every other frame at 60 FPS to be an identical duplicate — the accumulator hadn't reached the 33ms threshold, so no logic update ran. Motion was effectively 30Hz regardless of display rate, making 30 and 60 FPS feel identical. Variable timestep ensures every rendered frame has a unique logic update.

## Rendering Pipeline

See [RENDERING.md](RENDERING.md) for the full pipeline documentation.

### Summary

```
CPU: Model Matrix -> frustum cull -> MVP = VP * Model
     -> per planar face group (or per triangle of a curved group): cull + light
     -> per triangle corner: transform, clip checks, viewport map
RDP: Rasterize -> Texture Sample -> Z-Test -> Framebuffer
```

- Software transforms on CPU, hardware rasterization on RDP
- Hardware 16-bit Z-buffer (replaced painter's algorithm)
- Triangle format chosen per material and fog state: `TRIFMT_ZBUF_TEX` `{X, Y, Z, S, T, INV_W}` for textured, `TRIFMT_ZBUF` for flat, `TRIFMT_ZBUF_SHADE(_TEX)` with fog

## Lighting Model

Flat-shaded Blinn-Phong lighting computed on the CPU (once per planar face group, once per triangle on curved groups) with configurable directional sun, up to 4 point lights, and shadow casting.

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
| `SHADOW_PROJECTED` | the caster's light-facing triangles | Mesh silhouette projected onto floor plane along light direction |

### RDP State

```c
rdpq_set_mode_standard();           // 1-cycle mode (CRITICAL for hardware)
rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
rdpq_mode_zbuf(true, false);        // Z-read ON, Z-write OFF
```

- **Z-read ON**: shadows respect floor depth, don't render through objects
- **Z-write OFF**: shadows don't occlude objects drawn afterward
- Shadow color derived from `ShadowConfig.darkness` (0.0 = invisible, 1.0 = fully black)

### Blob Shadows

`shadow_draw_blob()` draws two triangles under the caster: a square of `blob_radius` scaled by the caster's bounding radius (0.5–2×), skipped when the caster is below the floor or more than 400 units above it.

### Projected Shadow Math

Projecting a point along the light direction onto the floor plane is an affine map, so `shadow_draw_projected()` composes one matrix per caster that takes a local vertex straight to its shadow's clip position:

```c
// kx = lx / ly, kz = lz / ly (light direction l, toward the light)
// S: x' = x - kx * (y - floor_y),  y' = floor_y + 0.01,  z' = z - kz * (y - floor_y)
M = VP * S * model
```

Per caster:

- **Culling:** nothing is drawn when the light is nearly horizontal (`ly < 0.05`) or when the shadow is off screen. The shadow of the caster's bounding sphere (radius r) fits in a sphere of radius r / ly around the projected centre, which is tested against the frustum (`mesh_world_bounds()` + `camera_sphere_visible()`).
- **Light-facing faces only:** for a closed mesh (`backface_cull` on) the faces turned toward the light cover the shadow exactly, so the others are skipped: a planar face group is tested once with its normal against the light direction, a triangle of a curved group by its projected winding. Open or double-sided meshes project every face.
- **Each vertex once:** a vertex is projected the first time a drawn triangle needs it and kept in static scratch (`MESH_MAX_VERTICES` entries) for the rest of the caster; vertices behind the near plane or outside the guard band drop the triangles that use them.

Draw order: background → floor → shadows → objects → particles → HUD → menu

## Particle System

Emitter-based particles with a fixed pool and a direct RDP batch renderer that bypasses `mesh_draw()`. Full documentation (definitions, API, renderer): [PARTICLES.md](PARTICLES.md).

```
ParticleEmitterDef (static const, data-driven; the emitter keeps the pointer)
    ↓ particle_emitter_create()
ParticleEmitter (runtime: position, pool slice, spawn state)
    ↓ particle_emitter_burst() or continuous spawn
Particle pool[128] (global, one contiguous slice per emitter)
    ↓ particle_update(dt)            particle.c: simulation (host-tested)
Gravity, drag, integration, colour/scale interpolation
    ↓ particle_draw(cam)             particle_draw.c: renderer (ENGINE_HOT)
Screen-aligned squares, one RDP mode set for all particles
```

- **Files:** `particle.c` (pool, emitters, `particle_update()`; no rendering, so `tests/host/test_particle.c` covers it), `particle_draw.c` (the renderer, in the hot-text block), `particle_internal.h` (the shared `Particle` / `ParticleEmitter` state; not a public API).
- **Update:** walks each emitter's own slice of the pool with that emitter's definition, so per-emitter constants are computed once and no particle searches for its owner.
- **Renderer:** sets the RDP mode once (standard mode, flat combiner, Z-read without Z-write, additive blend). A camera-facing quad is parallel to the image plane, so each particle's centre is transformed once and drawn as a screen-aligned square (two `TRIFMT_ZBUF` triangles); particles behind the near plane, beyond the far plane, off screen or past the guard band are skipped, and the prim colour is only set when it changes.
- **Limits:** 128 particles, 8 emitters, no heap allocation; destroying emitters returns the unused tail of the pool.
- **Blending:** only additive is implemented; `blend_mode = PARTICLE_BLEND_ALPHA` is ignored (defect D13). Fog dims the colour on the CPU (`1 - fog_factor`), since the RDP fog blender conflicts with additive blending.

## Billboards

Camera-facing textured quads (`src/render/billboard.c`), used by the demo's marker and trees. Full documentation: [BILLBOARDS.md](BILLBOARDS.md).

- A scene object with `on_draw = billboard_draw` and a `BillboardData` (texture slot, spherical or cylindrical mode, width/height, tint) in `data`.
- One shared unit-quad mesh (`billboard_init()` / `billboard_cleanup()`): textured, alpha cutout, no back-face culling. Each draw builds a model matrix that faces the camera (cylindrical mode rotates around Y only), sets the shared material's texture slot and tint, and calls `mesh_draw()`, so frustum culling, lighting, fog and stats apply as for any mesh.

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
  sky_draw()      → gradient fill rectangles (interpolated strips), called by scene_draw()
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

`sky_draw()` renders a smooth vertical gradient using 60 horizontal fill rectangle strips (4px each). Band colors from `SkyConfig` are treated as evenly-spaced gradient stops; each strip's color is linearly interpolated between the two nearest stops. This produces smooth transitions instead of hard-edged flat bands. Fill mode is set once; each strip only changes the fill colour. A one-band sky is a single full-screen rectangle.

The sky is the frame's background: `scene_draw()` calls `sky_draw()` instead of the colour clear whenever `sky_covers_screen()` (sky enabled with at least one band), and clears to `bg_color` otherwise. Scenes do not draw the sky themselves.

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
- **Fog ON (particles)**: one `fog_calculate_factor()` per particle, from the `w` of the centre transform the renderer already does
- **Sky**: 60 fill rectangles per frame, drawn instead of the colour clear — negligible RDP cost

### Menu Integration

ENVIRON tab (tab 3) with 6 items: Preset (8 options), Fog On/Off, Fog Near, Fog Far, Fog Color, Sky On/Off. Named presets auto-enable fog+sky and sync menu toggles. Custom mode allows individual control; with a named preset the five sub-items are disabled (the demo updates the disabled states only when the preset changes).

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

- **Start button**: Always toggles menu — hardcoded, not remappable (system-level; the demo scene reads it from the raw joypad)
- **Menu navigation**: D-pad, A/B, L/R in `menu.c` stay hardcoded (standard UI convention)
- **Analog stick**: Sensitivity/deadzone configurable per context, but not remapped to buttons; `action_analog_x()` is inverted (stick right = negative)
- **InputState preserved**: `input_update()` is a thin adapter reading from the action API — camera code unchanged
- **Polling**: each scene calls `action_update()` at the start of its `on_update`; nothing else polls the joypad ([INPUT.md](INPUT.md))

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

The Controls menu tab (tab 4) lists all 11 game actions. Each action's option list contains all 13 physical buttons. Menu option indices match `PhysicalButton` enum order, so remapping is a cast; the demo applies a binding only when its menu value changes:

```c
for (int i = 0; i < ACTION_COUNT; i++) {
    int btn_idx = menu_get_value(&start_menu, TAB_CONTROLS, i);
    if (btn_idx != last_binding[i]) {
        action_set_binding((GameAction)i, (PhysicalButton)btn_idx);
        last_binding[i] = btn_idx;
    }
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
float action_analog_x(void);                      // Filtered by deadzone/sensitivity; inverted
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
- Up to 64 colliders, 32 results per frame; every scan stops at the highest active slot (`CollisionWorld.high`)

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
- Up to 32 objects and 16 declared textures per scene; declared textures load before `on_init` and are freed after `on_cleanup`
- Per-object update/draw callbacks via SceneObject
- Draw order: background (sky or colour clear, by `scene_draw`) → on_draw (floor, shadows) → per-object on_draw → on_post_draw (particles → HUD → menu)
- Each scene polls input itself; only the demo scene drives the Start menu
- Support for both independent and shared-coordinate scenes

## Camera System

See [CAMERA.md](CAMERA.md) for full documentation.

### Summary

- Three modes: orbital, fixed, follow
- Camera collision via raycasting against environment layer
- Perspective projection with frustum culling
- Matrices rebuilt only when the camera is dirty (its API setters mark it) or in follow mode

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
text_set_style(font_id, style_id, color);   // colour for inline "^xx" style switches
```

Text is expensive on the CPU (about 15–20 µs per glyph on the Analogue 3D with the built-in debug font): the debug overlay prints each page as one multi-line paragraph with `^xx` colour switches and rebuilds it at 4 Hz ([BENCHMARKS.md](BENCHMARKS.md)).

## Audio

A thin `snd_*` wrapper over libdragon's audio and mixer (`src/audio/audio.c`). Full documentation: [AUDIO.md](AUDIO.md).

- 22,050 Hz output, 4 DMA buffers, 16 mixer channels; background music on channel 0 (a looping `wav64`), sound effects round-robin on channels 2–7.
- Sounds are `SoundId` entries in `sound_bank.h` / `sound_bank.c` (path, SFX or BGM, volume 0–128); game code never uses paths. `snd_init()` opens every SFX once at boot.
- `snd_update()` feeds the mixer once per frame from the main loop (`audio` profiler slot). The demo's Sound tab sets the SFX and BGM volumes; Master defaults to Off.

## Memory Budget (4MB)

Measured on the Analogue 3D, debug build ([BENCHMARKS.md](BENCHMARKS.md); the Memory overlay page shows the live values):

| Resource | Size | Notes |
|----------|------|-------|
| Framebuffer x3 | 460,800 B | 320x240 x 2 bytes x 3, heap-allocated by `display_init()` |
| Z-buffer | 153,600 B | 320x240 x 2 bytes, heap-allocated in `main.c` |
| Heap in use after the demo loads | ~0.9 MB | Includes the framebuffers and Z-buffer, meshes, sprites, audio |
| Textures (sprites) | ~2 KB each | 8 x 32x32 RGBA16 in the demo |
| TMEM per frame | 4KB max | RDP on-chip texture cache |
| Stack | 64 KB reserved | Peak use ~3 KB (~4.5 KB with the RDP validator) |

Static code and data come on top (`tools/rom_budget.py` reports text + data + bss; CI fails above 1 MB). The Analogue 3D reports an Expansion Pak (8 MB), but design for 4 MB. Shared resources (framebuffers, Z-buffer) persist across scene transitions. Per-scene textures load/unload with the scene.

## Optimization Notes

### CPU
- Camera matrices rebuilt only when the camera is dirty or following a target
- Frustum culling rejects entire objects before per-face work
- Backface culling skips ~50% of faces on convex objects: once per planar group, per triangle on curved groups
- Static-static collision pairs skipped; collision scans stop at the highest active slot
- Broadphase AABB culling before narrowphase shape tests
- Projected shadows: one composed matrix per caster, each vertex projected at most once, faces turned away from the light skipped
- Particles: one transform per particle, per-emitter constants hoisted out of the update loop
- Render hot path linked contiguously so it does not thrash the direct-mapped I-cache ([HARDWARE.md](HARDWARE.md))

### RDP
- Batch triangles by render state to minimize mode changes
- One texture upload per visible textured face group; consecutive groups of one draw that share a texture reuse the upload (no residency across draws yet)
- Z-buffer eliminates need for CPU-side depth sorting
- The sky replaces the colour clear instead of painting over it
- Transition overlays use 1-cycle mode triangles (hardware-safe)

### Memory
- Use `surface_alloc()` for Z-buffer (allocated once, reused every frame)
- Sprite slots are loaded once at scene init, uploaded to TMEM per-frame as needed
- Per-scene texture load/free prevents accumulation
- Menu/text configs are stack-allocated or static

## Source Files

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, display/input/menu init (builds the 6-tab start menu), variable-timestep game loop |
| `src/render/camera.c/h` | Multi-mode camera, 3D math, frustum culling, collision |
| `src/render/mesh.h` | Mesh types and API, inline helpers (`mesh_world_bounds`, `mesh_normal_matrix`, `mesh_screen_area2`) |
| `src/render/mesh_build.c` | Mesh builder, bounds and face-group analysis (host-tested) |
| `src/render/mesh.c` | `mesh_draw()`, the universal draw function |
| `src/render/mesh_defs.c/h` | Shape library: pillar, platform, pyramid, sphere factory functions |
| `src/render/cube.c/h` | Cube geometry (textured, built on Mesh) |
| `src/render/floor.c/h` | Checkered floor grid (dynamic, Z-biased, point light illumination) |
| `src/render/lighting.c/h` | Blinn-Phong lighting, point lights, configurable sun |
| `src/render/texture.c/h` | Texture slots, per-scene loading, TMEM upload |
| `src/render/billboard.c/h` | Billboard system: camera-facing textured quads |
| `src/render/shadow.c/h` | Shadow casting (blob + projected planar shadows) |
| `src/render/particle.c/h` | Particle pool, emitters and update (host-tested); public API |
| `src/render/particle_draw.c` | Particle renderer (direct RDP batch, hot path) |
| `src/render/particle_internal.h` | Particle state shared by the two particle files |
| `src/render/atmosphere.c/h` | Fog config, sky gradient renderer, 7 atmosphere presets |
| `src/math/vec3.h` | Vector math library (header-only) |
| `src/collision/collision.c/h` | Collision detection, raycasting, overlap queries |
| `src/physics/physics.c/h` | Physics simulation: gravity, impulse, bounce, ground detection |
| `src/scene/scene.c/h` | Scene lifecycle, background, manager, transitions, per-object callbacks, soft reset |
| `src/scenes/demo_scene.c/h` | Demo scene: mesh objects, billboards, selection, menu semantics, HUD |
| `src/scenes/benchmark_scene.c/h` | Benchmark scene: stress steps and BENCH CSV rows |
| `src/input/action.c/h` | Action mapping: remappable bindings, contexts, pressed/held/released |
| `src/input/input.c/h` | Camera input adapter (reads from action API) |
| `src/ui/text.c/h` | Text rendering |
| `src/ui/menu.c/h` | Tabbed menu system, scrollable, disabled items |
| `src/audio/audio.c/h` | Audio mixer, SFX/BGM playback |
| `src/audio/sound_bank.c/h` | Sound event definitions and path mapping |
| `src/debug/*.c/h` | Build switches, Debug tab, profiler, stats, memory, frame time, overlay pages, RDP capture, Reset Soak / Menu Sweep |
| `src/engine/hot.h`, `hot_text.ld` | `ENGINE_HOT` / `ENGINE_NOINIT` and the hot-text link order |

## Documentation Index

| Document | Contents |
|----------|----------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This file — system overview |
| [RENDERING.md](RENDERING.md) | Rendering pipeline details |
| [MESH_SYSTEM.md](MESH_SYSTEM.md) | Mesh/model abstraction |
| [TEXTURES.md](TEXTURES.md) | Texture pipeline and TMEM |
| [PARTICLES.md](PARTICLES.md) | Particle system |
| [BILLBOARDS.md](BILLBOARDS.md) | Billboards |
| [AUDIO.md](AUDIO.md) | Audio mixer and sound bank |
| [CAMERA.md](CAMERA.md) | Camera modes, math, frustum, collision |
| [COLLISION.md](COLLISION.md) | Collision detection and raycasting |
| [PHYSICS.md](PHYSICS.md) | Physics engine: gravity, bounce, impulse, timestep |
| [SCENE_SYSTEM.md](SCENE_SYSTEM.md) | Scene/world management |
| [MENU_SYSTEM.md](MENU_SYSTEM.md) | Menu overlay system |
| [INPUT.md](INPUT.md) | Controller input handling |
| [EXTENDING.md](EXTENDING.md) | How-to recipes and the contribution stage gate |
| [DEBUGGING.md](DEBUGGING.md) | Debug tab, logs, RDP validator and capture, crashes, unit tests |
| [PROFILING.md](PROFILING.md) | Profiler, stats, memory, frame time, RDP load, CSV rows |
| [BENCHMARKS.md](BENCHMARKS.md) | Benchmark scene and measured results |
| [HARDWARE.md](HARDWARE.md) | Analogue 3D + SummerCart64 facts, RDP rules, CPU caches |
| [SETUP.md](SETUP.md) | Environment setup guide |
| [WORKFLOW.md](WORKFLOW.md) | Development workflow |
| [ROADMAP_v2.md](ROADMAP_v2.md) | Current roadmap, defect register |
| [ROADMAP.md](ROADMAP.md) | v1 roadmap (delivery record for Features 1–10) |
