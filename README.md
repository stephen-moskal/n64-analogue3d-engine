# N64 Dev Engine

A Nintendo 64 homebrew game engine built with [libdragon](https://github.com/DragonMinded/libdragon). Provides modular building blocks for N64 game development — pick the subsystems you need and build on top of them.

**Verified on real hardware** (Analogue 3D via SummerCart64) and the Ares emulator.

![Demo scene with textured cube, pillars, pyramid, billboard trees, and shadow casting](docs/images/INITIAL%20SCENE%20FRONT.png)

## Engine Subsystems

| Subsystem | Description | Docs |
|-----------|-------------|------|
| **Rendering** | Software 3D transforms + hardware RDP rasterization, Z-buffer | [RENDERING.md](docs/RENDERING.md) |
| **Mesh System** | Generic mesh builder, shape library (cube, pillar, platform, pyramid) | [MESH_SYSTEM.md](docs/MESH_SYSTEM.md) |
| **Camera** | Orbital camera, perspective projection, collision, frustum culling | [CAMERA.md](docs/CAMERA.md) |
| **Textures** | Sprite loading, TMEM management, per-frame stats | [TEXTURES.md](docs/TEXTURES.md) |
| **Lighting** | Blinn-Phong with configurable sun, point lights, and shadow casting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Shadows** | Blob shadows and projected shadow silhouettes on floor plane | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Billboards** | Camera-facing textured quads (spherical and cylindrical modes) | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Audio** | BGM streaming, SFX playback, mixer with 16 channels | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Collision** | Sphere and AABB colliders, raycasting, camera pushout | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Scene** | Object management, update/draw callbacks, multi-object scenes | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Input** | Analog stick, D-pad, C-buttons, face buttons | [INPUT.md](docs/INPUT.md) |
| **Menu System** | Tabbed data-driven menus with cancel/revert, controller nav | [MENU_SYSTEM.md](docs/MENU_SYSTEM.md) |
| **Text** | Font rendering with alignment, color, formatting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |

## Current Demo

A multi-object scene with lighting, shadows, and full camera controls running at 60 FPS:

- Textured rotating cube, pillars, pyramid, platform, and billboard trees
- Orbital camera controlled by analog stick (orbit) and C-buttons (zoom/shift)
- Per-face texture mapping with bilinear filtering and perspective correction
- Blinn-Phong lighting with configurable sun direction, color, and intensity
- Point light support with smooth quadratic attenuation
- Shadow casting (blob and projected silhouette modes)
- Hardware Z-buffer depth testing with 16-bit precision
- Frustum and backface culling
- Camera collision (raycast + sphere pushout + floor clamp)
- Tabbed start menu with settings, sound, and lighting controls
- FPS counter and rendering stats overlay

## Quick Start

### Prerequisites

1. **Docker Desktop** — `brew install --cask docker`
2. **ares emulator** — `brew install --cask ares`
3. **libdragon CLI** — `npm install -g libdragon`
4. **sc64deployer** (optional, for SummerCart64) — [download](https://github.com/Polprzewodnikowy/SummerCart64/releases)

### Build & Run

```bash
# Initialize libdragon (first time only — downloads Docker image)
libdragon init

# Build ROM
libdragon make

# Test in emulator
open -a ares hello_cube.z64

# Deploy to SummerCart64 (N64 must be ON, cart connected via USB)
sc64deployer upload hello_cube.z64
# Then reset the console to boot
```

## Controls

### Scene Mode

| Input | Action |
|-------|--------|
| Analog Stick | Orbit camera |
| C-Up / C-Down | Zoom in / out |
| C-Left / C-Right | Shift view up / down |
| Start | Open menu |

### Menu Mode

| Input | Action |
|-------|--------|
| D-pad Up/Down | Navigate items |
| D-pad Left/Right | Cycle option values |
| L/R | Switch tabs |
| A | Confirm & close |
| B | Cancel & close (reverts changes) |

### Lighting Menu

Configure scene lighting in real-time from the in-game menu:

![Lighting tab with sun direction, color, brightness, ambient, shadow mode, and point light controls](docs/images/MENU%20OPTIONS%20FOR%20LIGHTING.png)

### Shadow Casting

Projected shadows cast object silhouettes onto the floor plane based on the sun direction:

![Projected shadows from cube, pillar, pyramid, and platform onto the checkered floor](docs/images/SHADOW%20CAST%20EXAMPLE.png)

## Project Structure

```
n64-dev-engine/
├── src/
│   ├── main.c                 # Entry point, game loop, menu integration
│   ├── math/
│   │   └── vec3.h             # 3D vector math utilities
│   ├── render/
│   │   ├── camera.c/h         # Orbital camera, collision, frustum culling
│   │   ├── cube.c/h           # Cube geometry, MVP transform, rendering
│   │   ├── mesh.c/h           # Generic mesh builder + universal renderer
│   │   ├── mesh_defs.c/h      # Shape factory (pillar, platform, pyramid)
│   │   ├── floor.c/h          # Floor grid with Z-bias
│   │   ├── billboard.c/h      # Camera-facing textured quads
│   │   ├── lighting.c/h       # Blinn-Phong, point lights, configurable sun
│   │   ├── shadow.c/h         # Blob and projected shadow casting
│   │   └── texture.c/h        # Sprite loading, TMEM upload, stats
│   ├── input/
│   │   └── input.c/h          # Controller polling, analog/button mapping
│   ├── collision/
│   │   └── collision.c/h      # Sphere/AABB colliders, raycasting
│   ├── scene/
│   │   └── scene.c/h          # Scene graph, object management
│   ├── scenes/
│   │   └── demo_scene.c/h     # Demo scene with all engine features
│   ├── audio/
│   │   ├── audio.c/h          # Audio init, mixer, BGM/SFX playback
│   │   └── sound_bank.c/h     # Sound asset management
│   └── ui/
│       ├── text.c/h           # Font rendering, formatted text
│       └── menu.c/h           # Tabbed data-driven menu system
├── assets/                    # Source PNGs (converted at build time)
├── filesystem/                # Built sprite/audio assets (bundled into ROM)
├── docs/                      # Engine documentation
│   ├── images/                # Screenshots and visual references
│   ├── RENDERING.md           # 3D pipeline, RDP modes, Z-buffer
│   ├── CAMERA.md              # Camera system, coordinate spaces, math
│   ├── TEXTURES.md            # Texture pipeline, TMEM, asset workflow
│   ├── MESH_SYSTEM.md         # Mesh architecture, shape library
│   ├── MENU_SYSTEM.md         # Menu API, usage patterns, integration
│   ├── INPUT.md               # Controller mappings, input flow
│   ├── ARCHITECTURE.md        # N64 hardware, libdragon stack, subsystems
│   ├── SETUP.md               # Environment setup guide
│   └── WORKFLOW.md            # Development workflow
├── Makefile
└── CLAUDE.md                  # AI assistant project context
```

## Technical Overview

### Rendering Pipeline

```
CPU: Input → Camera → Scene Update → Model Matrix → MVP Transform
     → Frustum Cull → Backface Cull → Lighting → Viewport Map
RDP: Triangle Rasterize → Texture Sample → Z-Buffer → Framebuffer
```

- **Display**: 320x240, 16-bit color, triple-buffered
- **Depth**: 16-bit hardware Z-buffer
- **Triangles**: `rdpq_triangle()` with `TRIFMT_ZBUF_TEX` format
- **Textures**: 32x32 RGBA16 sprites, bilinear filtered, perspective-correct
- **Lighting**: CPU-side Blinn-Phong per face with configurable sun, point lights, and shadow casting

### Critical Hardware Rule

**Fill mode is ONLY for rectangles.** Triangles must use standard (1-cycle) mode or they crash on real hardware. The Ares emulator is lenient about this — always test on hardware.

## Documentation

- [Rendering Pipeline](docs/RENDERING.md) — RDP modes, Z-buffer, triangle formats, frame structure
- [Camera System](docs/CAMERA.md) — Orbital camera, coordinate system, math library, frustum culling
- [Texture System](docs/TEXTURES.md) — Asset pipeline, TMEM constraints, sprite slots
- [Menu System](docs/MENU_SYSTEM.md) — API reference, data model, integration patterns
- [Input System](docs/INPUT.md) — Controller layout, button mappings, analog handling
- [Architecture](docs/ARCHITECTURE.md) — N64 hardware overview, libdragon stack, lighting model
- [Environment Setup](docs/SETUP.md) — Prerequisites, Docker, emulator, SummerCart64
- [Development Workflow](docs/WORKFLOW.md) — Build cycle, debugging, asset pipeline

## Future Plans

- Load 3D models from files
- Scene/level definition system
- Isometric tile system for tactics gameplay
- Character units with animation
- Turn-based combat system

## License

MIT
