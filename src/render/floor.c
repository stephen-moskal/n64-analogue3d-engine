#include "floor.h"
#include "texture.h"
#include "atmosphere.h"
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

// Compute additive point light contribution for a floor tile.
// Returns 0-255 intensity per channel (modulate with tile base color before adding).
// Floor normal = (0,1,0) so ndotl simplifies to just the Y component.
static color_t floor_point_light_add(const LightConfig *light, float wx, float wz) {
    float r = 0, g = 0, b = 0;
    float pos_y = FLOOR_Y;

    for (int i = 0; i < light->point_light_count; i++) {
        const PointLight *pl = &light->point_lights[i];
        if (!pl->active) continue;

        float lx = pl->position.x - wx;
        float ly = pl->position.y - pos_y;
        float lz = pl->position.z - wz;

        float dist_sq = lx * lx + ly * ly + lz * lz;
        float radius_sq = pl->radius * pl->radius;
        if (dist_sq >= radius_sq) continue;

        float dist = sqrtf(dist_sq);
        if (dist < 0.001f) continue;

        // ndotl for floor normal (0,1,0) = ly / dist
        float ndotl = ly / dist;
        if (ndotl <= 0) continue;

        // Quadratic falloff: (1 - (d/r)^2)^2
        float t = dist / pl->radius;
        float atten = (1.0f - t * t);
        atten *= atten;
        atten *= pl->intensity;

        r += pl->color[0] * ndotl * atten;
        g += pl->color[1] * ndotl * atten;
        b += pl->color[2] * ndotl * atten;
    }

    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;

    return RGBA32((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255), 0);
}

// Static to avoid stack usage on N64.
// TRIFMT_ZBUF format: {X, Y, Z} — 3 floats per vertex (no texture coords)
static float grid[VERT_COUNT][VERT_COUNT][3];
static float grid_depth[VERT_COUNT][VERT_COUNT];  // clip.w for CPU fog
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
            grid_depth[row][col] = clip.w;
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

    const FogConfig *fog = atmosphere_get_fog();
    int tri_count = 0;
    bool has_pt = (light->point_light_count > 0);

    if (has_pt || fog->enabled) {
        // Per-tile rendering: point lights and/or fog need per-tile color
        color_t last_color = RGBA32(0, 0, 0, 0);

        for (int row = 0; row < TILE_COUNT; row++) {
            for (int col = 0; col < TILE_COUNT; col++) {
                if (!grid_valid[row][col]     || !grid_valid[row][col+1] ||
                    !grid_valid[row+1][col+1] || !grid_valid[row+1][col])
                    continue;

                int is_light = (row + col) & 1;
                color_t tile = is_light ? col_light : col_dark;

                // Add point light contribution per tile
                if (has_pt) {
                    float cx = -FLOOR_HALF_SIZE + (col + 0.5f) * TILE_SIZE;
                    float cz = -FLOOR_HALF_SIZE + (row + 0.5f) * TILE_SIZE;
                    color_t pt = floor_point_light_add(light, cx, cz);
                    int tile_base_r = is_light ? TILE_LIGHT_R : TILE_DARK_R;
                    int tile_base_g = is_light ? TILE_LIGHT_G : TILE_DARK_G;
                    int tile_base_b = is_light ? TILE_LIGHT_B : TILE_DARK_B;
                    int fr = tile.r + (tile_base_r * pt.r) / 255;
                    int fg = tile.g + (tile_base_g * pt.g) / 255;
                    int fb = tile.b + (tile_base_b * pt.b) / 255;
                    if (fr > 255) fr = 255;
                    if (fg > 255) fg = 255;
                    if (fb > 255) fb = 255;
                    tile = RGBA32(fr, fg, fb, 255);
                }

                // Apply fog blend
                if (fog->enabled) {
                    float avg_depth = (grid_depth[row][col] + grid_depth[row][col+1] +
                                       grid_depth[row+1][col+1] + grid_depth[row+1][col]) * 0.25f;
                    tile = fog_blend_color(tile, avg_depth);
                }

                if (tile.r != last_color.r || tile.g != last_color.g ||
                    tile.b != last_color.b) {
                    rdpq_set_prim_color(tile);
                    last_color = tile;
                }

                rdpq_triangle(&TRIFMT_ZBUF,
                    grid[row][col], grid[row][col+1], grid[row+1][col+1]);
                rdpq_triangle(&TRIFMT_ZBUF,
                    grid[row][col], grid[row+1][col+1], grid[row+1][col]);
                tri_count += 2;
            }
        }
    } else {
        // Fast batched path: no point lights, no fog (2 prim_color calls total)
        for (int pass = 0; pass < 2; pass++) {
            rdpq_set_prim_color(pass == 0 ? col_light : col_dark);

            for (int row = 0; row < TILE_COUNT; row++) {
                for (int col = 0; col < TILE_COUNT; col++) {
                    int is_light = (row + col) & 1;
                    if ((pass == 0) != is_light) continue;

                    if (!grid_valid[row][col]     || !grid_valid[row][col+1] ||
                        !grid_valid[row+1][col+1] || !grid_valid[row+1][col])
                        continue;

                    rdpq_triangle(&TRIFMT_ZBUF,
                        grid[row][col], grid[row][col+1], grid[row+1][col+1]);
                    rdpq_triangle(&TRIFMT_ZBUF,
                        grid[row][col], grid[row+1][col+1], grid[row+1][col]);
                    tri_count += 2;
                }
            }
        }
    }

    texture_stats_add_triangles(tri_count);
}
