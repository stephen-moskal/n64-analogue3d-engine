# CLAUDE.md - N64 Dev Engine

## Project Overview
Nintendo 64 homebrew game engine built with libdragon (unstable branch). Long-term goal: Final Fantasy Tactics-style strategy game. Current state: proof-of-concept rotating cube verified on real hardware (Analogue 3D via SummerCart64).

## Build & Deploy

```bash
# Build ROM (uses Docker container internally)
libdragon make

# Clean build
libdragon make clean

# Test in emulator
open -a ares hello_cube.z64

# Deploy to SummerCart64 (N64 must be ON, SC64 connected via USB)
"/Users/smoskal/Downloads/sc64deployer" upload hello_cube.z64
# Then reset the console to boot
```

## Architecture

### Source Files
- `src/main.c` — Entry point, display init (320x240, 16-bit, triple-buffered), game loop
- `src/cube.c/h` — 3D cube geometry, rotation matrices, perspective projection, face rendering
- `src/lighting.c/h` — Blinn-Phong lighting (ambient + diffuse + specular), `LightConfig` struct
- `src/input.c/h` — Joypad polling (analog stick + D-pad), `InputState` struct

### Rendering Pipeline
Software 3D transform + hardware RDP rasterization:
1. CPU: rotation matrix, perspective projection, backface culling, depth sort (painter's algorithm), lighting
2. RDP: triangle rasterization via `rdpq_triangle()`, rectangle fills via `rdpq_fill_rectangle()`

### Critical Hardware Rules
- **Fill mode is ONLY for rectangles.** Triangles MUST use 1-cycle mode or they crash on real hardware:
  ```c
  // CORRECT for triangles:
  rdpq_set_mode_standard();
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
  rdpq_set_prim_color(color);
  rdpq_triangle(&TRIFMT_FILL, v1, v2, v3);

  // CORRECT for rectangles:
  rdpq_set_mode_fill(color);
  rdpq_fill_rectangle(x0, y0, x1, y1);

  // WRONG — crashes on hardware, works in emulators:
  rdpq_set_mode_fill(color);
  rdpq_triangle(...);
  ```
- Ares emulator is lenient — always verify on hardware/FPGA
- RSP timeout in `display_get` usually means RDP pipeline misconfiguration

## Conventions
- All source in `src/`, headers alongside their `.c` files
- Makefile uses libdragon's `n64.mk` include system
- ROM assets go in `filesystem/` (bundled into `.dfs`)
- IDE will show clang errors for libdragon headers — this is expected (cross-compilation toolchain)

## Reference Documentation
- Project docs: `docs/SETUP.md`, `docs/WORKFLOW.md`, `docs/ARCHITECTURE.md`
- N64 development reference: `../awesome-n64-development/`
