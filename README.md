# N64 Dev Engine

A Nintendo 64 homebrew game engine built with libdragon. This project serves as the foundation for building a Final Fantasy Tactics-style strategy game.

## Current State: Hello Cube Demo (Verified on Hardware)

Proof of concept complete — tested on Ares emulator and verified on real hardware (Analogue 3D via SummerCart64).

- Controller input (analog stick + D-pad)
- Software-rendered 3D with perspective projection
- Per-face lighting with specular highlights (Blinn-Phong)
- Backface culling and depth sorting
- 320x240 @ 16-bit color, triple-buffered

## Quick Start

### Prerequisites

1. **Docker Desktop** (for libdragon toolchain)
   ```bash
   brew install --cask docker
   ```

2. **ares emulator** (for testing)
   ```bash
   brew install --cask ares
   ```

3. **libdragon CLI**
   ```bash
   npm install -g libdragon
   ```

4. **sc64deployer** (for SummerCart64)
   ```bash
   # Download from https://github.com/Polprzewodnikowy/SummerCart64/releases
   ```

### Setup

```bash
cd n64-dev-engine

# Initialize libdragon (downloads Docker image)
libdragon init
```

### Build & Run

```bash
# Build ROM
libdragon make

# Test in emulator
open -a ares hello_cube.z64

# Deploy to SummerCart64
sc64deployer upload hello_cube.z64
```

## Project Structure

```
n64-dev-engine/
├── src/
│   ├── main.c        # Entry point & game loop
│   ├── cube.c/h      # Cube geometry, transform & rendering
│   ├── lighting.c/h  # Blinn-Phong lighting calculation
│   └── input.c/h     # Controller input handling
├── docs/
│   ├── SETUP.md      # Detailed setup guide
│   ├── WORKFLOW.md   # Development workflow
│   └── ARCHITECTURE.md
├── .devcontainer/    # VSCode DevContainer config
├── .vscode/          # VSCode tasks & settings
├── filesystem/       # ROM filesystem assets
└── Makefile
```

## Controls

| Input | Action |
|-------|--------|
| Analog Stick | Smooth rotation |
| D-Pad | Discrete rotation |
| (No input) | Auto-rotate |

## Technical Details

- **Graphics**: Uses libdragon's rdpq (RDP command queue) for hardware-accelerated triangle rasterization
- **3D Pipeline**: Software transform and projection, hardware rasterization
- **Lighting**: Per-face Blinn-Phong with ambient, diffuse, and specular components
- **Culling**: Backface culling based on transformed normals
- **Sorting**: Painter's algorithm (back-to-front) for correct occlusion
- **RDP Modes**: 1-cycle mode for triangles, fill mode for rectangles (fill mode only supports rectangles on real hardware)

## Documentation

- [Environment Setup](docs/SETUP.md)
- [Development Workflow](docs/WORKFLOW.md)
- [Architecture Overview](docs/ARCHITECTURE.md)

## Future Plans

- Add texture mapping
- Implement Z-buffer depth testing
- Load 3D models from files
- Add camera controls
- Build isometric tile system for tactics gameplay

## License

MIT
