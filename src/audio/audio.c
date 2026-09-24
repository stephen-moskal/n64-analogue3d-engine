#include "audio.h"
#include "snd_mix.h"
#include "../debug/stats.h"
#include <libdragon.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// --- Configuration ---

#ifndef SND_ENABLE_OPUS
#define SND_ENABLE_OPUS   0       // Makefile: SND_OPUS=1 links the Opus decoder
#endif

#define AUDIO_FREQ        22050   // sample rate of the assets
#define AUDIO_BUFFERS     4       // audio DMA buffers (slack before an underrun)
#define MUSIC_SLOTS       2       // current track + the one fading out
#define SND_VOICES        8       // sound-effect voices
// Channel map: each music slot owns a channel pair (a stereo track plays on
// ch and ch+1); SFX voices follow, one channel each (SFX must be mono).
#define MUSIC_CH(slot)    ((slot) * 2)
#define SFX_CH0           (MUSIC_SLOTS * 2)
#define MIXER_CHANNELS    (SFX_CH0 + SND_VOICES)

#define VOL_EPS           1e-4f
#define MIN_RAMP_S        0.05f   // even "immediate" volume changes ramp over 50 ms: no clicks

// --- Volume ramps (stepped once per snd_update) ---

typedef struct {
    float cur, target, rate;      // rate in volume units per second
} Ramp;

static void ramp_set(Ramp *r, float target, float seconds) {
    if (seconds < MIN_RAMP_S) seconds = MIN_RAMP_S;
    r->target = target;
    r->rate = fabsf(target - r->cur) / seconds;
}

static bool ramp_step(Ramp *r, float dt) {
    if (r->cur == r->target) return false;
    float step = r->rate * dt;
    if (fabsf(r->target - r->cur) <= step) r->cur = r->target;
    else r->cur += (r->target > r->cur) ? step : -step;
    return true;
}

// --- State ---

typedef struct {
    wav64_t  wav;
    bool     open;         // file opened
    bool     playing;      // on its mixer channel(s)
    bool     closing;      // fading out, released at silence
    SoundId  id;
    SoundId  ring;         // track the channel's sample ring was last set up for
    Ramp     fade;         // track fade 0..1
    float    applied;      // last volume sent to the mixer
} MusicSlot;

typedef struct {
    SoundId id;
    float   l, r;          // gains before the bus and master volumes
} VoiceInfo;

static bool         snd_ready;
static Ramp         vol[SND_VOL_COUNT];
static MusicSlot    music[MUSIC_SLOTS];
static int          music_active = -1;          // slot of the current track
static wav64_t      sfx_wave[SOUND_COUNT];
static bool         sfx_open[SOUND_COUNT];
static SndVoiceSlot voice_slot[SND_VOICES];
static VoiceInfo    voice_info[SND_VOICES];
static uint32_t     voice_serial;
static vec3_t       listener_pos, listener_right = {1.0f, 0.0f, 0.0f};
static SndPollPoint poll_point = SND_POLL_AFTER_DISPLAY;
static SndStats     st;
static float        ch_limit_hz[MIXER_CHANNELS];  // frequency limit set per channel (0 = libdragon default)

// --- Helpers ---

static float sfx_bus(void)   { return vol[SND_VOL_MASTER].cur * vol[SND_VOL_SFX].cur; }
static float music_bus(void) { return vol[SND_VOL_MASTER].cur * vol[SND_VOL_MUSIC].cur; }

static void apply_voice_volume(int v) {
    float b = sfx_bus();
    mixer_ch_set_vol(SFX_CH0 + v, voice_info[v].l * b, voice_info[v].r * b);
}

