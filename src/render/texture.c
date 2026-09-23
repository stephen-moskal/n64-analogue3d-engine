#include "texture.h"
#include <assert.h>
#include "../debug/stats.h"

static sprite_t *slots[TEX_MAX_SLOTS];
static int slot_count = 0;

static const char *cube_face_paths[] = {
    "rom:/face_front.sprite",
    "rom:/face_back.sprite",
    "rom:/face_top.sprite",
    "rom:/face_bottom.sprite",
    "rom:/face_right.sprite",
    "rom:/face_left.sprite",
};

// Recompute the "highest slot in use + 1" bound after a slot is freed
static void update_slot_count(void) {
    slot_count = 0;
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (slots[i]) slot_count = i + 1;
    }
}

// Load the six cube-face textures into slots 0-5. Idempotent: reloading frees
// the previous sprites first (D1), and slots above 5 are left untouched (D21).
void texture_init(void) {
    for (int i = 0; i < 6; i++) {
        bool ok = texture_load_slot(i, cube_face_paths[i]);
        assertf(ok, "Failed to load %s", cube_face_paths[i]);
        (void)ok;   // assertf is compiled out in release
    }
}

// Paths of the cube-face textures, for scenes that declare them in
// Scene.texture_paths instead of calling texture_init().
const char *texture_cube_face_path(int face) {
    return (face >= 0 && face < 6) ? cube_face_paths[face] : NULL;
}

int texture_upload(int slot, rdpq_tile_t tile) {
    assert(slot >= 0 && slot < slot_count);
    assert(slots[slot] != NULL);

    rdpq_sprite_upload(tile, slots[slot], NULL);

    // Compute TMEM bytes from sprite metadata (return value of
    // rdpq_sprite_upload can be 0 due to internal caching)
    surface_t surf = sprite_get_pixels(slots[slot]);
    int bytes = surf.stride * slots[slot]->height;
    STATS_INC(tex_uploads);
    STATS_ADD(tex_upload_bytes, bytes);
    return bytes;
}

void texture_cleanup(void) {
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (slots[i]) {
            sprite_free(slots[i]);
            slots[i] = NULL;
        }
    }
    slot_count = 0;
}

bool texture_load_slot(int slot, const char *path) {
    assert(slot >= 0 && slot < TEX_MAX_SLOTS);
    // Free existing sprite in slot if any
    if (slots[slot]) {
        sprite_free(slots[slot]);
        slots[slot] = NULL;
    }
    slots[slot] = sprite_load(path);
    if (!slots[slot]) {
        debugf("Failed to load texture: %s\n", path);
        return false;
    }
    assertf(sprite_fits_tmem(slots[slot]), "Sprite %s too large for TMEM", path);
    debugf("Loaded texture slot %d: %s (%dx%d)\n", slot, path,
           slots[slot]->width, slots[slot]->height);
    // Track highest slot in use
    if (slot >= slot_count) slot_count = slot + 1;
    return true;
}

void texture_free_slot(int slot) {
    assert(slot >= 0 && slot < TEX_MAX_SLOTS);
    if (slots[slot]) {
        sprite_free(slots[slot]);
        slots[slot] = NULL;
    }
    update_slot_count();
}

bool texture_slot_loaded(int slot) {
    if (slot < 0 || slot >= TEX_MAX_SLOTS) return false;
    return slots[slot] != NULL;
}
