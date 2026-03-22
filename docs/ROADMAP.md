# Development Roadmap

A methodical, incremental plan for building a flexible N64 graphics engine. Each feature builds on the previous one — the goal is to make each layer solid before stacking the next.

This project is **educational**: we build systems from scratch to understand them deeply, document everything along the way, and verify on real hardware. It is also intended to be **powerful**: we adopt proven tools (T3D, Fast64) when they unlock capabilities that would take months to replicate.

Long-term vision: an action RPG engine supporting both souls-like combat and Final Fantasy Tactics-style strategy modes. The engine should be general enough for other genres.

## Current State

**22 stable source modules**, all verified on Ares emulator and Analogue 3D hardware (via SummerCart64). All 5 short-term features complete. V2 Features 6-7 (Particles, Fog & Atmosphere), Feature 9 (Physics), and Feature 10 (Action Mapping) complete.

| System | Status | Notes |
|--------|--------|-------|
| 3D Rendering | Working | CPU software transforms + RDP rasterization, Z-buffered, textured |
| Mesh System | Mature | Generic Mesh type, builder API, universal `mesh_draw()`, frustum culling |
| Shape Library | Working | Factory functions for pillar, platform, pyramid, sphere (mesh_defs) |
| Multi-Object Scene | Working | 8 objects with different meshes, positions, scales, colliders |
| Object Manipulation | Working | Select, move, rotate, scale objects via controller (Z/A/B/analog) |
| Camera | Mature | 3 modes (orbital, fixed, follow), frustum culling, 3-layer collision |
| Collision | Mature | Sphere/AABB, raycasting, layer bitmasks, overlap queries |
| Scene Management | Mature | Lifecycle callbacks (init/update/draw/post_draw/cleanup), transitions, per-object callbacks |
| Lighting | Mature | Configurable sun, point lights (×4), shadow casting (blob + projected) |
| Shadow System | Mature | Blob + projected planar shadows, configurable darkness |
| Billboard System | Mature | Camera-facing quads, spherical + cylindrical modes |
| Audio System | Working | BGM streaming, SFX playback, 16-channel mixer |
| Sound Bank | Working | 9 sound events, DFS path mapping |
| Input | Working | Analog stick, D-pad, C-buttons, shoulder buttons, Start, Z-trigger |
| Fog & Atmosphere | Mature | Hardware fog, CPU fog (floor/particles), sky gradient, 7 presets |
| Menu System | Mature | Tabbed menu (Settings, Sound, Lighting, Environ), scrollable, snapshot/revert |
| Text Rendering | Working | 2 fonts, configurable alignment/color, left/right-aligned HUD |
| Variable Timestep | Working | Logic runs once per frame at display rate (30 or 60 FPS) |
| Texture Management | Working | 16 dynamic slots, per-frame TMEM upload |
| Particle System | Mature | Pool-based emitters, burst/continuous, additive blend, direct RDP batch renderer |
| Physics | Working | Semi-fixed timestep, gravity, bounce, impulse, ground detection, 3 body presets |

### What's Missing

No model loading (geometry is hand-coded C), no animation, no data-driven asset pipeline, no sprite animation.

---

## Short-Term: Next 5 Features

Ordered by dependency. Each feature unlocks the next.

### Feature 1: Mesh/Model Abstraction — COMPLETE

Generic `Mesh` type with builder API, per-group materials, frustum culling, and universal `mesh_draw()` renderer. The cube uses Mesh internally. See [MESH_SYSTEM.md](MESH_SYSTEM.md).

**Delivered:**
- `src/render/mesh.c/h` — Mesh struct, vertex/index buffers, material system, bounding sphere
- `src/render/cube.c` — Refactored to use Mesh (textured, 6 materials, 6 face groups)
- Per-group RDP mode batching, per-group lighting, backface culling
- Verified on hardware at 60 FPS

---

### Feature 2: Multi-Object Rendering — COMPLETE

SceneObject system fully activated with 5 objects (cube, 2 pillars, platform, pyramid), per-object update/draw callbacks, interactive selection and manipulation, and a reorganized HUD.

