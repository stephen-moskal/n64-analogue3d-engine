# N64 Dev Engine

A Nintendo 64 homebrew game engine built with [libdragon](https://github.com/DragonMinded/libdragon). Provides modular building blocks for N64 game development — pick the subsystems you need and build on top of them.

**Verified on real hardware** (Analogue 3D via SummerCart64) and the Ares emulator. Developed on Windows 11 and macOS.

![Demo scene with textured cube, pillars, pyramid, billboard trees, and shadow casting](docs/images/INITIAL%20SCENE%20FRONT.png)

## Engine Subsystems

| Subsystem | Description | Docs |
|-----------|-------------|------|
| **Rendering** | Software 3D transforms + hardware RDP rasterization, Z-buffer | [RENDERING.md](docs/RENDERING.md) |
| **Mesh System** | Generic mesh builder, shape library (cube, pillar, platform, pyramid, sphere) | [MESH_SYSTEM.md](docs/MESH_SYSTEM.md) |
| **Camera** | Orbital camera, perspective projection, collision, frustum culling | [CAMERA.md](docs/CAMERA.md) |
| **Textures** | Sprite loading, TMEM management, per-frame stats | [TEXTURES.md](docs/TEXTURES.md) |
| **Lighting** | Blinn-Phong with configurable sun, point lights, and shadow casting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Shadows** | Blob shadows and projected shadow silhouettes on floor plane | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Billboards** | Camera-facing textured quads (spherical and cylindrical modes) | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Particles** | Emitter-based system with pool allocation, additive blend, direct RDP batch renderer | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Atmosphere** | Fog (hardware + CPU hybrid), sky gradients, 7 presets with linked lighting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Physics** | Semi-fixed timestep, gravity, bounce, impulse, ground detection, body presets | [PHYSICS.md](docs/PHYSICS.md) |
| **Audio** | BGM streaming, SFX playback, mixer with 16 channels | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| **Collision** | Sphere and AABB colliders, raycasting, camera pushout | [COLLISION.md](docs/COLLISION.md) |
| **Scene** | Object management, update/draw callbacks, scene reset, multi-object scenes | [SCENE_SYSTEM.md](docs/SCENE_SYSTEM.md) |
| **Input / Action Mapping** | Remappable game actions, per-context bindings, analog stick, runtime rebinding via menu | [INPUT.md](docs/INPUT.md) |
| **Menu System** | Tabbed data-driven menus with cancel/revert, controller nav | [MENU_SYSTEM.md](docs/MENU_SYSTEM.md) |
| **Text** | Font rendering with alignment, color, formatting | [ARCHITECTURE.md](docs/ARCHITECTURE.md) |

## Current Demo

A multi-object scene with lighting, atmosphere, shadows, and full camera controls running at 60 FPS:

- Textured rotating cube, pillars, pyramid, platform, and billboard trees
- Orbital camera controlled by analog stick (orbit) and C-buttons (zoom/shift)
- Per-face texture mapping with bilinear filtering and perspective correction
- Blinn-Phong lighting with configurable sun direction, color, and intensity
- Point lights with floor illumination, configurable intensity (up to 24x) and radius (up to 2000 units)
- Torch flame particles on pillar tops that auto-toggle with point light setting
- Shadow casting (blob and projected silhouette modes)
- Fog & atmosphere system with 7 presets (Clear Day, Overcast, Foggy, Dense Fog, Sunset, Dusk, Night)
- Atmosphere presets auto-configure linked lighting (sun intensity, ambient, sun color)
- Sky gradient rendering with smooth interpolated color bands
- Hardware Z-buffer depth testing with 16-bit precision
- Frustum and backface culling
- Camera collision (raycast + sphere pushout + floor clamp)
- Particle effects: fire/sparks and magic/energy bursts on pillar tops
- Physics ball demo: press B to spawn a bouncing sphere on the platform (gravity, bounce, re-launch)
- Remappable controls via action mapping system with in-game Controls tab
- Tabbed start menu with settings, sound, lighting, environment, and controls tabs
- Scene reset feature (soft reset without console restart)
- FPS counter and rendering stats overlay
- Developer tools in a Debug menu tab: overlay pages (stats, CPU profiler, memory, frame-time histogram, RDP load), CSV export over USB, RDP validator and frame capture, crash test, and a benchmark scene

## Quick Start

Full instructions, including the Windows USB driver and line-ending steps, are in [docs/SETUP.md](docs/SETUP.md).

### Prerequisites

| | Windows 11 | macOS |
|---|---|---|
| Docker Desktop (WSL2 backend on Windows) | `wsl --install --no-distribution`, reboot, `winget install -e --id Docker.DockerDesktop` | `brew install --cask docker` |
| Node.js ≥ 24 + libdragon CLI | `winget install -e --id OpenJS.NodeJS.LTS` then `npm install -g libdragon` | `brew install node` then `npm install -g libdragon` |
| ares emulator (enable Homebrew Mode) | `winget install -e --id ares-emulator.ares` | `brew install --cask ares` |
| sc64deployer (SummerCart64) | zip from the [releases](https://github.com/Polprzewodnikowy/SummerCart64/releases) → `C:\tools\sc64deployer` on PATH, plus the FTDI VCP driver | tgz from the releases → `/usr/local/bin` |

### Build & Run

```powershell
git clone --recurse-submodules <repo-url>
cd n64-analogue3d-engine
# Windows only: set core.autocrlf=false / core.eol=lf in the repo and libdragon/ (SETUP.md step 6)

libdragon init                       # first time: create the container and build libdragon into it
libdragon make                       # -> engine-debug.z64

ares .\engine-debug.z64                # emulator      (macOS: open -a ares engine-debug.z64)
sc64deployer upload .\engine-debug.z64 # cart, then power on / reset the console
sc64deployer debug                   # optional: live debugf() log from the console
```

## Controls

All game controls are remappable via the in-game Controls menu tab. Defaults shown below.

### Scene Mode (Default Bindings)

| Action | Default Button | Description |
|--------|---------------|-------------|
| Orbit Camera | Analog Stick | Horizontal/vertical camera orbit |
| Zoom In / Out | C-Up / C-Down | Camera distance (held) |
| Shift View Up / Down | C-Right / C-Left | Camera target Y (held) |
| Confirm | A | Interact / confirm |
| Cancel / Ball | B | Spawn/re-launch physics ball |
| Select Mode | Z | Toggle object selection |
| Cycle Next / Prev | D-Right / D-Left | Cycle through objects |
| Camera Mode | L / R | Cycle camera modes |
| Open Menu | Start | Fixed (not remappable) |

### Menu Mode (Fixed)

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

### Atmosphere & Point Lights

Night mode with point light sources illuminating the scene — torch flames on pillars, floor tile lighting, and linked atmosphere-lighting preset:

![Night atmosphere with point lights illuminating cube, pillars, floor tiles, and torch flame particles on pillar tops](docs/images/DARK%20WITH%20POINT%20LIGHTS.png)

### Environment Menu

Configure atmosphere presets, fog, and sky from the Environ tab — shown here with the Sunset preset active:

![Environ tab showing Sunset preset with fog and sky settings, purple-to-orange sky gradient visible behind the menu](docs/images/ENVIRONMENT%20MENU%20SUNSET.png)

## Project Structure

```
n64-analogue3d-engine/
├── src/
│   ├── main.c                 # Entry point, game loop, menu integration
│   ├── math/
│   │   └── vec3.h             # 3D vector math utilities
│   ├── render/
│   │   ├── camera.c/h         # Orbital camera, collision, frustum culling
│   │   ├── cube.c/h           # Cube geometry, MVP transform, rendering
│   │   ├── mesh.c/h           # Generic mesh builder + universal renderer
│   │   ├── mesh_defs.c/h      # Shape factory (pillar, platform, pyramid, sphere)
│   │   ├── floor.c/h          # Floor grid with Z-bias, point light illumination
│   │   ├── billboard.c/h      # Camera-facing textured quads
│   │   ├── lighting.c/h       # Blinn-Phong, point lights, configurable sun
│   │   ├── shadow.c/h         # Blob and projected shadow casting
│   │   ├── particle.c/h       # Particle system, emitters, direct RDP renderer
│   │   ├── atmosphere.c/h     # Fog, sky gradient, 7 presets, linked lighting
│   │   └── texture.c/h        # Sprite loading, TMEM upload, stats
│   ├── input/
│   │   ├── action.c/h         # Action mapping: remappable bindings, contexts
│   │   └── input.c/h          # Camera input adapter (reads from action API)
│   ├── collision/
│   │   └── collision.c/h      # Sphere/AABB colliders, raycasting
│   ├── physics/
│   │   └── physics.c/h        # Physics: gravity, bounce, impulse, ground detection
│   ├── scene/
│   │   └── scene.c/h          # Scene graph, object management, soft reset
│   ├── scenes/
│   │   └── demo_scene.c/h     # Demo scene with all engine features
│   ├── audio/
│   │   ├── audio.c/h          # Audio init, mixer, BGM/SFX playback
│   │   └── sound_bank.c/h     # Sound asset management
│   └── ui/
│       ├── text.c/h           # Font rendering, formatted text
│       └── menu.c/h           # Tabbed data-driven menu system
├── assets/                    # Source PNGs and WAVs (converted at build time)
├── filesystem/                # Generated .sprite/.wav64 outputs (git-ignored), bundled into the ROM
├── libdragon/                 # libdragon submodule (preview branch, pinned)
├── tools/                     # gen_placeholder_audio.py
├── docs/                      # Engine documentation
│   ├── images/                # Screenshots and visual references
│   ├── ROADMAP_v2.md          # Current development roadmap (phases, tooling, T3D)
│   ├── ROADMAP.md             # v1 roadmap: delivery record for Features 1–10
│   ├── ARCHITECTURE.md        # N64 hardware, libdragon stack, subsystems
│   ├── RENDERING.md           # 3D pipeline, RDP modes, Z-buffer
│   ├── MESH_SYSTEM.md         # Mesh architecture, shape library
│   ├── CAMERA.md              # Camera system, coordinate spaces, math
│   ├── TEXTURES.md            # Texture pipeline, TMEM, asset workflow
│   ├── COLLISION.md           # Colliders, raycasts, layers
│   ├── PHYSICS.md             # Physics bodies, timestep, presets
│   ├── SCENE_SYSTEM.md        # Scene lifecycle, transitions, reset
│   ├── INPUT.md               # Controller mappings, action contexts
│   ├── MENU_SYSTEM.md         # Menu API, usage patterns, integration
│   ├── SETUP.md               # Environment setup (Windows 11 + macOS)
│   └── WORKFLOW.md            # Build/test/debug workflow
├── .libdragon/config.json     # libdragon CLI: Docker image + submodule vendoring
├── .vscode/tasks.json         # Build / run / upload / debug tasks (per-OS)
├── .gitattributes             # LF line endings (required by the Linux build container)
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
- **Atmosphere**: Hybrid fog (hardware RDP + CPU), sky gradients, 7 presets with linked lighting hints
- **Footprint** (2026-09-12 build): ROM 344,064 bytes; ELF text 285,720 / data 82,360 / bss 34,400 bytes

### Critical Hardware Rule

**Fill mode is ONLY for rectangles.** Triangles must use standard (1-cycle) mode or they crash on real hardware. The Ares emulator is lenient about this — always test on hardware.

## Documentation

- [Development Roadmap v2](docs/ROADMAP_v2.md) — Phased plan: tooling & benchmarking, engine hardening, graphics features, Tiny3D, animation, game framework
- [Roadmap v1](docs/ROADMAP.md) — Delivery record for Features 1–10
- [Architecture](docs/ARCHITECTURE.md) — N64 hardware overview, libdragon stack, lighting, shadows, particles, fog
- [Rendering Pipeline](docs/RENDERING.md) — RDP modes, Z-buffer, triangle formats, frame structure
- [Mesh System](docs/MESH_SYSTEM.md) — Mesh builder API, shape library, universal renderer
- [Camera System](docs/CAMERA.md) — Orbital camera, coordinate system, math library, frustum culling
- [Texture System](docs/TEXTURES.md) — Asset pipeline, TMEM constraints, sprite slots
- [Collision System](docs/COLLISION.md) — Colliders, raycasting, layers, overlap queries
- [Physics System](docs/PHYSICS.md) — Gravity, bounce, impulse, semi-fixed timestep, body presets
- [Scene System](docs/SCENE_SYSTEM.md) — Lifecycle, scene manager, transitions, soft reset
- [Input System](docs/INPUT.md) — Controller layout, action mapping, analog handling
- [Menu System](docs/MENU_SYSTEM.md) — API reference, data model, integration patterns
- [Debugging](docs/DEBUGGING.md) — Debug tab, log channels, RDP validator and capture, crash inspector, unit tests
- [Profiling](docs/PROFILING.md) — CPU profiler, stats, memory, frame time, RDP counters, CSV rows
- [Benchmarks](docs/BENCHMARKS.md) — Measured performance on the Analogue 3D and the benchmark baseline
- [Hardware Notes](docs/HARDWARE.md) — Analogue 3D + SummerCart64 facts, RDP rules
- [Environment Setup](docs/SETUP.md) — Windows 11 and macOS: Docker, libdragon CLI, ares, SummerCart64 + driver
- [Development Workflow](docs/WORKFLOW.md) — Build cycle, debugging, asset pipeline, hardware testing

## Roadmap at a Glance

See [ROADMAP_v2.md](docs/ROADMAP_v2.md) for the full plan with per-feature test plans and benchmark targets.

| Phase | Goal |
|-------|------|
| 0 — Environment & baseline | Windows 11 workflow reproducible, ROM verified on ares + Analogue 3D (done 2026-09-12) |
| 1 — Tooling & benchmarking | Per-phase profiler, unified stats, memory/frame-time overlays, RDP validator + capture, benchmark scene + CSV, CI (done 2026-09-23) |
| 2 — Engine hardening | Fix known defects (resource leaks on reset, renderer state, hot paths, mesh memory), settings/input cleanup |
| 3 — Graphics features (CPU path) | Vertex cache, Gouraud lighting, sprite animation, CI4/TMEM residency, fonts, VI options, decals, skybox, sorted transparency |
| 4 — Milestone 1: Tiny3D | libdragon upgrade, RSP rendering, Blender/Fast64 → GLTF → ROM pipeline, 64+ objects at 60 FPS |
| 5 — Milestone 2: Animation | Skeletal animation, character controller, state machine, entity pattern, FFT grid track |
| 6 — Milestone 3: Game framework | Souls-like arena, turn-based battle system, save/load, AI |

## License

MIT
