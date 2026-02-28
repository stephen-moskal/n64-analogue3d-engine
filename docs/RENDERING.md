# Rendering Pipeline

The engine uses a hybrid CPU/RDP rendering approach: the CPU handles 3D math (transforms, projection, lighting, culling) and the N64's RDP hardware rasterizes triangles and fills the framebuffer.

## Pipeline Overview

```
Input Poll → Camera Update → Scene Update
                                  ↓
                         Model Matrix (SRT)
                                  ↓
                         MVP = VP * Model
                                  ↓
                      Per-face: Frustum Cull
                                  ↓
                      Per-face: Backface Cull
                                  ↓
                      Per-face: Lighting Calc
                                  ↓
                      Per-vertex: MVP Transform
                                  ↓
                      Per-vertex: Perspective Divide
                                  ↓
                      Per-vertex: Viewport Map
                                  ↓
                       RDP: Triangle Rasterize
                                  ↓
                       RDP: Z-buffer Test/Write
                                  ↓
                        Framebuffer → Display
```

## Display Configuration

```c
display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
```

| Parameter | Value | Notes |
|-----------|-------|-------|
| Resolution | 320x240 | Low-res mode (standard for N64) |
| Color depth | 16-bit (RGBA5551) | 2 bytes per pixel |
| Framebuffers | 3 (triple buffered) | Smooth animation, ~450KB RDRAM |
| Gamma | None | Linear color space |

## Z-Buffer

The engine uses a hardware 16-bit depth buffer for correct occlusion, replacing the earlier painter's algorithm (back-to-front sorting).

```c
// Allocate once before game loop
surface_t zbuf = surface_alloc(FMT_RGBA16, 320, 240);

// Each frame: attach both color and depth
rdpq_attach(fb, &zbuf);
rdpq_clear(bg_color);
rdpq_clear_z(ZBUF_MAX);

// Enable Z-buffer read + write in render mode
rdpq_mode_zbuf(true, true);
```

**Key details:**
- Format: `FMT_RGBA16` (16-bit depth, same format token as color)
- `ZBUF_MAX` clears to maximum depth (farthest)
- Z values mapped to `[0, 1]` range after perspective divide: `z = ndc_z * 0.5 + 0.5`
- 16-bit precision is sufficient for near/far ratio of 1:100 (near=10, far=1000)

## RDP Render Modes

The N64 RDP has several rendering modes. The engine uses two:

### Standard Mode (1-Cycle) — for triangles

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);  // Texture * prim color
rdpq_mode_persp(true);                        // Perspective-correct texturing
rdpq_mode_filter(FILTER_BILINEAR);            // Bilinear texture filtering
rdpq_mode_zbuf(true, true);                   // Z-buffer read + write
rdpq_set_prim_color(color);                   // Per-face lit color
```

### Fill Mode — for rectangles ONLY

```c
rdpq_set_mode_fill(color);
rdpq_fill_rectangle(x0, y0, x1, y1);
```

**CRITICAL HARDWARE RULE:** Fill mode **only** works with `rdpq_fill_rectangle()`. Drawing triangles in fill mode will crash on real hardware (RSP timeout in `display_get`). The Ares emulator is lenient about this — always verify on hardware.

## Triangle Formats

The `rdpq_triangle()` function accepts different vertex formats:

| Format | Floats/Vertex | Layout | Use Case |
|--------|--------------|--------|----------|
| `TRIFMT_FILL` | 2 | `{X, Y}` | Flat-colored 2D triangles |
| `TRIFMT_TEX` | 5 | `{X, Y, S, T, INV_W}` | Textured, no depth |
| `TRIFMT_ZBUF_TEX` | 6 | `{X, Y, Z, S, T, INV_W}` | Textured + Z-buffer |

The engine uses `TRIFMT_ZBUF_TEX` for 3D geometry and `TRIFMT_FILL` for UI overlays (menu background).

## Vertex Transform Pipeline

For each vertex, the CPU performs:

```c
// 1. Multiply by MVP matrix (model-view-projection)
vec4_t clip;
mat4_mul_vec3(&clip, &mvp, &vertex_local);  // clip space (homogeneous)

// 2. Perspective divide
float inv_w = 1.0f / clip.w;
float ndc_x = clip.x * inv_w;   // [-1, 1]
float ndc_y = clip.y * inv_w;   // [-1, 1]
float ndc_z = clip.z * inv_w;   // [-1, 1]

// 3. Viewport mapping (NDC → screen pixels)
screen_x = (ndc_x * 0.5 + 0.5) * 320.0;
screen_y = (1.0 - (ndc_y * 0.5 + 0.5)) * 240.0;  // Y flipped
z_depth  = ndc_z * 0.5 + 0.5;                      // [0, 1] for Z-buffer
```

## Culling

### Frustum Culling (per-object)

Before rendering an object, its bounding sphere is tested against the 6 frustum planes. If fully outside any plane, the entire object is skipped.

```c
if (!camera_sphere_visible(cam, &object_position, bounding_radius)) return;
```

### Backface Culling (per-face)

Face normals are transformed to world space via the model matrix upper-3x3, then dotted with the camera-to-object direction. Faces pointing away from the camera are skipped.

```c
float facing = dot(world_normal, to_camera_dir);
if (facing < 0.0f) continue;  // Face points away
```

## Lighting

Per-face Blinn-Phong lighting computed on the CPU before rasterization. See [ARCHITECTURE.md](ARCHITECTURE.md) for the full lighting model.

The lit color modulates the base face color:
```c
color_t lit = lighting_calculate(light, world_normal, view_dir);
uint8_t r = (face_color.r * lit.r) / 255;  // Multiply base × light
```

The result is set as prim color, combined with the texture via `RDPQ_COMBINER_TEX_FLAT`.

## Alpha Blending

Used for the menu overlay background:

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);        // src*alpha + dst*(1-alpha)
rdpq_set_prim_color(RGBA32(0, 0, 0, 160));        // Semi-transparent black
rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);          // 2D triangle in standard mode
```

This produces a semi-transparent overlay by blending with the existing framebuffer contents.

## Frame Structure

```c
while (1) {
    // === UPDATE ===
    input_update(&state);
    camera_update(&camera);
    cube_update();

    // === RENDER ===
    surface_t *fb = display_get();       // Acquire framebuffer
    rdpq_attach(fb, &zbuf);              // Attach color + depth

    rdpq_clear(bg_color);               // Clear background
    rdpq_clear_z(ZBUF_MAX);             // Clear depth

    cube_draw(&camera, &light_config);  // 3D geometry (Z-buffered)

    // 2D overlays (text, menus)
    text_draw(...);
    menu_draw(...);

    rdpq_detach_show();                 // Present frame
}
```

## Performance Notes

- At 60 FPS, each frame has ~16.67ms total budget
- CPU and RDP can overlap: CPU prepares next frame while RDP rasterizes current
- Minimize RDP mode changes between triangles (batch by render state)
- The current cube (12 triangles, 6 texture uploads) runs well within budget
- Future optimization: batch geometry by texture to reduce `rdpq_sprite_upload` calls

## Source Files

| File | Purpose |
|------|---------|
| [src/main.c](../src/main.c) | Frame loop, display init, Z-buffer setup |
| [src/render/cube.c](../src/render/cube.c) | MVP transform, culling, triangle submission |
| [src/render/camera.c](../src/render/camera.c) | View/projection matrices |
| [src/render/lighting.c](../src/render/lighting.c) | Blinn-Phong calculation |
