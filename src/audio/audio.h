#ifndef AUDIO_H
#define AUDIO_H

// Sound module (snd_*): music with crossfades, sound-effect voices with
// priority-based stealing, positional sound, and master/music/SFX volumes.
// Built on libdragon's stable mixer API. See docs/AUDIO.md.

#include <stdbool.h>
#include "sound_bank.h"
#include "../math/vec3.h"

// Volume controls, each 0..1 (the effective volume multiplies master and bus)
typedef enum {
    SND_VOL_MASTER,
    SND_VOL_MUSIC,
    SND_VOL_SFX,
    SND_VOL_COUNT
} SndVolume;

// Where in the frame the main loop fills the mixer's audio buffers. The
// mixer runs as a high-priority RSP job and the CPU waits for it, so the
// point matters: right after rdpq_detach_show() the RSP has just received
// the whole frame's commands. Selected by snd_set_poll_point() (the audio
// benchmark measures each); main.c checks it at all three points.
typedef enum {
    SND_POLL_AFTER_PRESENT,    // after rdpq_detach_show() (the pre-S4b placement)
    SND_POLL_BEFORE_DISPLAY,   // after the update, before display_get()
    SND_POLL_AFTER_DISPLAY,    // after display_get(), before the frame is drawn
    SND_POLL_COUNT
} SndPollPoint;

typedef struct {
    int  voices_active;        // SFX voices playing
    int  voices_max;
    int  played, stolen, dropped;   // SFX since init
    int  buffers_filled;       // audio buffers mixed in the last poll
    bool music_paused;         // music silent, its decoding stopped
} SndStats;

void snd_init(void);
void snd_cleanup(void);

// Advance fades and volume changes, then fill every free audio buffer. Call
// once per frame, at the point snd_get_poll_point() names (main.c does).
void snd_update(float dt);
void snd_set_poll_point(SndPollPoint point);
SndPollPoint snd_get_poll_point(void);

// True if the sound's file is in this ROM (the benchmark-only tracks are
// packed into debug ROMs only). Looks the file up: not for every frame.
bool snd_available(SoundId id);

// Sound effects. Return the voice used, or -1 if the sound was dropped
// (every voice busy with more important sounds) or is silent.
int  snd_play(SoundId id);
// Positional: attenuated with distance (sound_bank min/max_dist) and panned
// relative to the listener set by snd_set_listener(); gain scales it (0..1).
int  snd_play_at(SoundId id, vec3_t pos, float gain);
void snd_stop(int voice);
void snd_stop_all_sfx(void);
void snd_set_listener(vec3_t position, vec3_t right);

// Music: crossfades from the current track over fade_s seconds (0 = cut).
// Muted music (volume 0) stops decoding and restarts from the top when heard again.
void    snd_music_play(SoundId id, float fade_s);
void    snd_music_stop(float fade_s);
SoundId snd_music_current(void);

// Volumes ramp to the new value over fade_s seconds (0 = immediately)
void  snd_set_volume(SndVolume which, float vol, float fade_s);
float snd_get_volume(SndVolume which);

const SndStats *snd_stats(void);

#endif
