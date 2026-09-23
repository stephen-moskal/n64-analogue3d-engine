# Texture System

The texture system loads sprite assets from the ROM filesystem into a table of numbered slots and uploads them to the RDP's texture memory (TMEM) when a textured face group is drawn.

## Architecture

```
PNG assets (assets/*.png)
       ↓  [build time: mksprite --format RGBA16]
Sprite files (filesystem/*.sprite)
       ↓  [ROM: bundled into the .dfs]
texture_load_slot() → sprite_t* in one of 16 slots
       ↓  [per draw, per visible textured face group]
texture_upload() → rdpq_sprite_upload() → TMEM (4 KB)
       ↓
RDP samples during rasterization
```

## TMEM (Texture Memory)

The N64 RDP has **4096 bytes** of on-chip texture memory (TMEM). All textures must fit within TMEM at the time of rasterization. The engine uploads one texture at a time, so each texture must individually fit in TMEM.

**Current textures:** 32x32 pixels, RGBA16 format = 32 * 32 * 2 = **2048 bytes** per texture (fits in TMEM).

## Asset Pipeline

### Build Time

Every PNG in `assets/` is converted to libdragon's `.sprite` format during the build (the Makefile uses a wildcard, so no list to maintain):

```makefile
MKSPRITE_FLAGS ?= --format RGBA16

filesystem/%.sprite: assets/%.png
    $(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"
```

The `.sprite` files are bundled into a DFS filesystem archive (`.dfs`) which is appended to the ROM. They are generated, not committed; `libdragon make clean` deletes them.

### Runtime Loading

```c
bool texture_load_slot(int slot, const char *path);  // frees the slot first, then sprite_load()
void texture_free_slot(int slot);
bool texture_slot_loaded(int slot);
void texture_cleanup(void);                          // frees every slot
void texture_init(void);                             // loads the six cube faces into slots 0-5 (idempotent)
const char *texture_cube_face_path(int face);        // their rom:/ paths
```

`texture_load_slot()` reads from the ROM filesystem (paths start with `rom:/`; a wrong path stops at libdragon's `File not found` assertion) and asserts `sprite_fits_tmem()`, so an oversized texture fails at load time rather than during rendering.

## Texture Slots

`TEX_MAX_SLOTS` is 16. Slots are global, not owned by a scene: two scenes that use the same slot number replace each other's texture.

| Slots | Used by |
|---|---|
| 0–5 | cube faces (`TEX_CUBE_FRONT` … `TEX_CUBE_LEFT` in `texture.h`) |
| 6, 7 | billboard marker and tree (`TEX_BILLBOARD_MARKER` / `TEX_BILLBOARD_TREE` in `demo_scene.c`) |
| 8–15 | free |

### Per-scene lifecycle

A scene lists its textures in `Scene.texture_paths` / `texture_slots` / `texture_count`. `scene_init()` loads them before the scene's `on_init`, and `scene_cleanup()` frees them after `on_cleanup`, so a Reset Scene or a scene switch cannot leak them. The demo declares its eight textures this way.

A texture loaded by hand with `texture_load_slot()` is **not** freed by `scene_cleanup()`: free it in `on_cleanup` (the benchmark scene loads with `texture_init()` and `texture_load_slot()` and calls `texture_cleanup()` in its cleanup). See [SCENE_SYSTEM.md](SCENE_SYSTEM.md).

## Per-Frame Upload

`mesh_draw()` calls `texture_upload(material->texture_slot, TILE0)` for each textured face group that survives back-face culling, unless an earlier group of the same draw already uploaded that slot with no RDP mode reset in between: a box whose six faces share one texture uploads it once per draw. Each call issues `rdpq_sprite_upload()`, which loads the pixels into TMEM and configures the tile descriptor. Nothing stays resident across draws (TMEM residency is planned in ROADMAP_v2 P3.3), so every `mesh_draw()` that shows a texture uploads it again.

`texture_upload()` is deliberately left out of the hot-text block: its libdragon upload path (~7 KB of code) would not fit next to `mesh_draw()` in the I-cache, which is why `mesh_draw()` avoids redundant uploads ([HARDWARE.md](HARDWARE.md)).

## Render Mode for Textures

Without fog (`mesh_draw()`):

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);  // texture_color * prim_color (the lit colour)
rdpq_mode_persp(true);                        // Perspective-correct interpolation
rdpq_mode_filter(FILTER_BILINEAR);            // Bilinear filtering
```

- `RDPQ_COMBINER_TEX_FLAT` multiplies the texture sample by the primitive color (used for lighting tint)
- Perspective correction is essential for 3D — without it, textures swim as polygons rotate
- Bilinear filtering smooths texel boundaries

With fog on, textured groups use `RDPQ_COMBINER_TEX_SHADE` with `TRIFMT_ZBUF_SHADE_TEX` and `RDPQ_FOG_STANDARD`; the lit colour travels in the shade channels ([RENDERING.md](RENDERING.md)). Materials with `alpha_cutout` set (the billboard quad) also enable alpha compare, so transparent texels are discarded.

## UV Coordinates

UVs are in texels, not 0–1: a 32×32 texture spans 0 to 32. The cube faces (`face_uvs` in `cube.c`) and the billboard quad use the full texture:

```c
static const float face_uvs[4][2] = {
    {0.0f,     32.0f},   // Bottom-left
    {32.0f,    32.0f},   // Bottom-right
    {32.0f,    0.0f},    // Top-right
    {0.0f,     0.0f},    // Top-left
};
```

UV coordinates are passed as part of the vertex data in `TRIFMT_ZBUF_TEX` format:
`{screen_x, screen_y, z_depth, s, t, inv_w}`

## Statistics

Uploads are counted in the unified per-frame stats (`src/debug/stats.h`): `tex_uploads` and `tex_upload_bytes` (sprite stride × height per upload). They appear as `U:` in the demo HUD (`T:<triangles> U:<uploads> COL:… RAY:…`), on the Stats overlay page, and as the `tex_uploads` / `tex_bytes` columns of `STATS` CSV rows ([PROFILING.md](PROFILING.md)).

## Adding a Texture

Drop the PNG in `assets/` (it is converted on the next build), pick a free slot, declare it in your scene's `texture_paths` / `texture_slots`, and reference the slot from a `Material.texture_slot` or `BillboardData.texture_slot`. The step-by-step recipe is in [EXTENDING.md](EXTENDING.md).

### Size Constraints

| Texture Size | Format | TMEM Usage | Fits? |
|-------------|--------|------------|-------|
| 32x32 | RGBA16 | 2048B | Yes |
| 64x64 | RGBA16 | 8192B | No (exceeds 4KB) |
| 64x32 | RGBA16 | 4096B | Yes (exactly) |
| 32x32 | CI4 | 512B + palette | Yes (not used yet: the Makefile converts every PNG to RGBA16) |

For larger textures, consider CI4/CI8 (indexed color) or split into tiles.

## Source Files

| File | Purpose |
|------|---------|
| [src/render/texture.h](../src/render/texture.h) | Slot defines, API |
| [src/render/texture.c](../src/render/texture.c) | Slot table, load/free, upload and upload stats |
| [src/scene/scene.c](../src/scene/scene.c) | Loads and frees each scene's declared textures |
| [Makefile](../Makefile) | Asset conversion rules (PNG → sprite) |
