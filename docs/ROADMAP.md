# Development Roadmap

A methodical, incremental plan for building a flexible N64 graphics engine. Each feature builds on the previous one — the goal is to make each layer solid before stacking the next.

This project is **educational**: we build systems from scratch to understand them deeply, document everything along the way, and verify on real hardware. It is also intended to be **powerful**: we adopt proven tools (T3D, Fast64) when they unlock capabilities that would take months to replicate.

Long-term vision: a Final Fantasy Tactics-style strategy game. But the engine should be general enough for other genres.

## Current State

**12 stable source modules**, all verified on Ares emulator and Analogue 3D hardware (via SummerCart64).

| System | Status | Notes |
|--------|--------|-------|
| 3D Rendering | Working | CPU software transforms + RDP rasterization, Z-buffered, textured |
| Camera | Mature | 3 modes (orbital, fixed, follow), frustum culling, 3-layer collision |
| Collision | Mature | Sphere/AABB, raycasting, layer bitmasks, overlap queries |
| Scene Management | Working | Lifecycle callbacks, transitions (cut, fade), per-scene ownership |
| Lighting | Working | Single directional Blinn-Phong (ambient + diffuse + specular) |
| Input | Working | Analog stick, D-pad, C-buttons, shoulder buttons, Start |
| Menu System | Working | 5-item overlay with navigation, apply/cancel |
| Text Rendering | Working | 2 fonts, configurable alignment/color |
| Fixed Timestep | Working | 30Hz logic decoupled from render rate (30/60/unlimited) |
| Texture Management | Working | 16 dynamic slots, per-frame TMEM upload |

### What's Missing

No model loading (geometry is hand-coded C), no animation, no audio, no sprites/2D rendering, no dynamic physics, no data-driven asset pipeline. The cube and floor are the only renderable objects.

---

## Short-Term: Next 5 Features

Ordered by dependency. Each feature unlocks the next.

### Feature 1: Mesh/Model Abstraction

**Problem:** Adding a new 3D object means writing another 180-line file like `cube.c` with hardcoded vertex positions, face normals, UV coordinates, and a custom draw function. This doesn't scale.

**Solution:** Extract a general `Mesh` type that holds vertex/index buffers, material references, and a bounding volume. The cube becomes one instance of Mesh. Any new object (pillar, crate, platform) uses the same type and rendering path.

```
src/render/mesh.h    — Mesh struct, vertex format, API
src/render/mesh.c    — Mesh creation, rendering, bounding volume computation
src/render/cube.c    — Refactored to use Mesh internally
```

**What it unlocks:** Every subsequent feature (multi-object, model loading, instancing) depends on having a common mesh representation. This is the single highest-leverage change in the entire roadmap.

**N64 constraints to consider:**
- Vertex data lives in RDRAM — keep vertex counts reasonable (N64 games typically use 50-500 triangles per object)
- Materials map to RDP mode + combiner + texture — a material struct should capture these
- Bounding volume (sphere or AABB) needed for frustum culling

---

### Feature 2: Multi-Object Rendering

**Problem:** The `SceneObject` system exists in `scene.h` (up to 32 objects per scene with per-object callbacks) but is completely unused — the demo scene draws the cube and floor directly in its `on_draw` callback.

**Solution:** Actually use the object system. Spawn 3-5 objects with different meshes, positions, and scales. Validate that frustum culling, collision, and rendering scale correctly.

```
Example scene: a cube at origin, two pillars flanking it, a platform behind.
Each is a SceneObject with a Mesh, transform, and optional collider.
```

**What it unlocks:** Confidence that the scene architecture works with real content. Exposes performance budgets — how many CPU-transformed objects before we need RSP acceleration? (The answer will motivate Milestone 1.)

**Verification:** Object count on HUD, frustum culling correctly hides off-screen objects, collision works between all objects.

---

### Feature 3: Basic Audio (Music + SFX)

**Problem:** A silent engine feels like a tech demo, not a game. Audio is also a significant subsystem that needs to coexist with rendering in the N64's memory and CPU budget.

**Solution:** Integrate libdragon's audio mixer. Background music via XM format (tracker music — compact, fits N64 RAM easily). Sound effects via WAV64 for discrete events.

```
src/audio/audio.h    — Audio init, music playback, SFX triggers
src/audio/audio.c    — Wrapper around libdragon mixer + xm64player + wav64
```

**libdragon provides:**
- `mixer_init()` — RSP-based audio mixing
- `xm64player_t` — XM module playback (music)
- `wav64_t` — WAV sample playback (SFX)
- Audio runs on RSP alongside graphics — libdragon handles the scheduling

**What it unlocks:** Scene atmosphere (background music per scene), interaction feedback (collision SFX, menu sounds), and validates that audio and rendering can share RSP time without frame drops.

**N64 constraints:**
- Audio buffers use ~64KB RAM (already budgeted in ARCHITECTURE.md)
- XM files are typically 10-50KB — very RAM-friendly
- RSP time-slicing between audio and graphics is handled by libdragon

---

### Feature 4: 2D Sprite & Billboard System

**Problem:** The engine can only draw 3D triangles and text. No way to render HUD elements, selection indicators, particle placeholders, health bars, or item icons.

**Solution:** Two rendering modes:
1. **Screen-space sprites** — fixed pixel position on screen (HUD, icons, UI panels)
2. **World-space billboards** — 3D position but always face the camera (markers, effects, labels)

```
src/render/sprite2d.h  — Sprite types, billboard API
src/render/sprite2d.c  — Screen-space and billboard rendering
```