**Delivered:**
- `src/render/mesh_defs.c/h` — Shape library: pillar (8-sided cylinder, 32 tris), platform (box, 12 tris), pyramid (4-sided + base, 6 tris)
- Generic `ObjectData` struct + `object_update`/`object_draw` callbacks in demo_scene
- Object selection (Z-trigger → D-pad cycle → A to transform) with visual highlight (boosted ambient)
- Object manipulation: move (analog+C), rotate (analog+C), scale (analog+C)
- Per-object sphere colliders updated during manipulation
- `on_post_draw` scene callback for HUD/overlays after all 3D geometry
- HUD: left (OBJ/VIS count, geometry stats, FPS), right (selection, camera mode, XYZ)
- Variable timestep (replaced 30Hz fixed accumulator that caused duplicate frames at 60 FPS)
- ~270 total triangles, stable 60 FPS on hardware

---

### Feature 3: Basic Audio (Music + SFX) — COMPLETE

Audio subsystem integrated with libdragon's mixer. Background music via WAV64 streaming, sound effects for all interactive events. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/audio/audio.c/h` — Mixer init (22050 Hz, 4 DMA buffers), SFX/BGM playback, volume control
- `src/audio/sound_bank.c/h` — 9 sound events (menu open/close/nav/select, object select/deselect/mode_change, collision, BGM)
- `snd_*` namespace (avoids collision with libdragon's `audio_init()`), 16 mixer channels, round-robin SFX allocation (channels 2-7), BGM on channel 0
- Menu Sound tab with master/SFX/BGM volume controls
- Verified on hardware at 60 FPS

---

### Feature 4: Billboard System — COMPLETE

Camera-facing textured quads for world-space sprites (markers, trees, effects). Built on the existing `mesh_draw()` pipeline for consistent rendering. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/render/billboard.c/h` — Camera-facing textured quads via `mesh_draw()` pipeline
- Two modes: spherical (fully faces camera) + cylindrical (Y-axis only rotation)
- Shared unit quad mesh (2 triangles) built once at init, material swapped per draw
- Alpha cutout support, 32×32 RGBA16 sprites (2KB, fits 4KB TMEM)
- Edge case handling: camera directly above → fallback vectors
- Billboard objects (marker, tree) in demo scene, non-selectable
- Verified on hardware at 60 FPS

---

### Feature 5: Advanced Lighting & Shadows — COMPLETE

Configurable directional sun, point lights with attenuation, and shadow casting (blob + projected). Real-time tuning via Lighting menu tab. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/render/lighting.h/c` — Enhanced `LightConfig`: configurable sun (direction, color, intensity), ambient RGB, specular (intensity + shininess via `fast_pow_int` binary exponentiation)
- `PointLight` struct: position, color, intensity, radius, active — up to 4 per scene, smooth quadratic falloff `(1-(d/r)²)²`
- `src/render/shadow.h/c` — `ShadowCaster` struct, blob shadows (dark quad at floor), projected shadows (mesh silhouette projected along light direction onto floor plane)
- Shadow RDP state: 1-cycle mode (hardware-safe), Z-read ON, Z-write OFF (shadows don't occlude objects)
- Lighting menu tab: 7 parameters (Sun Dir, Sun Color, Brightness, Ambient, Shadows, Shadow Dark, Pt Lights)
- 2 demo point lights positioned near pillars, ~58 extra triangles worst case
- Verified on hardware at 60 FPS

---

## V2 Roadmap: Engine Framework Features

With the original 5 features complete, these features extend the engine framework before transitioning to the T3D/GLTF model pipeline. Each feature remains hardware-verifiable and builds on existing systems. Ordered by dependency and visual impact.

### Feature 6: Particle System — COMPLETE

Emitter-based particle system with pool-based allocation, data-driven effect definitions, and a high-performance direct RDP batch renderer. Two demo effects (fire/sparks and magic/energy) burst from pillar tops via B button. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/render/particle.c/h` — Particle pool (128 max), emitter management (8 max), physics update (gravity, drag, integration), color/scale interpolation
- `ParticleEmitterDef` struct: data-driven effect definitions (burst count, spawn rate, lifetime range, velocity range, gravity, drag, color start/end, scale start/end, spawn shape, blend mode)
- Direct RDP batch renderer: bypasses `mesh_draw()` overhead, sets `rdpq_set_mode_standard()` only ONCE for all particles (vs per-call in mesh pipeline)
- Camera-facing billboard quads: 4 corners computed from camera right/up vectors, transformed through VP matrix, guard band + near-plane + depth clamping safety
- Z-read ON, Z-write OFF: particles clip behind scene geometry, don't occlude each other or subsequent objects
- Additive blending (`RDPQ_BLENDER_ADDITIVE`): overlapping particles produce glow effects
- `TRIFMT_ZBUF` (3 floats: X, Y, Z): no texture coordinates needed for flat-colored particles — zero TMEM cost
- Per-particle frustum culling via `camera_sphere_visible()`
- Color change batching: `rdpq_set_prim_color()` only called when color differs from previous particle
- Two demo effects: fire/sparks (left pillar, warm yellow→red, shrinking, arcing down) and magic/energy (right pillar, blue→purple, expanding, floating up)
- Burst trigger: B button in normal mode fires both emitters with SFX
- Performance: ~192 triangles worst case (96 particles × 2 tris), triangle count peaks at ~455 during burst and settles to ~345 steady state
- Verified on hardware (Analogue 3D) at 60 FPS

---

### Feature 7: Fog & Atmosphere — COMPLETE

Distance-based fog and configurable sky gradients for atmospheric depth cues and mood setting. Hybrid approach: hardware RDP fog for meshes, CPU-side fog for floor tiles and additive particles. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/render/atmosphere.c/h` — FogConfig, SkyConfig, 7 presets (Clear Day, Overcast, Foggy, Dense Fog, Sunset, Dusk, Night), utility functions
- Hardware fog in `mesh_draw()`: `RDPQ_FOG_STANDARD` via shade alpha, auto-switches vertex format (`TRIFMT_ZBUF_SHADE`/`TRIFMT_ZBUF_SHADE_TEX`) and combiner (`RDPQ_COMBINER_SHADE`/`RDPQ_COMBINER_TEX_SHADE`)
- CPU fog in `floor_draw()`: per-tile color blend via `fog_blend_color()` (floor shares vertices between tiles — can't use shade formats)
- CPU fog in `particle_draw()`: RGBA dimming (multiply by `1 - fog_factor`) — compatible with `RDPQ_BLENDER_ADDITIVE`
- Smooth sky gradient: 60 interpolated fill rectangle strips (4px each) between gradient stops, replacing hard-edged flat bands
- Seamless blending: bottom sky band = fog color = bg_color in every preset
- ENVIRON menu tab: Preset selector (8 options), Fog On/Off, Fog Near/Far, Fog Color, Sky On/Off
- Menu system upgraded: `MENU_MAX_TABS=6`, `MENU_MAX_ITEMS=12`, `MENU_VISIBLE_ITEMS=7`, scrolling with indicators
- Zero overhead when fog disabled — identical code paths as before
- Verified on hardware (Analogue 3D) at 60 FPS

---

### Feature 8: Sprite Animation

**Problem:** Billboards are static single frames. No way to animate effects (fire, water, explosions) or show NPC idle cycles. Particle effects need animated sprites.

**Solution:** Frame-based animation system for billboards and 2D elements. Animation definitions (texture atlas or per-frame sprites), playback controller (play, pause, loop, one-shot), frame timing via dt accumulator.

```
src/render/anim_sprite.c/h  — AnimDef, AnimState, billboard animation extension
```

**What it unlocks:** Animated effects (fire, water, explosions), NPC idle animations, UI flourishes, richer particle effects with animated frames.

---

### Feature 9: Basic Physics — COMPLETE

Lightweight physics simulation layer on top of the existing collision system. Semi-fixed timestep for deterministic behavior, Euler integration, ground detection via collision raycasts, and bounce/impulse response. See [PHYSICS.md](PHYSICS.md).

**Delivered:**
- `src/physics/physics.c/h` — PhysicsWorld, PhysicsBody, PhysicsBodyDef, semi-fixed timestep accumulator (1/60s, max 4 steps)
- Euler integration: gravity → external forces → air damping → position integration
- Ground detection via downward raycast through collision system (`COLLISION_LAYER_ENV`)
- Bounce response: velocity decomposition into normal (reflected with restitution) and tangent (damped with friction)
- Rest detection: stops micro-bouncing when velocity and impact speed fall below thresholds
- Penetration resolution: pushes body above ground surface when below
- 3 built-in body presets: Ball (bouncy, e=0.7), Heavy (low bounce, e=0.2), Floaty (low gravity, e=0.9)
- Force and impulse APIs for knockback, jumping, and projectile launch
- `mesh_defs_get_sphere()` — 6×6 UV sphere mesh with 6 latitude-band groups for per-group lighting
- Demo: B button spawns red physics ball above platform, bounces under gravity, re-launch with upward impulse
- Platform AABB collider for flat ground detection (separate from existing sphere collider)
- `src/math/vec3.h` — 6 constants (VEC3_ZERO, VEC3_UP, etc.) and 8 utility functions (reflect, project, reject, mul, clamp_length, move_toward, abs, sign)
- Verified in Ares emulator at 60 FPS

---

### Feature 10: Input Action Mapping — COMPLETE

Data-driven input abstraction that decouples game logic from physical button assignments. Remappable game actions, per-context bindings, and runtime rebinding via in-game Controls tab. See [ARCHITECTURE.md](ARCHITECTURE.md).

**Delivered:**
- `src/input/action.c/h` — ActionContext, PhysicalButton (13 buttons), GameAction (11 actions), pressed/held/released queries
- Per-context analog deadzone and sensitivity configuration
- Runtime remapping via Controls menu tab (option indices = PhysicalButton enum, item indices = GameAction enum)
- Cancel reverts all bindings to pre-menu-open values via snapshot system
- `action_init()` replaces `input_init()` — calls `joypad_init()` internally
- Fixed inputs: Start (menu toggle), D-pad/A/B/L/R in menus stay as raw joypad reads
- Tab rendering: single active tab with `< [Name] N/M >` indicator (scales to any tab count)
- Verified on hardware (Analogue 3D) at 60 FPS

---

## Milestone 1: Asset Pipeline & T3D Rendering Upgrade

**The transition from "educational prototype" to "content-ready engine."** Required for both FFT-style and souls-like game modes — the GLTF pipeline provides characters, weapons, and environments for either genre.

### Why T3D

The current CPU-based software transform pipeline (mat4_mul_vec3 per vertex, per face, per object) hits its limit at roughly 20-30 objects. Beyond that, the CPU can't finish transforms before the next frame.

[Tiny3D (T3D)](https://github.com/HailToDodongo/tiny3d) moves vertex transformation and lighting to the RSP, which is purpose-built for this work:
- **RSP T&L:** Transform & lighting computed on the vector processor, freeing the CPU for game logic
- **GLTF model loading:** Import standard 3D models instead of hand-coding geometry
- **Skeletal animation:** Binary skinning with compressed animation streaming from ROM
- **Vertex cache:** 70-vertex cache with optimized DMA — designed around N64's memory architecture
- **Fast64 Blender plugin:** Professional 3D authoring → N64-optimized export → ROM

### What This Milestone Delivers

| Capability | Before | After |
|-----------|--------|-------|
| Adding a new object | Write 180 lines of C | Export from Blender, load at runtime |
| Objects per scene | ~20-30 (CPU limit) | 100+ (RSP accelerated) |
| Lighting | CPU per-face | RSP per-vertex |
| Asset workflow | Hand-code vertices | Blender → Fast64 → GLTF → T3D |

### Architecture: Parallel Path (not Dual-Path Dispatch)

T3D models have a fundamentally different data hierarchy (chunk-based objects, materials, skeleton) and rendering flow (viewport-based, matrix stack) than the existing Mesh system. Rather than cramming T3D into `mesh_draw()`, the engine uses a **parallel path**:

- T3D objects get their own `T3DObjectData` struct and `t3d_object_draw` callback, using `SceneObject.on_draw` polymorphism
- The CPU pipeline (`mesh_draw()`, `mesh_defs`, floor, particles, billboards) stays untouched — zero regression risk
- Bridge functions sync lighting (`LightConfig` → T3D lights) and fog (`FogConfig` → T3D fog) across both paths

**CPU-rendered permanently:** Floor grid, particles, billboards, shadows, debug visualization, HUD.

### Blender → N64 Workflow

```
Blender (modeling + Fast64 F3D materials)
    ↓ Fast64 plugin (N64-optimized GLTF export)
GLTF 2.0 file (.glb, standard interchange format)
    ↓ T3D converter (build-time, in Makefile)
T3D model data (.t3dm, ROM asset)
    ↓ t3d_model_load() (runtime, from DFS)
Rendered on screen via RSP at 60 FPS
```

---

### Feature 11: T3D Bootstrap — RSP Rendering Proof of Life

**Problem:** No RSP-accelerated rendering. All vertex transforms and lighting run on CPU, limiting scene complexity to ~20-30 objects.

**Solution:** Link T3D as a git submodule, initialize RSP microcode, render a single hardcoded quad via T3D alongside the existing CPU-rendered scene. Prove both paths coexist in one frame.

```
src/render/t3d_render.c/h  — T3D init, viewport, frame sync, lighting bridge
```

**What it unlocks:** Model loading (Feature 12) — T3D runtime, viewport, and frame synchronization are in place.

---

### Feature 12: GLTF Model Loading — First Blender Model on Screen

**Problem:** All geometry is hand-coded in C (~180 lines per shape). No way to import from standard 3D tools like Blender.

**Solution:** Export a simple model from Blender via Fast64, convert to `.t3dm` at build time, load from ROM at runtime, render via T3D. Prove the end-to-end asset pipeline. Document the Fast64 setup workflow.

```
src/render/t3d_model.c/h  — T3DModelWrapper, model load/draw/free API
assets/models/test_crate.glb  — Simple crate from Blender + Fast64
docs/BLENDER_SETUP.md  — Fast64 installation, F3D materials, export checklist
```

**What it unlocks:** Textured materials and fog (Feature 13) — base model loading pipeline is working.

---

### Feature 13: Textured Materials & Fog Bridge

**Problem:** Feature 12 models are flat-colored only. Fog does not apply to T3D-rendered objects (they ignore the atmosphere system).

**Solution:** T3D models with Fast64-authored textures rendered correctly. Fog bridge syncs `FogConfig` to T3D's native fog API (`t3d_fog_set_range()`, `t3d_fog_set_enabled()`). Document F3D material setup for N64 texture constraints.

```
assets/models/textured_crate.glb  — Crate with 32×32 RGBA16 texture
docs/FAST64_MATERIALS.md  — Material setup guide, combiner modes, TMEM constraints
```

**What it unlocks:** Scene composition (Feature 14) — materials and fog are handled across both paths.

---

### Feature 14: Dual-Path Scene — Replace Shapes with Blender Exports

**Problem:** Five shape factories in `mesh_defs.c` (320 lines of hand-coded geometry). Adding new objects still means writing C code.

**Solution:** Recreate pillar, platform, and pyramid as Blender models rendered via T3D. Auto-generate collision from model AABB bounds. Create a new `t3d_demo_scene` demonstrating the full Blender workflow alongside CPU-rendered floor, particles, billboards, and HUD.

```
assets/models/pillar.glb, platform.glb, pyramid.glb  — Blender shape equivalents
src/scenes/t3d_demo_scene.c/h  — New scene: T3D models + CPU floor/particles/billboards
docs/ASSET_PIPELINE.md  — End-to-end pipeline documentation
```

**What it unlocks:** Developer tooling (Feature 15) — the full model pipeline is proven.

---

### Feature 15: Developer Tooling — Model Viewer & Debug Overlay

**Problem:** No way to inspect T3D models at runtime. No visibility into RSP performance. No quick test workflow for Blender exports.

**Solution:** Standalone model viewer scene (orbital camera, lighting presets, metrics). T3D debug overlay showing RSP triangle counts. Blender template project with Fast64 pre-configured.

```
src/scenes/model_viewer_scene.c/h  — Orbital camera, metrics, model display
assets/blender/template_project.blend  — Pre-configured Fast64, F3D material template
docs/T3D_INTEGRATION.md  — Complete T3D architecture, API reference, troubleshooting
```

**What it unlocks:** Pipeline polish (Feature 16) — developers have tools to iterate on content.

---

### Feature 16: Pipeline Polish — LOD Workflow & Collision from Models

**Problem:** Complex Blender models may exceed N64 triangle budgets. No automatic collision geometry from models.

**Solution:** LOD workflow documentation (Blender decimation, triangle budget targets per object type). Collision mesh extraction from T3D model bounds (AABB → sphere/box). Architecture documentation cleanup.

```
src/render/t3d_collision.c/h  — Extract collision shapes from T3D model bounds
docs/LOD_WORKFLOW.md  — LOD strategy, decimation guide, triangle budgets
```

**N64 triangle budgets (design-time LOD, not runtime):**

| Category | Target Triangles |
|----------|-----------------|
| Environment prop (crate, barrel) | 20–50 |
| Architectural (pillar, wall) | 30–80 |
| Character (humanoid) | 150–300 |
| Boss / hero character | 300–500 |
| Vehicle / large prop | 100–200 |

**What it unlocks:** Milestone 2 (Animation & Character System) — the asset pipeline is complete.

---

## Milestone 2: Animation & Character System

**The transition from "objects exist" to "objects live."** With Milestone 1 delivering the static model pipeline, Milestone 2 adds skeletal animation, character controllers, and the entity system needed for gameplay.

### Feature 17: Skeletal Animation — First Animated Character

A rigged character model with idle and walk animations loaded from Blender, playing at runtime via T3D's skeleton system. An `AnimController` wrapper manages playback.

```
src/render/t3d_anim.c/h  — AnimController, T3D skeleton API wrapper
assets/models/character.glb  — Low-poly humanoid (~200 tris, 10-15 bones, idle + walk)
docs/ANIMATION.md  — Rigging guide for N64, bone limits, authoring tips
```

### Feature 18: Animation Blending & State Queries

Crossfade between animation clips (idle → walk over 0.2s). Speed control, state queries (is_playing, current_time, duration). Double-buffered skeletons to prevent RSP DMA tearing.

### Feature 19: Character Controller

Movement driven by analog stick input. Character faces movement direction (lerped). Walk animation plays during movement, idle during rest. Physics integration for gravity/grounding.

### Feature 20: Animation State Machine

Named states (idle, walk, attack, hit, death, dodge) with data-driven transition rules. Trigger SFX or game logic at specific keyframes ("deal damage" at frame 12 of attack). Validates the animation and input systems end-to-end.

### Feature 21: Entity/Actor Pattern

Evolve `SceneObject` into a composable entity system:

```
Entity
├── Transform      (position, rotation, scale)
├── Renderable     (T3D model or CPU mesh, material)
├── Collidable     (collider shape, layer, callbacks)
├── Animated       (animation state machine, current clip)
├── Controller     (input-driven or AI-driven movement)
├── Combat         (hitboxes, hurtboxes, i-frames, stamina)
└── GameData       (HP, stats, inventory — game-specific)
```

This is NOT a full ECS — it's a tagged-struct pattern where entities have optional capability pointers. The test target: 1 player character + 1 enemy with a basic attack/dodge loop.

### Grid-Based Movement (FFT Track)

For the strategy game vision (parallel development track):
- **Tile grid overlay** — World-space grid with configurable tile size
- **Grid snapping** — Entities snap to tile centers
- **Pathfinding** — A* on the tile grid (N64 CPU can handle grids up to ~32x32 easily)
- **Movement animation** — Entity lerps between tile positions with walk animation

---

## Milestone 3: Game Framework

**The transition from "engine" to "game."**

First playable target is a souls-like combat test scene (1v1 arena), validating animation, physics, and input systems end-to-end. The FFT battle system is the second track, sharing the same foundation.

### Game State Machine

```
Title Screen → World Map → Battle Setup → Battle → Victory/Defeat → Save
     ↑                                                        |
     └────────────────────────────────────────────────────────┘
```

Each state is a scene (or scene configuration) with its own update/draw logic. The existing scene transition system handles the visual transitions; this adds the logical flow.

### Combat Test Scene (Souls-Like)

- **1v1 arena** — Player character vs single enemy in enclosed space
- **Basic attack loop** — Light attack, heavy attack with windup/recovery
- **Dodge mechanic** — Roll with i-frames, stamina cost
- **Hit reactions** — Knockback, stagger, particle effects on impact
- **Health/stamina UI** — HUD bars using existing text/menu system

### Turn-Based Battle System (FFT Track)

- **Initiative/turn order** — Speed-based queue determining who acts when
- **Action system** — Move, Attack, Ability, Item, Wait
- **Targeting** — Range calculation on tile grid, valid target highlighting
- **Damage model** — Stats, elemental types, terrain bonuses

### Save/Load

- **SRAM save** (battery-backed cartridge RAM, 32KB) or **Controller Pak** (memory card, 32KB)
- Serialize game state: current scene, entity positions, inventory, progress flags
- libdragon provides `eepromfs` and `flashcart` APIs

### AI Foundation

- **Decision trees** — Simple priority-based AI for enemy behavior
- **A* pathfinding** — Built for grid movement (FFT) and arena navigation (souls-like)
- **Threat assessment** — Target selection based on distance, HP, advantage

---

## Guiding Principles

1. **Make each layer boring before building the next.** If adding a mesh is still buggy, don't start on animation. If animation is glitchy, don't build a turn system.

2. **Verify on real hardware early and often.** Ares is lenient. The Analogue 3D via SummerCart64 is the source of truth.

3. **Document as you build.** Every system gets a `docs/*.md` file with architecture, API reference, and the "why" behind design decisions. Future-you (and anyone reading this project) will thank present-you.

4. **Respect the hardware.** 4MB RAM, 4KB TMEM, 16-bit Z-buffer. Every feature should be designed around these constraints, not in spite of them. The N64's limitations are what make it interesting.

5. **Prefer libdragon's built-in systems.** Audio mixer, sprite loading, text rendering, RDPQ — use what exists. Build custom only when libdragon doesn't provide it or when the educational value justifies it.

---

## Reference: N64 Hardware Budget

| Resource | Total | Currently Used | Available |
|----------|-------|---------------|-----------|
| RAM | 4 MB | ~900 KB (framebuffers + Z + code + textures) | ~3.1 MB |
| TMEM | 4 KB | Per-frame uploads (cube textures) | Shared |
| CPU per frame | ~16 ms (60Hz) | ~3-5 ms (current scene) | ~11-13 ms |
| RSP per frame | ~16 ms | Audio mixing only (future) | Available for T&L |
| ROM | 64 MB max | ~337 KB (code + assets) | ~63.7 MB |

---

## Reference: Key External Tools

| Tool | Purpose | When Needed |
|------|---------|-------------|
| [Tiny3D (T3D)](https://github.com/HailToDodongo/tiny3d) | RSP-accelerated 3D rendering, GLTF loading, animation | Milestone 1 |
| [Fast64](https://github.com/Fast-64/fast64) | Blender plugin for N64-optimized model/material export | Milestone 1 |
| [Blender](https://www.blender.org/) | 3D modeling, rigging, animation authoring | Milestone 1 |
| [OpenTracker / MilkyTracker](https://milkytracker.org/) | XM music composition | Feature 3 (Audio) |
| [Audacity](https://www.audacityteam.org/) | WAV sound effect editing | Feature 3 (Audio) |

---

## Documentation Index

Each feature and milestone produces corresponding documentation:

| Feature/Milestone | Expected Docs |
|-------------------|---------------|
| Mesh Abstraction | `docs/MESH_SYSTEM.md` |
| Audio | `docs/AUDIO.md` |
| Billboards | `docs/ARCHITECTURE.md` (Billboard section) |
| Lighting & Shadows | `docs/ARCHITECTURE.md` (Lighting & Shadow sections) |
| Particle System | `docs/PARTICLES.md` |
| Physics | `docs/PHYSICS.md` |
| T3D Integration | `docs/T3D_INTEGRATION.md`, `docs/BLENDER_SETUP.md`, `docs/FAST64_MATERIALS.md` |
| Asset Pipeline | `docs/ASSET_PIPELINE.md`, `docs/LOD_WORKFLOW.md` |
| Animation | `docs/ANIMATION.md` |
| Entity System | `docs/ENTITY_SYSTEM.md` |
| Game Framework | `docs/GAME_FRAMEWORK.md` |
