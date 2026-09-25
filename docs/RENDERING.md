# Rendering Pipeline

The engine uses a hybrid CPU/RDP rendering approach: the CPU handles 3D math (transforms, projection, lighting, culling) and the N64's RDP hardware rasterizes triangles and fills the framebuffer.

## Pipeline Overview

```
Scene update: input → game logic → camera → collision
                                  ↓
Per object (mesh_draw):  Model Matrix (SRT)
                                  ↓
                         Frustum cull (bounding sphere)
                                  ↓
                         MVP = VP * Model
                                  ↓
Per face group:          planar → back-face test + lighting once
                         curved → per triangle, below
                                  ↓
Per triangle corner:     MVP transform → near-plane reject → perspective divide
                         → viewport map → guard-band reject
                                  ↓
Per triangle (curved):   winding back-face test + lighting
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
surface_t *zbuf = display_get_zbuf();   // top of RDRAM, away from the framebuffers

// Each frame: attach both color and depth (engine.c)
rdpq_attach(fb, zbuf);

// scene_draw(): the sky replaces the colour clear when it covers the screen
if (sky_covers_screen()) sky_draw(); else rdpq_clear(bg_color);
rdpq_clear_z(ZBUF_MAX);

// Enable Z-buffer read + write in render mode
rdpq_mode_zbuf(true, true);
```

**Key details:**
- Format: `FMT_RGBA16` (16-bit depth, same format token as color)
- `ZBUF_MAX` clears to maximum depth (farthest)
- Z values mapped to `[0, 1]` range after perspective divide: `z = ndc_z * 0.5 + 0.5`
- Current projection: near=20, far=2000 (ratio 1:100)

### Z-Buffer Precision & Common Issues

The N64's 16-bit Z-buffer stores depth as a hyperbolic `1/z` distribution. This gives excellent precision near the camera but very coarse resolution at far distances. With 65,536 discrete Z values:

| Distance range | Approximate Z-buffer values | Notes |
|---------------|---------------------------|-------|
| near–2×near (20–40) | ~32,768 values | Half of all precision |
| 2×near–4×near (40–80) | ~16,384 values | Quarter |
| 100–500 | ~3,000–5,000 values | Typical gameplay range |
| 500–2000 | ~500–1,000 values | Far objects, Z-fighting likely |

**Near/far ratio is critical.** Doubling the near plane doubles Z precision across the entire depth range. The engine uses near=20, far=2000 (100:1 ratio). Avoid near < 10 — it wastes most Z-buffer precision on the first few units.

### Per-Vertex Clipping in `mesh_draw()`

Three safety checks prevent corrupt Z values from reaching the RDP:

1. **Near-plane rejection** (`clip.w < 1.0`): Vertices behind or very close to the camera produce garbage Z from the perspective divide. If any vertex fails, the entire triangle is rejected.

2. **Guard-band clipping** (±1024 screen bounds): Vertices projecting far off-screen overflow the RDP's 12.2 fixed-point coordinate math, causing rendering artifacts or hangs.

3. **Depth clamping** (`depth ∈ [0, 1]`): Out-of-range Z values are clamped before submission. Without this, objects past the far plane all map to depth=1.0 and Z-fight with each other.

```c
// Near-plane rejection
if (clip.w < 1.0f) { reject = true; break; }

// Guard-band check
if (screen_x < -1024 || screen_x > 1344 ||
    screen_y < -1024 || screen_y > 1264) { reject = true; break; }

// Depth clamp
float depth = ndc_z * 0.5f + 0.5f;
if (depth < 0.0f) depth = 0.0f;
if (depth > 1.0f) depth = 1.0f;
```

### Floor Z-Bias

The floor uses an additive Z-bias (`+0.005`) to push its depth slightly farther, preventing Z-fighting with objects resting on it. At far orbital distances, the floor and objects can map to nearly identical Z-buffer values due to the coarse 16-bit quantization.

### Known Limitations

- **Interpenetrating geometry** will always Z-fight at far distances — 16-bit Z cannot resolve sub-unit depth differences past ~500 units from camera
- **No proper triangle clipping**: Triangles that straddle the near plane are rejected entirely rather than clipped. This can cause popping at close range.
- **Floor-through-object artifacts**: At extreme orbital distances (>1000 units), the floor and object depths can collide. The Z_BIAS helps but doesn't eliminate this entirely.
- **Future mitigation**: For the FFT-style isometric camera, the fixed viewing angle and distance will keep most geometry in the high-precision Z range

## RDP Render Modes

The N64 RDP has several rendering modes. The engine's geometry uses the two below. Besides them, copy mode blits the cached UI text layers (`rdpq_tex_blit`, `ui_layer.c`), and translucent UI rectangles use standard mode with the blender (`ui_rect()`, see [Alpha Blending](#alpha-blending)):

