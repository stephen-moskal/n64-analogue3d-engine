# N64 Dev Engine

A Nintendo 64 homebrew game engine built with [libdragon](https://github.com/DragonMinded/libdragon). Provides modular building blocks for N64 game development — pick the subsystems you need and build on top of them.

**Verified on real hardware** (Analogue 3D via SummerCart64) and the Ares emulator.

## Engine Subsystems

| Subsystem | Description | Docs |
|-----------|-------------|------|
| **Rendering** | Software 3D transforms + hardware RDP rasterization, Z-buffer | [RENDERING.md](docs/RENDERING.md) |
| **Camera** | Orbital camera, perspective projection, frustum culling | [CAMERA.md](docs/CAMERA.md) |
| **Textures** | Sprite loading, TMEM management, per-frame stats | [TEXTURES.md](docs/TEXTURES.md) |
| **Lighting** | Per-face Blinn-Phong (ambient + diffuse + specular) | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Input** | Analog stick, D-pad, C-buttons, face buttons | [INPUT.md](docs/INPUT.md) |
| **Menu System** | Data-driven menus with cancel/revert, controller nav | [MENU_SYSTEM.md](docs/MENU_SYSTEM.md) |
| **Text** | Font rendering with alignment, color, formatting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |

## Current Demo

A textured, lit, rotating cube with full camera controls and an in-game settings menu:

- Orbital camera controlled by analog stick (orbit) and C-buttons (zoom/shift)
- Per-face texture mapping with bilinear filtering and perspective correction
- Blinn-Phong lighting with specular highlights
- Hardware Z-buffer depth testing
- Frustum and backface culling
- Start menu with background color selection and debug text toggle
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
| A | Confirm & close |
| B | Cancel & close (reverts changes) |

## Project Structure

```
n64-dev-engine/
├── src/
│   ├── main.c                 # Entry point, game loop, menu integration
│   ├── render/
│   │   ├── camera.c/h         # Orbital camera, 3D math, frustum culling
│   │   ├── cube.c/h           # Cube geometry, MVP transform, rendering
│   │   ├── lighting.c/h       # Blinn-Phong lighting model
│   │   └── texture.c/h        # Sprite loading, TMEM upload, stats
│   ├── input/
│   │   └── input.c/h          # Controller polling, analog/button mapping
│   └── ui/
│       ├── text.c/h           # Font rendering, formatted text
│       └── menu.c/h           # Data-driven menu system
├── assets/                    # Source PNGs (converted at build time)
├── filesystem/                # Built sprite assets (bundled into ROM)
├── docs/                      # Engine documentation
│   ├── RENDERING.md           # 3D pipeline, RDP modes, Z-buffer
│   ├── CAMERA.md              # Camera system, coordinate spaces, math
│   ├── TEXTURES.md            # Texture pipeline, TMEM, asset workflow
│   ├── MENU_SYSTEM.md         # Menu API, usage patterns, integration
│   ├── INPUT.md               # Controller mappings, input flow
│   ├── ARCHITECTURE.md        # N64 hardware, libdragon stack, lighting
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
- **Lighting**: CPU-side Blinn-Phong per face, modulates texture color

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
- Multiple objects in a scene
- Scene/level definition system
- Isometric tile system for tactics gameplay
- Audio system

## License

MIT
