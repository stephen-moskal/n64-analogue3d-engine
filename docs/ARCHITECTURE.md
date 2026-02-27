# Architecture Overview

Understanding N64 hardware and the libdragon software stack.

## N64 Hardware

### CPU: VR4300

- **Architecture:** MIPS III 64-bit
- **Clock:** 93.75 MHz
- **Cache:** 16KB instruction, 8KB data
- **Memory:** 4MB RDRAM (8MB with Expansion Pak)

The CPU handles game logic, physics, AI, and orchestrates the RCP.

### RCP (Reality Co-Processor)

The RCP contains two processors:

#### RSP (Reality Signal Processor)
- **Purpose:** Geometry transformation, lighting, audio
- **Clock:** 62.5 MHz
- **Memory:** 4KB instruction, 4KB data (DMEM)
- **Architecture:** Vector processor (8x 16-bit SIMD)

The RSP runs microcode that transforms vertices, calculates lighting, and generates RDP commands.

#### RDP (Reality Display Processor)
- **Purpose:** Rasterization, texturing, blending
- **Features:**
  - Triangle rasterization
  - Texture mapping (4KB texture cache)
  - Z-buffering
  - Anti-aliasing
  - Alpha blending

### Memory Map

```
0x00000000 - 0x003FFFFF  RDRAM (4MB/8MB)
0x04000000 - 0x040FFFFF  RSP DMEM/IMEM
0x04400000 - 0x044FFFFF  Video Interface
0x04600000 - 0x046FFFFF  Peripheral Interface
0x10000000 - 0x1FBFFFFF  Cartridge ROM
```

### Display

- **Resolution:** 320x240 (LO) or 640x480 (HI)
- **Color depth:** 16-bit or 32-bit
- **Framebuffer:** In RDRAM
- **Refresh:** 60Hz (NTSC) / 50Hz (PAL)

## libdragon Architecture

### Overview

libdragon is a modern, open-source N64 SDK providing:
- Hardware abstraction
- Display/audio subsystems
- Input handling
- RSP microcode (including custom)
- ROM filesystem

### Key Subsystems

#### Display

```c
display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
```

- Manages framebuffers
- Handles V-blank synchronization
- Triple buffering for smooth animation

#### RDPQ (RDP Queue)

```c
rdpq_init();
rdpq_attach(framebuffer, depth_buffer);
// ... draw commands ...
rdpq_detach_show();
```

- High-level RDP command interface
- Automatic state management
- Batches commands for efficiency
- Triangle rasterization via `rdpq_triangle()`

#### Joypad

```c
joypad_init();
joypad_poll();
joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
```

- Supports N64 and GameCube controllers
- Analog stick with deadzone handling
- Button state (pressed, held, released)

### Memory Management

#### Standard Allocation
```c
void *ptr = malloc(size);  // Cached memory
```

#### Uncached Allocation
```c
void *ptr = malloc_uncached(size);  // For DMA buffers
```

The RSP uses DMA to access memory. DMA buffers must be:
- Uncached (to avoid coherency issues)
- 8-byte aligned

## Current Rendering Approach

Our Hello Cube demo uses a hybrid software/hardware approach:

### CPU (Software)
- 3D transformation (rotation matrices)
- Perspective projection
- Lighting calculation (Blinn-Phong)
- Backface culling
- Depth sorting (painter's algorithm)

### RDP (Hardware)
- Triangle rasterization via `rdpq_triangle()`
- Fill mode rendering
- Framebuffer writes

This approach is simpler than full RSP-accelerated 3D but sufficient for learning and small scenes.

## Lighting Model

We implement Blinn-Phong lighting in software:

```c
// Ambient component
color += ambient_light;

// Diffuse component (Lambert)
float ndotl = dot(normal, light_direction);
color += diffuse_light * max(0, ndotl);

// Specular component (Blinn-Phong)
vec3 half = normalize(light_direction + view_direction);
float ndoth = dot(normal, half);
color += specular_intensity * pow(max(0, ndoth), shininess);
```

Per-face lighting is computed on the CPU before drawing.

## Frame Rendering Pipeline

```
1. CPU: Poll input
         ↓
2. CPU: Update rotation matrix
         ↓
3. CPU: For each face:
        - Transform vertices
        - Calculate lighting
        - Project to screen
        - Depth sort
         ↓
4. CPU: Submit triangles to RDPQ
         ↓
5. RDP: Rasterize triangles
        Fill with solid color
        Write to framebuffer
         ↓
6. VI:  Display framebuffer
```

### Frame Timing

At 60 FPS, each frame has ~16.67ms:
- CPU budget: ~10-12ms (for software 3D)
- RDP budget: ~8-12ms (can overlap with CPU)

## Memory Budget (4MB Configuration)

| Resource | Approximate Size |
|----------|-----------------|
| Framebuffer x3 | 450KB |
| Z-buffer | 150KB |
| Audio buffers | 64KB |
| ROM filesystem | Variable |
| Game code/data | Remaining |

With Expansion Pak (8MB), significantly more room for assets.

## Optimization Guidelines

### CPU
- Avoid floating point when possible (use fixed point)
- Cache-friendly data layout
- Minimize cache misses
- Consider SIMD for batch operations

### RDP
- Reduce overdraw
- Batch triangles by render state
- Minimize mode changes between triangles

### Memory
- Use uncached memory for DMA buffers
- Align buffers to 8 bytes
- Pool allocations where possible

## Project Architecture

```
main.c
  ├── display_init()       → Display subsystem (320x240, triple buffer)
  ├── rdpq_init()          → RDP command queue
  ├── input_init()         → Joypad subsystem
  ├── lighting_init()      → Light configuration
  └── cube_init()          → Initial rotation matrix

Game Loop:
  1. input_update()        → Poll controller, get rotation delta
  2. cube_update()         → Update rotation matrix
  3. rdpq_attach_clear()   → Begin frame, clear screen
  4. cube_draw()           → Transform, light, project, rasterize
  5. rdpq_detach_show()    → Present frame
```

This architecture separates concerns and prepares for scaling to a full game engine.

## Future Enhancements

### Hardware-Accelerated 3D (tiny3d)
When tiny3d becomes available for Apple Silicon Docker images, we can upgrade to:
- RSP-accelerated vertex transformation
- Per-vertex lighting
- Hardware Z-buffering
- Texture mapping

### Additional Features
- Model loading (T3DM format)
- Skeletal animation
- Particle systems
- Multi-viewport rendering (split-screen)