**What it unlocks:** HUD system for any game genre. Selection indicators for the FFT grid. Foundation for a future particle system (particles are just billboards with a lifetime). World-space labels for debug visualization.

**Implementation detail:** Billboards use the existing camera VP matrix to compute screen position, then draw a textured quad. Screen-space sprites skip the 3D transform entirely — just blit at pixel coordinates.

---

### Feature 5: Enhanced Lighting

**Problem:** Single global directional light with per-face computation. Every object has the same flat lighting regardless of position. No local light sources (torches, spell effects, glowing items).

**Solution:** Multiple light sources per scene. Add point lights with distance attenuation. Move to per-vertex lighting (interpolated across faces) for meshes with enough vertex density.

```
LightConfig changes:
- Array of lights (max 4 per scene — N64 CPU budget)
- Light types: DIRECTIONAL (existing), POINT (new)
- Per-vertex lighting option on Mesh
- Attenuation: 1 / (constant + linear*d + quadratic*d^2)
```

**What it unlocks:** Environmental mood (dark dungeon with torch points, bright outdoor with sun). Spell and item effects that cast light. Visual depth that makes multi-object scenes readable.

**N64 constraints:**
- Per-vertex lighting is CPU-intensive — limit to 4 active lights
- Point light attenuation is a per-vertex distance calculation
- Can skip lights outside an object's bounding sphere (light culling)
- When we migrate to T3D (Milestone 1), RSP handles lighting — this CPU implementation still teaches the math

---

## Milestone 1: Asset Pipeline & T3D Rendering Upgrade

**The transition from "educational prototype" to "content-ready engine."**

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
| Animation | None | Skeletal with skinning |
| Asset workflow | Hand-code vertices | Blender → Fast64 → GLTF → T3D |

### Sub-Features

1. **T3D integration** — Add T3D as a dependency, initialize RSP microcode alongside RDPQ
2. **Model loader** — Load GLTF models exported from Blender via Fast64
3. **Material system** — Map Fast64 materials to RDP combiner modes and textures
4. **Mesh system migration** — The Mesh type from Feature 1 gains a T3D backend (CPU fallback stays for educational reference)
5. **Animation playback** — Load and play skeletal animations from GLTF

### Blender → N64 Workflow

```
Blender (modeling, rigging, animation)
    ↓ Fast64 plugin (N64-optimized export)
GLTF 2.0 file (standard interchange format)
    ↓ T3D importer (build-time conversion)
T3D model data (ROM asset)
    ↓ t3d_model_load() (runtime)
Rendered on screen via RSP
```

---

## Milestone 2: Animation & Entity System

**The transition from "objects exist" to "objects live."**

### Skeletal Animation

With T3D providing the runtime, this milestone focuses on the authoring workflow and game integration:
- **Animation state machine** — States (idle, walk, attack, hit, death) with transition rules
- **Blend transitions** — Smooth crossfade between animation clips
- **Animation events** — Trigger SFX or game logic at specific keyframes (e.g., "deal damage" at frame 12 of attack animation)

### Entity/Actor Pattern

Evolve `SceneObject` into a proper entity system with composable behaviors:

```
Entity
├── Transform      (position, rotation, scale)
├── Renderable     (mesh/model reference, material)
├── Collidable     (collider shape, layer, callbacks)
├── Animated       (animation state machine, current clip)
├── Controller     (input-driven or AI-driven movement)
└── GameData       (HP, stats, inventory — game-specific)
```

This is NOT a full ECS (entity-component-system) — that's over-engineering for N64. It's a tagged-struct pattern where entities have optional capability pointers.

### Grid-Based Movement (FFT Prep)

For the strategy game vision:
- **Tile grid overlay** — World-space grid with configurable tile size
- **Grid snapping** — Entities snap to tile centers
- **Pathfinding** — A* on the tile grid (N64 CPU can handle grids up to ~32x32 easily)
- **Movement animation** — Entity lerps between tile positions with walk animation

---

## Milestone 3: Game Framework

**The transition from "engine" to "game."**

### Game State Machine

```
Title Screen → World Map → Battle Setup → Battle → Victory/Defeat → Save
     ↑                                                        |
     └────────────────────────────────────────────────────────┘
```

Each state is a scene (or scene configuration) with its own update/draw logic. The existing scene transition system handles the visual transitions; this adds the logical flow.

### Turn-Based Battle System (FFT-Specific)

- **Initiative/turn order** — Speed-based queue determining who acts when
- **Action system** — Move, Attack, Ability, Item, Wait
- **Targeting** — Range calculation on tile grid, valid target highlighting
- **Damage model** — Stats, elemental types, terrain bonuses

### Save/Load

- **SRAM save** (battery-backed cartridge RAM, 32KB) or **Controller Pak** (memory card, 32KB)
- Serialize game state: current scene, entity positions, inventory, progress flags
- libdragon provides `eepromfs` and `flashcart` APIs

### AI Foundation

- **Decision trees** — Simple priority-based AI for enemy turns
- **A* pathfinding** — Already built for grid movement (Milestone 2)
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
| ROM | 64 MB max | ~280 KB (code + assets) | ~63.7 MB |

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
| Sprites & Billboards | `docs/SPRITES.md` |
| T3D Integration | `docs/T3D_INTEGRATION.md`, updated `RENDERING.md` |
| Entity System | `docs/ENTITY_SYSTEM.md` |
| Game Framework | `docs/GAME_FRAMEWORK.md` |
