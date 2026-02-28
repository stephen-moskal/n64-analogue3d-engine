# Texture System

The engine's texture system manages loading sprite assets from the ROM filesystem, uploading them to the RDP's texture memory (TMEM), and tracking per-frame statistics.

## Architecture

```
PNG assets (assets/*.png)
       ↓  [build time: mksprite]
Sprite files (filesystem/*.sprite)
       ↓  [ROM: bundled into .dfs]
sprite_load() → sprite_t* slots
       ↓  [per-frame]
rdpq_sprite_upload() → TMEM (4KB)
       ↓
RDP samples during rasterization
```

## TMEM (Texture Memory)

The N64 RDP has **4096 bytes** of on-chip texture memory (TMEM). All textures must fit within TMEM at the time of rasterization. The engine uploads textures one at a time per face, so each texture must individually fit in TMEM.

**Current textures:** 32x32 pixels, RGBA16 format = 32 * 32 * 2 = **2048 bytes** per texture (fits in TMEM).

## Asset Pipeline

### Build Time

PNG files in `assets/` are converted to libdragon `.sprite` format during build:

```makefile
MKSPRITE_FLAGS ?= --format RGBA16

filesystem/%.sprite: assets/%.png
    $(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"
```

The `.sprite` files are bundled into a DFS filesystem archive (`.dfs`) which is appended to the ROM.

### Runtime Loading

```c
void texture_init(void) {
    for (int i = 0; i < 6; i++) {
        slots[i] = sprite_load(cube_face_paths[i]);
        assertf(slots[i] != NULL, "Failed to load %s", ...);
        assertf(sprite_fits_tmem(slots[i]), "Sprite too large for TMEM", ...);
    }
}
```

`sprite_load()` reads from the ROM filesystem (paths prefixed with `rom:/`). The `sprite_fits_tmem()` check catches oversized textures at load time rather than crashing during rendering.

## Texture Slots

Textures are managed via a simple slot array:

```c
#define TEX_MAX_SLOTS 16

// Pre-defined cube face slots
#define TEX_CUBE_FRONT  0
#define TEX_CUBE_BACK   1
#define TEX_CUBE_TOP    2
#define TEX_CUBE_BOTTOM 3
#define TEX_CUBE_RIGHT  4
#define TEX_CUBE_LEFT   5
```

Slots 0-5 are used for cube faces. Slots 6-15 are available for future objects.

## Per-Frame Upload

Before drawing each face, its texture is uploaded to TMEM:

```c
texture_upload(face_tex_slot[f], TILE0);
```

This calls `rdpq_sprite_upload(tile, sprite, NULL)` which:
1. Uploads pixel data to TMEM
2. Configures the tile descriptor (size, format, stride)
3. May use internal caching to skip redundant uploads

## Render Mode for Textures

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);  // texture_color * prim_color
rdpq_mode_persp(true);                        // Perspective-correct interpolation
rdpq_mode_filter(FILTER_BILINEAR);            // Bilinear filtering
```

- `RDPQ_COMBINER_TEX_FLAT` multiplies the texture sample by the primitive color (used for lighting tint)
- Perspective correction is essential for 3D — without it, textures swim as polygons rotate
- Bilinear filtering smooths texel boundaries

## UV Coordinates

Each face quad uses the full texture (0 to 32 in texel space):

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

## Per-Frame Statistics

The texture system tracks stats that are displayed in the debug HUD:

```c
typedef struct {
    int tmem_bytes_used;    // Total TMEM bytes uploaded this frame
    int upload_count;       // Number of sprite uploads this frame
    int triangle_count;     // Triangles submitted this frame
} TextureStats;
```

Stats are reset each frame with `texture_stats_reset()` and displayed as:
```
T:12 U:6 TMEM:12288B
```
(12 triangles, 6 uploads, 12288 bytes total TMEM traffic)

## Adding New Textures

1. Place a PNG in `assets/` (recommended: 32x32, power-of-2 dimensions)
2. It will auto-convert to `.sprite` during build
3. Add a slot define in `texture.h`: `#define TEX_MY_OBJECT 6`
4. Load in `texture_init()`: `slots[TEX_MY_OBJECT] = sprite_load("rom:/my_texture.sprite");`
5. Upload before drawing: `texture_upload(TEX_MY_OBJECT, TILE0);`

### Size Constraints

| Texture Size | Format | TMEM Usage | Fits? |
|-------------|--------|------------|-------|
| 32x32 | RGBA16 | 2048B | Yes |
| 64x64 | RGBA16 | 8192B | No (exceeds 4KB) |
| 64x32 | RGBA16 | 4096B | Yes (exactly) |
| 32x32 | CI4 | 512B + palette | Yes |

For larger textures, consider CI4/CI8 (indexed color) or split into tiles.

## Source Files

| File | Purpose |
|------|---------|
| [src/render/texture.h](../src/render/texture.h) | Slot defines, stats struct, API |
| [src/render/texture.c](../src/render/texture.c) | Load, upload, stats tracking |
| [Makefile](../Makefile) | Asset conversion rules (PNG → sprite) |
