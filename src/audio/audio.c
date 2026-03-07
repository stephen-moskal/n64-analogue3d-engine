#include "audio.h"
#include <libdragon.h>
#include <string.h>

// --- Configuration ---

#define AUDIO_FREQ       22050   // Sample rate (matches placeholder WAVs)
#define AUDIO_BUFFERS    4       // Audio DMA buffers
#define MAX_CHANNELS     16      // Total mixer channels
#define SFX_CH_START     2       // First SFX channel
#define SFX_CH_COUNT     6       // Number of SFX channels (2-7)
#define BGM_CH           0       // BGM channel

// --- State ---

// Pre-loaded SFX waveforms (opened at init, played on demand)
static wav64_t sfx_waves[SOUND_COUNT];
static bool sfx_loaded[SOUND_COUNT];

// BGM state
static wav64_t bgm_wave;
static bool bgm_loaded = false;
static bool bgm_playing = false;
static SoundId current_bgm = SOUND_NONE;

// Volume (0-128 range, mapped to 0.0-1.0 for mixer)
static int sfx_volume = 100;
static int bgm_volume = 80;

// Round-robin SFX channel allocation
static int next_sfx_ch = SFX_CH_START;

static bool snd_ready = false;

// --- Helpers ---

static float vol_to_float(int vol) {
    if (vol <= 0) return 0.0f;
    if (vol >= 128) return 1.0f;
    return vol / 128.0f;
}

// --- Public API ---

void snd_init(void) {
    audio_init(AUDIO_FREQ, AUDIO_BUFFERS);
    mixer_init(MAX_CHANNELS);

    memset(sfx_loaded, 0, sizeof(sfx_loaded));

    // Pre-load all SFX waveforms from DFS
    for (int i = 0; i < SOUND_COUNT; i++) {
        const SoundDef *def = &sound_bank[i];
        if (!def->path) continue;
        if (def->type != SOUND_TYPE_SFX) continue;

        wav64_open(&sfx_waves[i], def->path);
        sfx_loaded[i] = true;
    }

    snd_ready = true;
    debugf("Audio initialized: %d Hz, %d channels\n", AUDIO_FREQ, MAX_CHANNELS);
}

void snd_cleanup(void) {
    if (!snd_ready) return;

    snd_stop_bgm();

    // Close all loaded SFX
    for (int i = 0; i < SOUND_COUNT; i++) {
        if (sfx_loaded[i]) {
            wav64_close(&sfx_waves[i]);
            sfx_loaded[i] = false;
        }
    }

    mixer_close();
    audio_close();
    snd_ready = false;
}

void snd_update(void) {
    if (!snd_ready) return;

    // Feed audio buffers to DAC
    if (audio_can_write()) {
        short *buf = audio_write_begin();
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }
}

void snd_play_sfx(SoundId id) {
    if (!snd_ready) return;
    if (id <= SOUND_NONE || id >= SOUND_COUNT) return;
    if (!sfx_loaded[id]) return;

    // Round-robin channel allocation
    int ch = next_sfx_ch;
    next_sfx_ch = SFX_CH_START + ((next_sfx_ch - SFX_CH_START + 1) % SFX_CH_COUNT);

    // Set volume from sound bank definition, scaled by global SFX volume
    const SoundDef *def = &sound_bank[id];
    float vol = vol_to_float(def->volume) * vol_to_float(sfx_volume);

    mixer_ch_set_vol(ch, vol, vol);
    wav64_play(&sfx_waves[id], ch);
}

void snd_play_bgm(SoundId id) {
    if (!snd_ready) return;
    if (id <= SOUND_NONE || id >= SOUND_COUNT) return;

    const SoundDef *def = &sound_bank[id];
    if (!def->path) return;

    // Stop current BGM if playing
    snd_stop_bgm();

    // Open and play new BGM
    wav64_open(&bgm_wave, def->path);
    wav64_set_loop(&bgm_wave, true);
    bgm_loaded = true;

    float vol = vol_to_float(def->volume) * vol_to_float(bgm_volume);
    mixer_ch_set_vol(BGM_CH, vol, vol);
    wav64_play(&bgm_wave, BGM_CH);
    bgm_playing = true;
    current_bgm = id;
}

void snd_stop_bgm(void) {
    if (!snd_ready) return;

    if (bgm_playing) {
        mixer_ch_stop(BGM_CH);
        bgm_playing = false;
    }

    if (bgm_loaded) {
        wav64_close(&bgm_wave);
        bgm_loaded = false;
    }

    current_bgm = SOUND_NONE;
}

void snd_set_sfx_volume(int vol) {
    sfx_volume = vol;
}

void snd_set_bgm_volume(int vol) {
    bgm_volume = vol;

    // Update playing BGM volume immediately
    if (snd_ready && bgm_playing && current_bgm != SOUND_NONE) {
        const SoundDef *def = &sound_bank[current_bgm];
        float v = vol_to_float(def->volume) * vol_to_float(bgm_volume);
        mixer_ch_set_vol(BGM_CH, v, v);
    }
}
