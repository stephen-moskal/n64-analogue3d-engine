#include "particle_internal.h"
#include "../debug/stats.h"
#include "atmosphere.h"
#include "../engine/hot.h"
#include "../engine/engine_config.h"

// Particle renderer (hot path). The simulation is in particle.c.

ENGINE_HOT void particle_draw(const Camera *cam) {
    if (!particle_initialized) return;

    int alive = 0;
    for (int i = 0; i < particle_pool_allocated; i++) {
        if (particle_pool[i].alive) alive++;
    }
    STATS_SET(particles_alive, alive);
    if (alive == 0) return;

    bool use_fog = atmosphere_get_fog_enabled();

    // A particle is a quad spanned by the camera's right and up axes, so it
    // is parallel to the image plane (the camera has no roll) and projects to
    // a screen-aligned square at a single depth. One transform of the centre
    // gives all four corners exactly: half-size in pixels = scale * P / w,
    // with P the projection's x/y scale times half the screen size (D14; this
    // replaces four corner transforms and a frustum sphere test).
    const float px = cam->proj.m[0][0] * (0.5f * ENGINE_SCREEN_W);
    const float py = cam->proj.m[1][1] * (0.5f * ENGINE_SCREEN_H);

    // The loop reads the matrix for every particle: a copy on the stack, as
    // the camera (scene_view_camera(), pinned at 0x0290) shares D-cache
    // colours with the particle pool (src/engine/hot_data.ld, D35)
    const mat4_t vp = cam->vp;
    const float near_plane = cam->near_plane;

    // Set RDP mode ONCE for all particles (additive blend)
    // Z-read ON, Z-write OFF (same as shadows)
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_zbuf(true, false);
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);

    int tri_count = 0;
    uint8_t last_r = 0, last_g = 0, last_b = 0, last_a = 0;
    bool color_set = false;

    for (int i = 0; i < particle_pool_allocated; i++) {
        const Particle *p = &particle_pool[i];
        if (!p->alive) continue;

        // Skip fully transparent particles
        if (p->color[3] == 0) continue;

        vec4_t clip;
        mat4_mul_vec3(&clip, &vp, &p->position);
        if (clip.w < near_plane) continue;              // centre in front of the near plane

        float inv_w = 1.0f / clip.w;
        float ndc_z = clip.z * inv_w;
        if (ndc_z > 1.0f) continue;                     // beyond the far plane

        float cx = (clip.x * inv_w * 0.5f + 0.5f) * (float)ENGINE_SCREEN_W;
        float cy = (1.0f - (clip.y * inv_w * 0.5f + 0.5f)) * (float)ENGINE_SCREEN_H;
        float hx = p->scale * px * inv_w;
        float hy = p->scale * py * inv_w;

        // Entirely off screen, or past the guard band
        if (cx + hx < 0.0f || cx - hx > (float)ENGINE_SCREEN_W || cy + hy < 0.0f || cy - hy > (float)ENGINE_SCREEN_H)
            continue;
        if (cx - hx < ENGINE_GUARD_X_MIN || cx + hx > ENGINE_GUARD_X_MAX ||
            cy - hy < ENGINE_GUARD_Y_MIN || cy + hy > ENGINE_GUARD_Y_MAX)
            continue;

        float depth = ndc_z * 0.5f + 0.5f;
        if (depth < 0.0f) depth = 0.0f;

        // Apply fog dimming: distant particles fade to black (correct for additive blend)
        uint8_t pr = p->color[0], pg = p->color[1];
        uint8_t pb = p->color[2], pa = p->color[3];
        if (use_fog) {
            float dim = 1.0f - fog_calculate_factor(clip.w);
            pr = (uint8_t)(pr * dim);
            pg = (uint8_t)(pg * dim);
            pb = (uint8_t)(pb * dim);
            pa = (uint8_t)(pa * dim);
            if (pa == 0 && pr == 0) continue;
        }

        // Only change prim color when it differs
        if (!color_set ||
            pr != last_r || pg != last_g ||
            pb != last_b || pa != last_a) {
            rdpq_set_prim_color(RGBA32(pr, pg, pb, pa));
            last_r = pr;
            last_g = pg;
            last_b = pb;
            last_a = pa;
            color_set = true;
        }

        // Corners TL, TR, BL, BR (screen Y points down)
        float screen[4][3] ENGINE_NOINIT;
        screen[0][0] = cx - hx; screen[0][1] = cy - hy; screen[0][2] = depth;
        screen[1][0] = cx + hx; screen[1][1] = cy - hy; screen[1][2] = depth;
        screen[2][0] = cx - hx; screen[2][1] = cy + hy; screen[2][2] = depth;
        screen[3][0] = cx + hx; screen[3][1] = cy + hy; screen[3][2] = depth;

        // Emit 2 triangles: (TL, TR, BR) and (TL, BR, BL)
        rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[1], screen[3]);
        rdpq_triangle(&TRIFMT_ZBUF, screen[0], screen[3], screen[2]);
        tri_count += 2;
    }

    STATS_ADD(tris_particle, tri_count);
    STATS_ADD(particles_drawn, tri_count / 2);
}
