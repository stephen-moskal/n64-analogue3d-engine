#ifndef SOUND_BANK_H
#define SOUND_BANK_H

// ============================================================
// Sound Event Definitions
//
// This file defines all sound events in the engine. To add a
// new sound, add an enum entry here and a path entry in
// sound_bank.c. Then drop the audio file in assets/audio/.
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

    // Collision SFX
    SFX_COLLISION,

    // Background music
    BGM_DEMO,

    SOUND_COUNT
} SoundId;

typedef enum {
    SOUND_TYPE_SFX,
    SOUND_TYPE_BGM,
} SoundType;

typedef struct {
    const char *path;   // DFS path (e.g., "rom:/audio/sfx/menu_open.wav64")
    SoundType type;
    int volume;         // 0-128
} SoundDef;

extern const SoundDef sound_bank[SOUND_COUNT];

#endif
