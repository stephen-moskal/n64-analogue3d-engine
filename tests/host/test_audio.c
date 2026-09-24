#include "test.h"
#include "audio/snd_mix.h"
#include <math.h>

// Sound module helpers (snd_mix.c): voice stealing and positional gains.

static void test_voice_choose(void) {
    SndVoiceSlot v[4] = {0};
    CHECK(snd_voice_choose(v, 4, 10) == 0);                // all free: the first

    v[0] = (SndVoiceSlot){ true, 10, 1 };
    v[1] = (SndVoiceSlot){ true, 20, 2 };
    CHECK(snd_voice_choose(v, 4, 10) == 2);                // a free voice beats stealing

    v[2] = (SndVoiceSlot){ true, 10, 3 };
    v[3] = (SndVoiceSlot){ true, 5,  4 };
    CHECK(snd_voice_choose(v, 4, 10) == 3);                // all busy: lowest priority first
    v[3].priority = 10;
    CHECK(snd_voice_choose(v, 4, 10) == 0);                // equal priority: the oldest (serial 1)
    CHECK(snd_voice_choose(v, 4, 20) == 0);                // higher priority steals the oldest low one
    CHECK(snd_voice_choose(v, 4, 1) == -1);                // everything is more important: drop

    SndVoiceSlot ui[2] = { { true, 20, 1 }, { true, 20, 2 } };
    CHECK(snd_voice_choose(ui, 2, 10) == -1);              // world sounds never cut UI sounds
}

static void test_spatial_gains(void) {
    vec3_t listener = {0, 0, 0}, right = {1, 0, 0};
    float l, r;

    // Straight ahead, inside min_dist: full volume, centred (equal power)
    snd_spatial_gains(listener, right, (vec3_t){0, 0, 50}, 100, 1000, &l, &r);
    CHECK_NEAR(l, 0.7071f, 1e-3);
    CHECK_NEAR(r, 0.7071f, 1e-3);
    CHECK_NEAR(l * l + r * r, 1.0f, 1e-3);                 // constant power

    // Hard right and hard left
    snd_spatial_gains(listener, right, (vec3_t){80, 0, 0}, 100, 1000, &l, &r);
    CHECK_NEAR(l, 0.0f, 1e-3);
    CHECK_NEAR(r, 1.0f, 1e-3);
    snd_spatial_gains(listener, right, (vec3_t){-80, 0, 0}, 100, 1000, &l, &r);
    CHECK_NEAR(l, 1.0f, 1e-3);
    CHECK_NEAR(r, 0.0f, 1e-3);

    // Distance: half way between min and max is half volume; beyond max silent
    snd_spatial_gains(listener, right, (vec3_t){0, 0, 550}, 100, 1000, &l, &r);
    CHECK_NEAR(sqrtf(l * l + r * r), 0.5f, 1e-3);
    snd_spatial_gains(listener, right, (vec3_t){0, 0, 1500}, 100, 1000, &l, &r);
    CHECK(l == 0.0f && r == 0.0f);

    // On the listener: centred, full volume
    snd_spatial_gains(listener, right, listener, 100, 1000, &l, &r);
    CHECK_NEAR(l, r, 1e-6);
    CHECK_NEAR(l * l + r * r, 1.0f, 1e-3);
}

void run_audio_tests(void) {
    RUN_TEST(test_voice_choose);
    RUN_TEST(test_spatial_gains);
}