// Give channels a new sample ring. libdragon 39d0d6096 asserts "samplebuffer
// too small" when a channel reuses its ring for a waveform of another
// encoding: after a block codec (Opus, ULC), mixer_ch_play resizes the ring
// for the new sample unit with the old codec's append margin. Changing a
// channel's limits frees its ring (mixer_ch_set_limits), so the limit is
// bumped and restored and the next waveform allocates a ring of its own.
// Music slots do this whenever a different track starts on them (the codec
// is not visible through libdragon's stable API); sound effects share one
// encoding (the Makefile converts the whole directory alike) and keep theirs.
static void fresh_ring(int ch, int nch) {
    for (int c = ch; c < ch + nch; c++) {
        if (mixer_ch_playing(c)) continue;
        mixer_ch_set_limits(c, 0, ch_limit_hz[c] + 1.0f, 0);   // frees the ring
        mixer_ch_set_limits(c, 0, ch_limit_hz[c], 0);
    }
}

static void music_close(int s) {
    MusicSlot *m = &music[s];
    if (m->playing) mixer_ch_stop(MUSIC_CH(s));
    if (m->open) wav64_close(&m->wav);
    m->open = m->playing = m->closing = false;
    if (music_active == s) music_active = -1;
}

// Fade the track. Once it settles at silence (muted) it stops, so nothing is
// decoded; heard again, it restarts from the top. Compressed audio can only
// seek to skip points (none unless audioconv64 --wav-seek adds them), so a
// resume in the middle is not possible in general.
static void music_update(int s, float dt) {
    MusicSlot *m = &music[s];
    if (!m->open) return;
    ramp_step(&m->fade, dt);

    if (m->closing && m->fade.cur <= VOL_EPS) {
        music_close(s);
        return;
    }

    float def_vol = sound_bank[m->id].volume;
    float eff = music_bus() * def_vol * m->fade.cur;
    float eff_target = vol[SND_VOL_MASTER].target * vol[SND_VOL_MUSIC].target *
                       def_vol * m->fade.target;
    int ch = MUSIC_CH(s);

    if (m->playing && eff <= VOL_EPS && eff_target <= VOL_EPS) {
        mixer_ch_stop(ch);
        m->playing = false;
        return;
    }
    if (!m->playing && eff_target > VOL_EPS) {     // not while fading out
        if (m->ring != SOUND_NONE && m->ring != m->id) fresh_ring(ch, 2);
        m->ring = m->id;
        wav64_play(&m->wav, ch);                   // from the start
        m->playing = true;
        m->applied = -1.0f;                       // force the volume below
    }
    if (m->playing && eff != m->applied) {
        mixer_ch_set_vol(ch, eff, eff);
        m->applied = eff;
    }
}

static int play_voice(SoundId id, float l, float r) {
    const SoundDef *def = &sound_bank[id];
    int v = snd_voice_choose(voice_slot, SND_VOICES, def->priority);
    if (v < 0) { st.dropped++; return -1; }
    int ch = SFX_CH0 + v;
    if (voice_slot[v].active) { st.stolen++; mixer_ch_stop(ch); }

    voice_slot[v] = (SndVoiceSlot){ .active = true, .priority = def->priority, .serial = ++voice_serial };
    voice_info[v] = (VoiceInfo){ .id = id, .l = l * def->volume, .r = r * def->volume };
    wav64_play(&sfx_wave[id], ch);
    apply_voice_volume(v);
    st.played++;
    return v;
}

