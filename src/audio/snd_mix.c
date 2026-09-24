#include "snd_mix.h"
#include <math.h>

int snd_voice_choose(const SndVoiceSlot *slots, int count, int priority) {
    for (int i = 0; i < count; i++) {
        if (!slots[i].active) return i;
    }
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (slots[i].priority > priority) continue;          // more important: keep
        if (best < 0 ||
            slots[i].priority < slots[best].priority ||
            (slots[i].priority == slots[best].priority && slots[i].serial < slots[best].serial)) {
            best = i;
        }
    }
    return best;
}

void snd_spatial_gains(vec3_t listener, vec3_t right, vec3_t src,
                       float min_dist, float max_dist, float *left, float *right_gain) {
    float dx = src.x - listener.x, dy = src.y - listener.y, dz = src.z - listener.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    float gain = 1.0f;
    if (dist > min_dist) {
        gain = (max_dist > min_dist) ? (max_dist - dist) / (max_dist - min_dist) : 0.0f;
        if (gain < 0.0f) gain = 0.0f;
    }

    // Side: -1 hard left .. +1 hard right (0 when the source is on the listener)
    float side = 0.0f;
    if (dist > 1e-3f) {
        side = (dx * right.x + dy * right.y + dz * right.z) / dist;
        if (side < -1.0f) side = -1.0f;
        if (side > 1.0f) side = 1.0f;
    }
    // Equal-power pan: angle 0 = left, pi/2 = right
    float angle = (side + 1.0f) * 0.25f * 3.14159265f;
    *left = gain * cosf(angle);
    *right_gain = gain * sinf(angle);
}
