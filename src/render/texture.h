#ifndef TEXTURE_H
#define TEXTURE_H

#include <libdragon.h>

#define TEX_MAX_SLOTS 16

// Cube face texture slot indices
#define TEX_CUBE_FRONT  0
#define TEX_CUBE_BACK   1
#define TEX_CUBE_TOP    2
#define TEX_CUBE_BOTTOM 3
#define TEX_CUBE_RIGHT  4
#define TEX_CUBE_LEFT   5

typedef struct {
    int tmem_bytes_used;
    int upload_count;
    int triangle_count;
} TextureStats;

void texture_init(void);
int  texture_upload(int slot, rdpq_tile_t tile);
void texture_stats_reset(void);
void texture_stats_add_triangles(int count);
const TextureStats *texture_stats_get(void);
void texture_cleanup(void);

// Dynamic per-slot loading (for scene-based texture management)
bool texture_load_slot(int slot, const char *path);
void texture_free_slot(int slot);
bool texture_slot_loaded(int slot);

#endif