static bool valid(SoundId id, SndBus bus) {
    return snd_ready && id > SOUND_NONE && id < SOUND_COUNT &&
           sound_bank[id].path && sound_bank[id].bus == bus;
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// A mixer channel plays waveforms up to its frequency limit, by default the
// output rate; a faster one asserts in libdragon's mixer (release builds do
// not check). Opus is always 48 kHz, and a WAV converted without
// --wav-resample keeps its own rate. snd_init() raises each channel group's
// limit to its fastest sound, so the sample buffers are sized once and
// nothing is reallocated during play.
static void raise_rate_limit(int ch0, int count, float max_hz) {
    if (max_hz <= audio_get_frequency() * 1.01f) return;         // the default covers it
    for (int ch = ch0; ch < ch0 + count; ch++) {
        mixer_ch_set_limits(ch, 0, max_hz, 0);
        ch_limit_hz[ch] = max_hz;
    }
}

// --- Lifecycle ---

void snd_init(void) {
    audio_init(AUDIO_FREQ, AUDIO_BUFFERS);
    mixer_init(MIXER_CHANNELS);
#if SND_ENABLE_OPUS
    wav64_init_compression(3);        // Opus files (level 3): ~94 KB more RAM (SND_OPUS=1)
#endif

    memset(music, 0, sizeof(music));
    memset(voice_slot, 0, sizeof(voice_slot));
    memset(&st, 0, sizeof(st));
    for (int ch = 0; ch < MIXER_CHANNELS; ch++) ch_limit_hz[ch] = 0.0f;
    st.voices_max = SND_VOICES;
    music_active = -1;
    for (int i = 0; i < SND_VOL_COUNT; i++) vol[i] = (Ramp){ 1.0f, 1.0f, 0.0f };

    // Sound effects stay open (their headers and sample buffers are reused).
    // A missing file is a build error: debug builds stop here, release
    // builds play silence for that sound.
    for (int i = 0; i < SOUND_COUNT; i++) {
        sfx_open[i] = false;
        if (!sound_bank[i].path || sound_bank[i].bus != SND_BUS_SFX) continue;
        bool found = file_exists(sound_bank[i].path);
        assertf(found, "%s: sound file not in the ROM", sound_bank[i].path);
        if (!found) continue;
        wav64_open(&sfx_wave[i], sound_bank[i].path);
        assertf(sfx_wave[i].wave.channels == 1, "%s: sound effects must be mono", sound_bank[i].path);
        sfx_open[i] = true;
    }

    // Channel rate limits (raise_rate_limit). Music files are opened briefly
    // to read their rate, which also checks every track at boot rather than
    // when it first plays.
    float sfx_hz = 0.0f, music_hz = 0.0f;
    for (int i = 0; i < SOUND_COUNT; i++) {
        if (sfx_open[i] && sfx_wave[i].wave.frequency > sfx_hz) sfx_hz = sfx_wave[i].wave.frequency;
        if (!sound_bank[i].path || sound_bank[i].bus != SND_BUS_MUSIC) continue;
        if (!file_exists(sound_bank[i].path)) continue;   // benchmark tracks: debug ROMs only
        wav64_t probe;
        wav64_open(&probe, sound_bank[i].path);
        if (probe.wave.frequency > music_hz) music_hz = probe.wave.frequency;
        wav64_close(&probe);
    }
    raise_rate_limit(0, SFX_CH0, music_hz);
    raise_rate_limit(SFX_CH0, SND_VOICES, sfx_hz);

    snd_ready = true;
    debugf("Audio initialized: %d Hz, %d channels (%d SFX voices); fastest music %.0f Hz, SFX %.0f Hz\n",
           AUDIO_FREQ, MIXER_CHANNELS, SND_VOICES, music_hz, sfx_hz);
}

void snd_cleanup(void) {
    if (!snd_ready) return;
    snd_stop_all_sfx();
    for (int s = 0; s < MUSIC_SLOTS; s++) music_close(s);
    for (int i = 0; i < SOUND_COUNT; i++) {
        if (sfx_open[i]) wav64_close(&sfx_wave[i]);
        sfx_open[i] = false;
    }
    mixer_close();
    audio_close();
    snd_ready = false;
}

// --- Frame update ---

void snd_update(float dt) {
    if (!snd_ready) return;

    bool vol_changed = false;
    for (int i = 0; i < SND_VOL_COUNT; i++) vol_changed |= ramp_step(&vol[i], dt);

    for (int s = 0; s < MUSIC_SLOTS; s++) music_update(s, dt);

    int active = 0;
    for (int v = 0; v < SND_VOICES; v++) {
        if (!voice_slot[v].active) continue;
        if (!mixer_ch_playing(SFX_CH0 + v)) { voice_slot[v].active = false; continue; }
        active++;
        if (vol_changed) apply_voice_volume(v);
    }

    // Fill every free buffer: after a long frame several are free, and
    // leaving them empty would be an audible gap
    int filled = 0;
    while (audio_can_write()) {
        short *buf = audio_write_begin();
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
        filled++;
    }

    st.voices_active = active;
    st.buffers_filled = filled;
    STATS_SET(snd_voices, active);
    STATS_SET(snd_buffers, filled);
    st.music_paused = music_active >= 0 && !music[music_active].playing;
}

void snd_set_poll_point(SndPollPoint point) {
    if (point >= 0 && point < SND_POLL_COUNT) poll_point = point;
}

SndPollPoint snd_get_poll_point(void) { return poll_point; }

// --- Sound effects ---

bool snd_available(SoundId id) {
    return id > SOUND_NONE && id < SOUND_COUNT && sound_bank[id].path &&
           file_exists(sound_bank[id].path);
}

int snd_play(SoundId id) {
    if (!valid(id, SND_BUS_SFX) || !sfx_open[id]) return -1;
    return play_voice(id, 1.0f, 1.0f);
}

int snd_play_at(SoundId id, vec3_t pos, float gain) {
    if (!valid(id, SND_BUS_SFX) || !sfx_open[id]) return -1;
    const SoundDef *def = &sound_bank[id];
    if (def->max_dist <= 0.0f) return play_voice(id, gain, gain);   // not positional

    float l, r;
    snd_spatial_gains(listener_pos, listener_right, pos, def->min_dist, def->max_dist, &l, &r);
    if (l * gain <= VOL_EPS && r * gain <= VOL_EPS) return -1;     // out of range: keep the voice
    return play_voice(id, l * gain, r * gain);
}

void snd_stop(int voice) {
    if (!snd_ready || voice < 0 || voice >= SND_VOICES) return;
    if (voice_slot[voice].active) mixer_ch_stop(SFX_CH0 + voice);
    voice_slot[voice].active = false;
}

void snd_stop_all_sfx(void) {
    for (int v = 0; v < SND_VOICES; v++) snd_stop(v);
}

void snd_set_listener(vec3_t position, vec3_t right) {
    listener_pos = position;
    listener_right = right;
}

// --- Music ---

void snd_music_play(SoundId id, float fade_s) {
    if (!valid(id, SND_BUS_MUSIC)) return;
    if (music_active >= 0 && music[music_active].id == id) return;   // already the current track
    bool found = snd_available(id);               // a file lookup, once per track change
    assertf(found, "%s: music file not in the ROM", sound_bank[id].path);
    if (!found) return;

    if (music_active >= 0) {                  // fade the current track out
        music[music_active].closing = true;
        ramp_set(&music[music_active].fade, 0.0f, fade_s);
    }
    // A free slot; if both are busy (a crossfade still running), reuse the
    // one that is not the track just told to fade out
    int s = -1;
    for (int i = 0; i < MUSIC_SLOTS && s < 0; i++) if (!music[i].open) s = i;
    for (int i = 0; i < MUSIC_SLOTS && s < 0; i++) if (i != music_active) s = i;
    if (music[s].open) music_close(s);

    MusicSlot *m = &music[s];
    wav64_open(&m->wav, sound_bank[id].path);
    wav64_set_loop(&m->wav, true);
    m->open = true;
    m->playing = m->closing = false;
    m->id = id;
    m->fade = (Ramp){ fade_s > 0.0f ? 0.0f : 1.0f, 1.0f, 0.0f };
    if (fade_s > 0.0f) ramp_set(&m->fade, 1.0f, fade_s);
    music_active = s;
    music_update(s, 0.0f);                    // starts it unless everything is muted
}

void snd_music_stop(float fade_s) {
    if (!snd_ready || music_active < 0) return;
    int s = music_active;
    music_active = -1;
    if (fade_s <= 0.0f) { music_close(s); return; }
    music[s].closing = true;
    ramp_set(&music[s].fade, 0.0f, fade_s);
}

SoundId snd_music_current(void) {
    return music_active >= 0 ? music[music_active].id : SOUND_NONE;
}

// --- Volumes and stats ---

void snd_set_volume(SndVolume which, float v, float fade_s) {
    if (which < 0 || which >= SND_VOL_COUNT) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    ramp_set(&vol[which], v, fade_s);
}

float snd_get_volume(SndVolume which) {
    return (which >= 0 && which < SND_VOL_COUNT) ? vol[which].target : 0.0f;
}

const SndStats *snd_stats(void) { return &st; }
