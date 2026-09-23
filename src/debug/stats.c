#include "stats.h"
#include <string.h>
#include <libdragon.h>

EngineStats g_stats_cur;
static EngineStats stats_last;
static bool header_sent = false;

void stats_frame_begin(void) {
    stats_last = g_stats_cur;
    memset(&g_stats_cur, 0, sizeof(g_stats_cur));
}

const EngineStats *stats_get(void) {
    return &stats_last;
}

uint32_t stats_tris_total(const EngineStats *s) {
    return s->tris_mesh + s->tris_floor + s->tris_shadow + s->tris_particle + s->tris_ui;
}

void stats_dump_csv(uint32_t frame_index) {
    const EngineStats *s = &stats_last;
    (void)s;   // debugf compiles out in release builds
    if (!header_sent) {
        debugf("STATS_HDR,frame,tris_total,tris_mesh,tris_floor,tris_shadow,tris_particle,tris_ui,"
               "rej_near,rej_guard,mesh_draws,mesh_culled,groups_drawn,groups_culled,"
               "mode_changes,tex_uploads,tex_bytes,fill_rects,particles_alive,particles_drawn,"
               "colliders,collision_pairs,raycasts,physics_bodies,physics_steps\n");
        header_sent = true;
    }
    debugf("STATS,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
           (unsigned long)frame_index, (unsigned long)stats_tris_total(s),
           (unsigned long)s->tris_mesh, (unsigned long)s->tris_floor, (unsigned long)s->tris_shadow,
           (unsigned long)s->tris_particle, (unsigned long)s->tris_ui,
           (unsigned long)s->tris_rejected_near, (unsigned long)s->tris_rejected_guard,
           (unsigned long)s->mesh_draws, (unsigned long)s->mesh_culled_frustum,
           (unsigned long)s->groups_drawn, (unsigned long)s->groups_culled_backface,
           (unsigned long)s->mode_changes, (unsigned long)s->tex_uploads,
           (unsigned long)s->tex_upload_bytes, (unsigned long)s->fill_rects,
           (unsigned long)s->particles_alive, (unsigned long)s->particles_drawn,
           (unsigned long)s->colliders, (unsigned long)s->collision_pairs,
           (unsigned long)s->raycasts, (unsigned long)s->physics_bodies,
           (unsigned long)s->physics_steps);
}
