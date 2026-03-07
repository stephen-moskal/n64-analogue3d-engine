// ============================================================
// Sound Bank — Data-only configuration file
//
// Maps sound event IDs to DFS file paths. Edit this file to
// add, remove, or change sounds. Game logic only references
// SoundId enums, never file paths directly.
//
// To change a sound: replace the file in assets/audio/ and
// rebuild. No code changes needed.
//
// To add a new sound:
//   1. Add enum entry in sound_bank.h
//   2. Add path entry below
//   3. Drop audio file in assets/audio/sfx/ or assets/audio/music/
// ============================================================

#include "sound_bank.h"
#include <stddef.h>

const SoundDef sound_bank[SOUND_COUNT] = {
    [SOUND_NONE]       = { NULL,                                  SOUND_TYPE_SFX, 0 },

    // Menu sounds
    [SFX_MENU_OPEN]    = { "rom:/audio/sfx/menu_open.wav64",     SOUND_TYPE_SFX, 100 },
    [SFX_MENU_CLOSE]   = { "rom:/audio/sfx/menu_close.wav64",    SOUND_TYPE_SFX, 100 },
    [SFX_MENU_NAV]     = { "rom:/audio/sfx/menu_nav.wav64",      SOUND_TYPE_SFX, 80 },
    [SFX_MENU_SELECT]  = { "rom:/audio/sfx/menu_select.wav64",   SOUND_TYPE_SFX, 100 },

    // Object interaction
    [SFX_OBJ_SELECT]   = { "rom:/audio/sfx/obj_select.wav64",    SOUND_TYPE_SFX, 90 },
    [SFX_OBJ_DESELECT] = { "rom:/audio/sfx/obj_deselect.wav64",  SOUND_TYPE_SFX, 80 },
    [SFX_MODE_CHANGE]  = { "rom:/audio/sfx/mode_change.wav64",   SOUND_TYPE_SFX, 90 },

    // Collision
    [SFX_COLLISION]    = { "rom:/audio/sfx/collision.wav64",     SOUND_TYPE_SFX, 110 },

    // Music (WAV placeholder — replace with XM module for production)
    [BGM_DEMO]         = { "rom:/audio/music/demo.wav64",        SOUND_TYPE_BGM, 80 },
};
