#include "floor.h"
#include "texture.h"
#include <math.h>

#define FLOOR_HALF_SIZE  500.0f
#define TILE_COUNT       10
#define TILE_SIZE        (FLOOR_HALF_SIZE * 2.0f / TILE_COUNT)
#define VERT_COUNT       (TILE_COUNT + 1)

// Z bias: push floor depth slightly farther to prevent Z-fighting with
// the cube.  At far orbital distances the cube and floor map to nearly
// identical Z-buffer values (N64's 16-bit Z has very coarse quantization
// past ~0.96).  A small additive bias gives several extra Z-buffer steps
// of clearance without visible artifacts.
#define Z_BIAS  0.005f

// RDP coordinate guard band — vertices outside this range
// overflow fixed-point math and cause rendering artifacts
#define GUARD_X_MIN  -1024.0f
#define GUARD_X_MAX   1344.0f
#define GUARD_Y_MIN  -1024.0f
#define GUARD_Y_MAX   1264.0f

// Checkered tile colors (before lighting)
#define TILE_LIGHT_R  0xB0
#define TILE_LIGHT_G  0xB0
#define TILE_LIGHT_B  0xB0

#define TILE_DARK_R   0x50
#define TILE_DARK_G   0x50
#define TILE_DARK_B   0x50

// Static to avoid stack usage on N64.
// TRIFMT_ZBUF format: {X, Y, Z} — 3 floats per vertex (no texture coords)
static float grid[VERT_COUNT][VERT_COUNT][3];
static bool  grid_valid[VERT_COUNT][VERT_COUNT];

void floor_draw(const Camera *cam, const LightConfig *light) {
    // Compute lighting for floor (normal faces up)
    float normal[3] = {0.0f, 1.0f, 0.0f};
    float view_dir[3] = {cam->view_dir.x, cam->view_dir.y, cam->view_dir.z};
    color_t lit = lighting_calculate(light, normal, view_dir, NULL);

    // Pre-compute lit tile colors
    color_t col_light = RGBA32(
        (TILE_LIGHT_R * lit.r) / 255,
        (TILE_LIGHT_G * lit.g) / 255,
        (TILE_LIGHT_B * lit.b) / 255, 255);
    color_t col_dark = RGBA32(
        (TILE_DARK_R * lit.r) / 255,
        (TILE_DARK_G * lit.g) / 255,
        (TILE_DARK_B * lit.b) / 255, 255);

    // Project all grid vertices once — shared edges eliminate sub-pixel gaps
    // between adjacent tiles that cause horizontal line artifacts.
    // Uses TRIFMT_ZBUF: only {X, Y, Z} needed (no texture on floor).
    for (int row = 0; row <= TILE_COUNT; row++) {
        for (int col = 0; col <= TILE_COUNT; col++) {
            float x = -FLOOR_HALF_SIZE + col * TILE_SIZE;
            float z = -FLOOR_HALF_SIZE + row * TILE_SIZE;

            vec3_t world_pos = {x, FLOOR_Y, z};
            vec4_t clip;
            mat4_mul_vec3(&clip, &cam->vp, &world_pos);

            // Reject vertices behind or very close to camera (prevents
            // extreme perspective divide and degenerate triangles)
            if (clip.w < 1.0f) {
                grid_valid[row][col] = false;
                continue;
            }

            float inv_w = 1.0f / clip.w;
            float sx = (clip.x * inv_w * 0.5f + 0.5f) * 320.0f;
            float sy = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * 240.0f;

            // Guard band: reject vertices that would overflow RDP fixed-point
            if (sx < GUARD_X_MIN || sx > GUARD_X_MAX ||
                sy < GUARD_Y_MIN || sy > GUARD_Y_MAX) {
                grid_valid[row][col] = false;
                continue;
            }

            grid_valid[row][col] = true;
            grid[row][col][0] = sx;
            grid[row][col][1] = sy;
            float depth = clip.z * inv_w * 0.5f + 0.5f + Z_BIAS;
            if (depth > 1.0f) depth = 1.0f;
            grid[row][col][2] = depth;
        }
    }

    // Flat-colored rendering with Z-buffer
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_zbuf(true, true);

    // Draw tiles batched by color to minimize prim_color changes.
    // Pass 0: light tiles, Pass 1: dark tiles.
    int tri_count = 0;
    for (int pass = 0; pass < 2; pass++) {
        rdpq_set_prim_color(pass == 0 ? col_light : col_dark);

        for (int row = 0; row < TILE_COUNT; row++) {
            for (int col = 0; col < TILE_COUNT; col++) {
                // Select tiles for this pass:
                // pass 0 (light): (row+col) is odd
                // pass 1 (dark):  (row+col) is even
                int is_light = (row + col) & 1;
                if ((pass == 0) != is_light) continue;

                // All 4 corners must be valid
                if (!grid_valid[row][col]     || !grid_valid[row][col+1] ||
                    !grid_valid[row+1][col+1] || !grid_valid[row+1][col])
                    continue;

                // Two triangles per tile: (BL, BR, TR) and (BL, TR, TL)
                rdpq_triangle(&TRIFMT_ZBUF,
                    grid[row][col], grid[row][col+1], grid[row+1][col+1]);
                rdpq_triangle(&TRIFMT_ZBUF,
                    grid[row][col], grid[row+1][col+1], grid[row+1][col]);
                tri_count += 2;
            }
        }
    }

    texture_stats_add_triangles(tri_count);
}
