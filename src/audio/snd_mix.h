#ifndef SND_MIX_H
#define SND_MIX_H

// Pure helpers of the sound module: voice allocation and positional gains.
// No libdragon calls, so they run in the host unit tests (test_audio.c).

#include <stdbool.h>
#include <stdint.h>
#include "../math/vec3.h"

// One sound-effect voice (a mixer channel) as the allocator sees it
typedef struct {
    bool     active;       // playing
    int      priority;     // of the sound playing on it
    uint32_t serial;       // start order: lower = started earlier
} SndVoiceSlot;

// Choose the voice for a new sound of priority `priority`: a free voice if
// there is one, otherwise the lowest-priority active voice whose priority is
// not above the new sound's (the oldest of those). Returns -1 when every
// voice plays something more important: the new sound is dropped.
int snd_voice_choose(const SndVoiceSlot *slots, int count, int priority);

// Stereo gains of a sound at `src` heard by a listener at `listener` whose
// right-hand direction is `right` (unit length). Distance attenuation: full
// volume within `min_dist`, falling linearly to silence at `max_dist`.
// Panning: equal-power law (centre = 0.707 per side, hard left/right = 1/0),
// driven by how far the source lies to the listener's right or left.
void snd_spatial_gains(vec3_t listener, vec3_t right, vec3_t src,
                       float min_dist, float max_dist, float *left, float *right_gain);

#endif
