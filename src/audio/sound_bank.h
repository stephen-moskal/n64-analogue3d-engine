#ifndef SOUND_BANK_H
#define SOUND_BANK_H

// ============================================================
// Sound definitions
//
// Every sound the engine can play: an id here and a row in sound_bank.c.
// Game code only uses SoundId values, never file paths. To add a sound, add
// an id, a row, and the WAV under assets/audio/sfx/ or assets/audio/music/
// (docs/AUDIO.md, docs/EXTENDING.md "Add a sound").
// ============================================================

typedef enum {
    SOUND_NONE = 0,

    // Menu SFX
    SFX_MENU_OPEN,
    SFX_MENU_CLOSE,
    SFX_MENU_NAV,
    SFX_MENU_SELECT,

    // Object interaction SFX
    SFX_OBJ_SELECT,
    SFX_OBJ_DESELECT,
    SFX_MODE_CHANGE,

    // World SFX (positional)
    SFX_COLLISION,

    // Music
    BGM_DEMO,

    // Benchmark only: the demo track in other encodings (Bench = Audio)
    BGM_BENCH_RAW,
    BGM_BENCH_OPUS,

    SOUND_COUNT
} SoundId;

// Mix bus a sound plays on (its volume is the bus volume times the master)
typedef enum {
    SND_BUS_SFX,
    SND_BUS_MUSIC,
} SndBus;

typedef struct {
    const char *path;   // DFS path, e.g. "rom:/audio/sfx/menu_open.wav64"
    SndBus bus;
    float volume;       // 0..1, multiplied with the bus and master volumes
    int priority;       // SFX: when every voice is busy, a sound replaces the
                        // oldest voice of equal or lower priority (else it is dropped)
    float min_dist;     // positional SFX (snd_play_at): full volume within
    float max_dist;     //   min_dist, silent beyond max_dist (world units)
} SoundDef;

extern const SoundDef sound_bank[SOUND_COUNT];

#endif
