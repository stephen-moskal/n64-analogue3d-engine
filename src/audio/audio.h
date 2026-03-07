#ifndef AUDIO_H
#define AUDIO_H

#include "sound_bank.h"

// Initialize the audio system (call once after dfs_init)
void snd_init(void);

// Shut down audio and free resources
void snd_cleanup(void);

// Call every frame to feed the audio mixer
void snd_update(void);

// Play a one-shot sound effect
void snd_play_sfx(SoundId id);

// Start looping background music (stops any current BGM)
void snd_play_bgm(SoundId id);

// Stop current background music
void snd_stop_bgm(void);

// Volume control (0-128)
void snd_set_sfx_volume(int vol);
void snd_set_bgm_volume(int vol);

#endif
