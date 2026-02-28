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
├── input/input       [controller polling]
├── render/camera     [orbital camera, 3D math]
├── render/cube       [geometry, transforms, rendering]
│   ├── render/camera
│   ├── render/lighting
│   └── render/texture
├── render/lighting   [Blinn-Phong calculation]
├── render/texture    [sprite loading, TMEM upload]
├── ui/text           [font rendering]
└── ui/menu           [settings menus]
    └── ui/text
```

### Initialization Order

```c
debug_init_isviewer();      // Debug output (ISViewer)
debug_init_usblog();        // Debug output (USB)
display_init(...);          // Framebuffers
rdpq_init();                // RDP command queue
dfs_init(...);              // ROM filesystem
input_init();               // Joypad
lighting_init(&config);     // Light parameters
texture_init();             // Load sprites from ROM
cube_init();                // Model matrix
text_init();                // Load fonts
menu_init(&menu, title);    // Menu state
camera_init(&cam, &preset); // Camera matrices
surface_alloc(...);         // Z-buffer
```

### Frame Loop

```c
while (1) {
    // Timing
    fps = 1.0 / delta_seconds;

    // Input
    input_update(&input_state);
    // Menu toggle (Start button)
    // Menu input OR camera input (mutually exclusive)

    // Update
    camera_update(&camera);
    cube_update();  // Auto-rotation

    // Render
    surface_t *fb = display_get();
    rdpq_attach(fb, &zbuf);
    rdpq_clear(bg_color);      // From menu selection
    rdpq_clear_z(ZBUF_MAX);

    cube_draw(&camera, &light);  // 3D geometry
    text_draw(...);              // HUD (if debug on)
    menu_draw(&menu);            // Menu overlay (if open)

    rdpq_detach_show();
}
```

## Rendering Pipeline

See [RENDERING.md](RENDERING.md) for the full pipeline documentation.

### Summary

```
CPU: Model Matrix → MVP = VP * Model → Per-face: Cull + Light + Transform
RDP: Rasterize → Texture Sample → Z-Test → Framebuffer
```

- Software transforms on CPU, hardware rasterization on RDP
- Hardware 16-bit Z-buffer (replaced painter's algorithm)
- `TRIFMT_ZBUF_TEX` vertex format: `{X, Y, Z, S, T, INV_W}`

## Lighting Model

Per-face Blinn-Phong lighting computed on the CPU:

```
color = ambient
      + diffuse * max(0, dot(normal, light_dir))
      + specular * pow(max(0, dot(normal, half_vec)), 32)
```

Where `half_vec = normalize(light_dir + view_dir)`.

### Default Light Configuration

| Component | Value | Notes |
|-----------|-------|-------|
| Ambient | (0.15, 0.15, 0.20) | Slightly blue tint |
| Diffuse | (0.85, 0.80, 0.70) | Warm white |
| Direction | normalized(1, 1, 1) | Upper-right-front |
| Specular | 0.5 intensity, shininess ~32 | Blinn-Phong |

The lit color modulates the per-face base color and texture:
```c
final_pixel = texture_sample * (face_base_color * lighting) / 255
```

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
| Code + data | ~255KB | Current ROM size |
| Audio buffers | ~64KB | Reserved for future |
| **Available** | **~3MB** | For game assets and logic |

With Expansion Pak (8MB), an additional 4MB is available.

## Optimization Notes

### CPU
- Dirty flag on camera (skip matrix recompute when unchanged)
- Frustum culling rejects entire objects before per-face work
- Backface culling skips ~50% of faces on convex objects

### RDP
- Batch triangles by render state to minimize mode changes
- One texture upload per face (6 per cube) — batch by texture for multiple objects
- Z-buffer eliminates need for CPU-side depth sorting

### Memory
- Use `surface_alloc()` for Z-buffer (allocated once, reused every frame)
- Sprite slots are loaded once at init, uploaded to TMEM per-frame as needed
- Menu/text configs are stack-allocated or static

## Source Files

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, game loop, subsystem init |
| `src/render/camera.c/h` | Camera, 3D math, frustum culling |
| `src/render/cube.c/h` | Cube geometry and rendering |
| `src/render/lighting.c/h` | Blinn-Phong lighting |
| `src/render/texture.c/h` | Texture loading and TMEM management |
| `src/input/input.c/h` | Controller input |
| `src/ui/text.c/h` | Text rendering |
| `src/ui/menu.c/h` | Menu system |
