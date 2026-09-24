#ifndef STATS_H
#define STATS_H

/*
 * Unified per-frame engine counters (ROADMAP_v2 P1.2).
 *
 * Producers add to g_stats_cur during the frame with STATS_ADD / STATS_INC /
 * STATS_SET. stats_frame_begin() (called once at the top of the main loop)
 * publishes the finished frame and zeroes the counters, so stats_get() always
 * returns the last complete frame. Counters stay in release builds unless the
 * Makefile sets ENGINE_STATS=0. Header is libdragon-free so pure-C modules
 * (collision, physics) can use it in host tests.
 */

#include <stdint.h>

#ifndef ENGINE_STATS
#define ENGINE_STATS 1
#endif

typedef struct {
    // Objects / culling (mesh_draw calls)
    uint32_t mesh_draws;             // mesh_draw() calls
    uint32_t mesh_culled_frustum;    // rejected by the bounding-sphere test
    uint32_t groups_drawn;           // face groups submitted
    uint32_t groups_culled_backface; // face groups skipped as back-facing

    // Triangles submitted to the RDP, by source
    uint32_t tris_mesh;
    uint32_t tris_floor;
    uint32_t tris_shadow;
    uint32_t tris_particle;
    uint32_t tris_ui;                // menu background, transitions
    uint32_t tris_rejected_near;     // mesh triangles dropped at the near plane
    uint32_t tris_rejected_guard;    // mesh triangles dropped by the guard band
    uint32_t tris_culled_backface;   // mesh triangles dropped by the per-triangle winding test

    // Audio (set by snd_update)
    uint32_t snd_voices;             // sound-effect voices playing
    uint32_t snd_buffers;            // audio buffers mixed this frame

    // RDP state traffic
    uint32_t mode_changes;           // rdpq_set_mode_standard() in mesh_draw
    uint32_t tex_uploads;            // texture loads issued (after culling)
    uint32_t tex_upload_bytes;
    uint32_t fill_rects;             // rdpq_fill_rectangle (sky, UI panels)

    // Simulation
    uint32_t particles_alive;
    uint32_t particles_drawn;
    uint32_t colliders;
    uint32_t collision_pairs;        // overlapping pairs found this frame
    uint32_t raycasts;
    uint32_t physics_bodies;
    uint32_t physics_steps;          // fixed steps run this frame
} EngineStats;

extern EngineStats g_stats_cur;

void stats_frame_begin(void);          // publish last frame, zero the counters
const EngineStats *stats_get(void);    // last complete frame
uint32_t stats_tris_total(const EngineStats *s);

// CSV over debugf: "STATS_HDR,..." once, then "STATS,<frame>,..." per call
void stats_dump_csv(uint32_t frame_index);

#if ENGINE_STATS
  #define STATS_ADD(field, n)  (g_stats_cur.field += (uint32_t)(n))
  #define STATS_SET(field, v)  (g_stats_cur.field  = (uint32_t)(v))
#else
  #define STATS_ADD(field, n)  ((void)0)
  #define STATS_SET(field, v)  ((void)0)
#endif
#define STATS_INC(field) STATS_ADD(field, 1)

#endif