### Standard Mode (1-Cycle) — for triangles

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);  // Texture * prim color
rdpq_mode_persp(true);                        // Perspective-correct texturing
rdpq_mode_filter(FILTER_BILINEAR);            // Bilinear texture filtering
rdpq_mode_zbuf(true, true);                   // Z-buffer read + write
rdpq_set_prim_color(color);                   // Lit color of the group or triangle
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
| `TRIFMT_ZBUF` | 3 | `{X, Y, Z}` | Z-buffered, no texture |
| `TRIFMT_ZBUF_TEX` | 6 | `{X, Y, Z, S, T, INV_W}` | Textured + Z-buffer |
| `TRIFMT_ZBUF_SHADE` | 7 | `{X, Y, Z, R, G, B, A}` | Shaded + Z-buffer (fog) |
| `TRIFMT_ZBUF_SHADE_TEX` | 10 | `{X, Y, Z, R, G, B, A, S, T, INV_W}` | Shaded + textured + Z (fog) |

The engine uses:
- `TRIFMT_ZBUF_TEX` for textured mesh materials (cube, billboards, benchmark boxes) when fog is OFF
- `TRIFMT_ZBUF` for flat-colored Z-buffered geometry: flat mesh materials when fog is OFF, the floor, shadows and particles (no texture coords)
- `TRIFMT_ZBUF_SHADE` for flat mesh materials with hardware fog (shade RGB = lit color, shade A = fog factor)
- `TRIFMT_ZBUF_SHADE_TEX` for textured mesh materials with hardware fog
- `TRIFMT_FILL` for the scene transition fade (2D triangles in standard mode with the blender); UI panels are rectangles (`ui_rect()`)

A textured format with a flat combiner is an RDP error the validator reports (defect D2): `mesh_draw()` picks the format from the material type and the fog state.

**Performance note:** Use the simplest format that fits your needs. `TRIFMT_ZBUF` skips texture gradient computation in `rdpq_triangle()`, which is meaningful at high triangle counts (floor: 200 triangles/frame).

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

Before rendering a mesh, `mesh_draw()` tests its world-space bounding sphere against the 6 frustum planes. If it is fully outside any plane, the entire object is skipped (`mesh_culled_frustum` in the stats).

```c
vec3_t center; float radius;
mesh_world_bounds(mesh, model, &center, &radius);   // mesh.h: centre transformed, radius × largest axis scale
if (!camera_sphere_visible(cam, &center, radius)) return;
```

The other passes cull their own way: a projected shadow is skipped when a sphere around its projected centre is off screen (`shadow.c`), and each particle is dropped when its screen square lies outside the screen or the guard band (`particle_draw.c`).

### Backface Culling

Flat (planar) face groups: the group normal is taken to world space through the model's normal matrix (`mesh_normal_matrix()`, the cofactor matrix) and dotted with the direction from the group's centre to the camera; a group facing away is skipped with all its triangles (`groups_culled_backface`). Curved groups (sphere bands) are culled per triangle after projection, by the sign of the screen-space area (`tris_culled_backface`): front faces are counter-clockwise, which is negative area once Y points down. Meshes with `backface_cull = false` (the billboard quad) skip both tests.

```c
// planar group
float facing = dot(world_normal, cam_pos - group_center_world);
if (facing <= 0.0f) continue;                  // whole group faces away
// curved group, per triangle
if (mesh_screen_area2(s0, s1, s2) >= 0.0f) continue;
```

The previous test (one normal per group from its first vertex, against the object-centre direction) made spheres vanish from some sides (D24).

## Lighting

Flat-shaded Blinn-Phong lighting computed on the CPU before rasterization: once per planar face group, and once per triangle (with the average of its vertex normals) for curved groups. See [ARCHITECTURE.md](ARCHITECTURE.md) for the full lighting model.

The lit color modulates the material's base color:
```c
color_t lit = lighting_calculate(light, world_normal, view_dir, world_pos);  // world_pos for point lights
uint8_t r = (mat->base_color[0] * lit.r) / 255;  // Multiply base × light
```

Without fog the result is set as prim color, combined with the texture via `RDPQ_COMBINER_TEX_FLAT` (or used alone via `RDPQ_COMBINER_FLAT`). With fog it goes into the shade RGB of each vertex, and shade alpha carries the fog factor.

## Alpha Blending

