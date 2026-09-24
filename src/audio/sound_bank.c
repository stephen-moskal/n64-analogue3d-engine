// ============================================================
// Sound bank: data only
//
// Maps SoundId to its file and mix settings. Replacing a sound is a file
// swap in assets/audio/ and a rebuild; no code changes. The encoding of each
// file is set by the Makefile per directory (SFX and music), see AUDIO.md.
// ============================================================

#include "sound_bank.h"
#include <stddef.h>

// Priorities: UI feedback outranks world sounds, so a burst of collisions
// never silences the menu.
#define PRIO_UI     20
#define PRIO_WORLD  10

const SoundDef sound_bank[SOUND_COUNT] = {
    [SOUND_NONE]       = { NULL, SND_BUS_SFX, 0.0f, 0, 0, 0 },

    // Menu sounds
    [SFX_MENU_OPEN]    = { "rom:/audio/sfx/menu_open.wav64",    SND_BUS_SFX, 0.80f, PRIO_UI, 0, 0 },
    [SFX_MENU_CLOSE]   = { "rom:/audio/sfx/menu_close.wav64",   SND_BUS_SFX, 0.80f, PRIO_UI, 0, 0 },
    [SFX_MENU_NAV]     = { "rom:/audio/sfx/menu_nav.wav64",     SND_BUS_SFX, 0.60f, PRIO_UI, 0, 0 },
    [SFX_MENU_SELECT]  = { "rom:/audio/sfx/menu_select.wav64",  SND_BUS_SFX, 0.80f, PRIO_UI, 0, 0 },

    // Object interaction
    [SFX_OBJ_SELECT]   = { "rom:/audio/sfx/obj_select.wav64",   SND_BUS_SFX, 0.70f, PRIO_UI, 0, 0 },
    [SFX_OBJ_DESELECT] = { "rom:/audio/sfx/obj_deselect.wav64", SND_BUS_SFX, 0.60f, PRIO_UI, 0, 0 },
    [SFX_MODE_CHANGE]  = { "rom:/audio/sfx/mode_change.wav64",  SND_BUS_SFX, 0.70f, PRIO_UI, 0, 0 },

    // World: the physics ball's bounces, heard from where they happen
    [SFX_COLLISION]    = { "rom:/audio/sfx/collision.wav64",    SND_BUS_SFX, 0.85f, PRIO_WORLD, 150.0f, 1200.0f },

    // Music (placeholder track)
    [BGM_DEMO]         = { "rom:/audio/music/demo.wav64",       SND_BUS_MUSIC, 0.65f, 0, 0, 0 },

    // Benchmark-only encodings of the same track
    [BGM_BENCH_RAW]    = { "rom:/audio/bench/demo_raw.wav64",   SND_BUS_MUSIC, 0.65f, 0, 0, 0 },
    [BGM_BENCH_OPUS]   = { "rom:/audio/bench/demo_opus.wav64",  SND_BUS_MUSIC, 0.65f, 0, 0, 0 },
};
