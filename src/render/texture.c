#include "texture.h"
#include <assert.h>

static sprite_t *slots[TEX_MAX_SLOTS];
static int slot_count = 0;
static TextureStats stats;

static const char *cube_face_paths[] = {
    "rom:/face_front.sprite",
    "rom:/face_back.sprite",
    "rom:/face_top.sprite",
    "rom:/face_bottom.sprite",
    "rom:/face_right.sprite",
    "rom:/face_left.sprite",
};

void texture_init(void) {
    for (int i = 0; i < 6; i++) {
        slots[i] = sprite_load(cube_face_paths[i]);
        assertf(slots[i] != NULL, "Failed to load %s", cube_face_paths[i]);
        assertf(sprite_fits_tmem(slots[i]), "Sprite %s too large for TMEM", cube_face_paths[i]);
        debugf("Loaded texture: %s (%dx%d)\n", cube_face_paths[i],
               slots[i]->width, slots[i]->height);
    }
    slot_count = 6;
    texture_stats_reset();
}

int texture_upload(int slot, rdpq_tile_t tile) {
    assert(slot >= 0 && slot < slot_count);
    assert(slots[slot] != NULL);

    int bytes = rdpq_sprite_upload(tile, slots[slot], NULL);
    stats.upload_count++;
    stats.tmem_bytes_used += bytes;
    return bytes;
}

void texture_stats_reset(void) {
    stats.tmem_bytes_used = 0;
    stats.upload_count = 0;
    stats.triangle_count = 0;
}

void texture_stats_add_triangles(int count) {
    stats.triangle_count += count;
}

const TextureStats *texture_stats_get(void) {
    return &stats;
}

void texture_cleanup(void) {
    for (int i = 0; i < slot_count; i++) {
        if (slots[i]) {
            sprite_free(slots[i]);
            slots[i] = NULL;
        }
    }
    slot_count = 0;
}