`RDPQ_BLENDER_MULTIPLY` in standard mode, colour and alpha from the prim colour. Used by translucent UI rectangles (the menu panel, HUD backdrops), the debug overlay's panel, the scene transition fade, and alpha-blended particles (additive ones use `RDPQ_BLENDER_ADDITIVE`, [PARTICLES.md](PARTICLES.md)). From `ui_rect()` in `ui_draw.c`:

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);        // src*alpha + dst*(1-alpha)
rdpq_set_prim_color(color);                      // e.g. RGBA32(0, 0, 0, 160)
rdpq_fill_rectangle(x0, y0, x1, y1);             // rectangles are safe in any mode
```

This blends with the existing framebuffer contents. Opaque rectangles use fill mode instead (faster), and the fade draws two `TRIFMT_FILL` triangles in the blended mode (`scene.c`).

## Frame Structure

The loop in `engine_run()` (`src/engine/engine.c`, [ENGINE.md](ENGINE.md)), simplified (profiler, stats and debug-tool calls omitted):

```c
while (1) {
    float dt = display_get_delta_time();   // time between presented frames, capped at 0.1 s
    surface_t *fb = display_get();    // Wait for a free framebuffer (triple buffering)
    snd_update(dt);                   // Mix the audio (poll point: AUDIO.md)
    input_poll(...);                  // this vblank's controller read (INPUT.md)
    scene_manager_update(&mgr, dt);   // scene on_update (menu, logic), physics, camera, collision

    rdpq_attach(fb, zbuf);            // Attach color + depth (zbuf = engine_zbuf())
    scene_manager_draw(&mgr);         // scene_draw(), then the transition fade
    overlay_draw(budget_ms);          // Debug overlay page (not while the menu is open)
    rdpq_detach_show();               // Present frame
    // 30 FPS mode: display_set_fps_limit(30) makes display_get() wait
}
```

Draw order inside `scene_draw()` for the demo: background (sky gradient or colour clear) → Z clear → floor → shadows (Z-read, no Z-write) → objects (meshes, billboards) → particles (alpha-blended, then additive; Z-read, no Z-write) → HUD text → menu.

## Performance Notes

- At 60 FPS, each frame has ~16.67ms total budget
- CPU and RDP can overlap: CPU prepares next frame while RDP rasterizes current
- The engine is CPU-bound: the demo's quiet view submits a few hundred triangles (200 of them floor) and the RDP is busy about a third of the frame. Measurements are in [BENCHMARKS.md](BENCHMARKS.md)
- Per-triangle code is linked in a hot-text block so it does not collide in the direct-mapped I-cache ([HARDWARE.md](HARDWARE.md))

### RDP State Change Cost

**`rdpq_set_mode_standard()` is expensive** — it resets the entire RDP pipeline and generates many RDP commands. Minimize calls by:
- Batching geometry by material type (only reset mode when type changes)
- Batching same-colored geometry (set `rdpq_set_prim_color()` once per color, not per primitive)

### Floor Optimization Case Study

The floor was originally 20×20 tiles (800 triangles) with per-tile color changes (400 `rdpq_set_prim_color` calls) using `TRIFMT_ZBUF_TEX` (6 floats, with unused texture coords). This caused FPS drops to ~30 when zoomed out.

Optimizations applied:
1. **Reduced grid 20×20 → 10×10**: 800 → 200 triangles, 441 → 121 vertex transforms
2. **Batched by color**: Draw all light tiles, then all dark tiles. 2 color changes instead of 400.
3. **Switched to `TRIFMT_ZBUF`**: 3 floats instead of 6. `rdpq_triangle()` skips texture gradient computation.
4. **Increased Z_BIAS 0.003 → 0.005**: Compensates for coarser Z interpolation at 10×10 grid density.

Result: ~4x fewer triangles, 200x fewer state changes, less CPU per-triangle. FPS recovered to 60.

## Source Files

| File | Purpose |
|------|---------|
| [src/engine/engine.c](../src/engine/engine.c) | Frame loop, display init, Z-buffer setup |
| [src/scene/scene.c](../src/scene/scene.c) | Per-frame background (sky or clear), draw order, transition fade |
| [src/render/mesh.c](../src/render/mesh.c) | `mesh_draw()`: culling, lighting, transform, triangle submission |
| [src/render/mesh.h](../src/render/mesh.h) | Mesh types, `mesh_world_bounds()`, `mesh_normal_matrix()`, `mesh_screen_area2()` |
| [src/render/floor.c](../src/render/floor.c) | Checkered floor (Z-bias, per-tile fog and point lights) |
| [src/render/shadow.c](../src/render/shadow.c) | Blob and projected shadows |
| [src/render/particle_draw.c](../src/render/particle_draw.c) | Particle renderer ([PARTICLES.md](PARTICLES.md)) |
| [src/render/atmosphere.c](../src/render/atmosphere.c) | Fog factor, sky gradient |
| [src/render/camera.c](../src/render/camera.c) | View/projection matrices, frustum |
| [src/render/lighting.c](../src/render/lighting.c) | Blinn-Phong calculation |
| [src/engine/hot_text.ld](../src/engine/hot_text.ld) | Placement of the render path in the I-cache |
