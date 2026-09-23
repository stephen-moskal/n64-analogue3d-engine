# Audio

A thin `snd_*` wrapper in `src/audio/` over libdragon's audio, mixer and wav64 modules: one looping background track (BGM) and six round-robin sound-effect (SFX) voices. Game code names sounds by `SoundId`; the file paths live in one data table, the sound bank. Adding a sound is a recipe in [EXTENDING.md](EXTENDING.md#add-a-sound).

## Mixer and channels

`snd_init()` runs once in `main.c`, after `dfs_init()` (the sounds are DFS files):

```c
audio_init(AUDIO_FREQ, AUDIO_BUFFERS);   // 22,050 Hz output, 4 DMA buffers
mixer_init(MAX_CHANNELS);                // 16 mixer channels
```

It then opens every `SOUND_TYPE_SFX` entry of the sound bank with `wav64_open()`. Opening parses the header; libdragon streams the samples from ROM during playback (`wav64.h`). BGM files are opened when they start and closed when they stop.

| Mixer channel | Use |
|---|---|
| 0 | BGM, looping |
| 1 | unused (a stereo BGM would occupy channels 0 and 1) |
| 2–7 | SFX, round-robin |
| 8–15 | unused |

- libdragon's mixer plays a stereo waveform on two consecutive channels, `ch` and `ch + 1` (`mixer.h`). All placeholder sounds are mono; keep effects mono, since a stereo effect would also take the next round-robin channel.
- Each `snd_play_sfx()` takes the next channel from 2 to 7 in turn. Starting a sound on a busy channel cuts off what was playing there, so a seventh overlapping effect replaces the oldest one. There are no priorities.

## API (`audio.h`)

| Function | Behaviour |
|---|---|
| `snd_init()` | set up audio and the mixer, open every SFX; once at boot |
| `snd_update()` | feed the mixer; once per frame (below) |
| `snd_play_sfx(id)` | one-shot on the next SFX channel; ignored for `SOUND_NONE`, out-of-range ids and ids that aren't SFX |
| `snd_play_bgm(id)` | stop the current BGM, open `id`, loop it on channel 0; no type check, so an SFX id would loop too |
| `snd_stop_bgm()` | stop and close the BGM |
| `snd_set_sfx_volume(vol)` | 0–128; applies to effects started afterwards |
| `snd_set_bgm_volume(vol)` | 0–128; changes the playing BGM at once |
| `snd_cleanup()` | stop and close everything, shut down the mixer and audio; not called today (the main loop never exits) |

A channel's volume is the sound bank's per-sound volume times the global SFX or BGM volume, each scaled to 0–1 (`vol / 128`, clamped), equal on left and right. Both global volumes start at 0. From `snd_play_sfx()`:

```c
int ch = next_sfx_ch;
next_sfx_ch = SFX_CH_START + ((next_sfx_ch - SFX_CH_START + 1) % SFX_CH_COUNT);

const SoundDef *def = &sound_bank[id];
float vol = vol_to_float(def->volume) * vol_to_float(sfx_volume);

mixer_ch_set_vol(ch, vol, vol);
wav64_play(&sfx_waves[id], ch);
```

## Per-frame update

`main.c` calls `snd_update()` once per loop iteration, after `rdpq_detach_show()`, inside the `audio` profiler slot (`PROF_AUDIO`):

```c
if (audio_can_write()) {
    short *buf = audio_write_begin();
    mixer_poll(buf, audio_get_buffer_length());
    audio_write_end();
}
```

It mixes at most one buffer per call. libdragon plays 25 buffers per second (`BUFFERS_PER_SECOND` in `libdragon/src/audio.c`), so the loop has to call `snd_update()` at least 25 times per second or the four buffers run dry. That follows from the code; it hasn't been measured on the console. The mixing itself runs on the RSP ([HARDWARE.md](HARDWARE.md)); the `audio` slot times the whole `snd_update()` call on the CPU.

## The sound bank

`sound_bank.h` declares the `SoundId` enum and `sound_bank.c` maps each id to a DFS path, a type and a volume (0–128):

```c
const SoundDef sound_bank[SOUND_COUNT] = {
    [SOUND_NONE]       = { NULL,                                  SOUND_TYPE_SFX, 0 },

    // Menu sounds
    [SFX_MENU_OPEN]    = { "rom:/audio/sfx/menu_open.wav64",     SOUND_TYPE_SFX, 100 },
    ...
    // Music (WAV placeholder — replace with XM module for production)
    [BGM_DEMO]         = { "rom:/audio/music/demo.wav64",        SOUND_TYPE_BGM, 80 },
};
```

The type decides the handling: `SOUND_TYPE_SFX` entries are opened at boot and played on the SFX channels, `SOUND_TYPE_BGM` entries are opened on demand and loop on channel 0. The comment about XM is a plan: nothing in `snd_*` can play an XM module yet.

| Id | File (`assets/audio/…`) | Volume | Played by the demo when (default bindings) |
|---|---|---|---|
| `SFX_MENU_OPEN` | `sfx/menu_open.wav` | 100 | Start opens the menu |
| `SFX_MENU_CLOSE` | `sfx/menu_close.wav` | 100 | Start closes the menu |
| `SFX_MENU_NAV` | `sfx/menu_nav.wav` | 80 | the menu cursor or tab moves; cycling objects in select mode |
| `SFX_MENU_SELECT` | `sfx/menu_select.wav` | 100 | the menu closes with A or B |
| `SFX_OBJ_SELECT` | `sfx/obj_select.wav` | 90 | Z enters object mode |
| `SFX_OBJ_DESELECT` | `sfx/obj_deselect.wav` | 80 | Z leaves object mode; B steps back from transform to select |
| `SFX_MODE_CHANGE` | `sfx/mode_change.wav` | 90 | B spawns or relaunches the ball; A enters or cycles transform mode |
| `SFX_COLLISION` | `sfx/collision.wav` | 110 | never: opened at boot but not played anywhere |
| `BGM_DEMO` | `music/demo.wav` | 80 | from `demo_init()` to `demo_cleanup()` |

The benchmark scene plays no sound; the BGM stops when the demo is cleaned up and starts again with it.

## The demo's Sound tab

| Item | Options | Default | Effect |
|---|---|---|---|
| Master | On / Off | **Off** | Off sets both global volumes to 0 |
| SFX Vol | 0–100 % in 10 % steps | 80 % | `snd_set_sfx_volume(index × 13)` while Master is On |
| BGM Vol | 0–100 % in 10 % steps | 60 % | `snd_set_bgm_volume(index × 13)` while Master is On |

Because Master defaults to Off and the global volumes start at 0, **the demo boots muted**: the BGM plays, at volume 0, until Master is switched On. 100 % maps to 130, which the wrapper clamps to 128.

## Asset pipeline

| Source | Makefile rule | Output (packed as `rom:/…`) |
|---|---|---|
| `assets/audio/sfx/*.wav` | `audioconv64` | `filesystem/audio/sfx/*.wav64` |
| `assets/audio/music/*.wav` | `audioconv64` | `filesystem/audio/music/*.wav64` |
| `assets/audio/music/*.xm` | `audioconv64` | `filesystem/audio/music/*.xm64` (converted and packed, but nothing plays it) |

- The Makefile runs `audioconv64` without options (`$(N64_AUDIOCONV) -o <dir> <file>`), so its defaults apply: VADPCM compression, and the source's sample rate and channel count. Switching to Opus (`--wav-compress 3`) would also need `wav64_init_compression(3)` before the files are opened, which the engine doesn't call; libdragon asserts otherwise.
- `filesystem/audio/` is git-ignored, and the wav64 format is tied to the libdragon version: a file built by another version asserts `wav64 …: invalid version` at boot (defect D17). After changing the submodule, run `libdragon make clean`, then `libdragon make`.
- A path in the sound bank must name the converted `.wav64`. A missing file, or one pointing at the source `.wav`, stops the ROM with a libdragon assertion: in `snd_init()` for an effect, when it starts for music.
- **Placeholders.** All nine WAVs are 22,050 Hz, 16-bit mono (the mixer's output rate) and come from `tools/gen_placeholder_audio.py`: chirps, tones and a seeded noise burst for the effects, and a 4-second Am–F–C–G chord loop with a 200 ms crossfade for the music. `libdragon exec python3 tools/gen_placeholder_audio.py` rewrites them in `assets/audio/`, overwriting any replacements.

## Measured cost

From [BENCHMARKS.md](BENCHMARKS.md): demo quiet view, debug build, Analogue 3D, 2026-09-23, default Sound settings (Master Off, so the BGM plays at volume 0).

| `audio` slot (`snd_update`) | Validator off | Validator on |
|---|---|---|
| average | 1.30 ms | 0.65–0.78 ms |
| peak | 6.7 ms | — |

Audio was the third-largest CPU item in that view (floor 2.8, objects 2.0, audio 1.3, HUD 1.1 ms) and hasn't been broken down further.

## Limits

- One BGM at a time, wav64 only: no XM or YM playback, and no crossfade (a new track stops the old one first).
- Six SFX voices, round-robin, without priorities; no pitch, panning or 3D positioning (left and right always get the same volume).
- A change of SFX volume doesn't reach effects that are already playing.
- Every effect is opened at boot, and the id list is fixed at compile time (`SOUND_COUNT`).
- At most one mixed buffer per frame (see Per-frame update).
- `SFX_COLLISION` is defined and loaded but unused.

## Source files

| File | Purpose |
|---|---|
| [src/audio/audio.h](../src/audio/audio.h), [src/audio/audio.c](../src/audio/audio.c) | `snd_*` API: mixer setup, channels, volumes, per-frame update |
| [src/audio/sound_bank.h](../src/audio/sound_bank.h), [src/audio/sound_bank.c](../src/audio/sound_bank.c) | `SoundId`, `SoundType`, `SoundDef` and the `sound_bank[]` table |
| [tools/gen_placeholder_audio.py](../tools/gen_placeholder_audio.py) | regenerates the placeholder WAVs |
| [Makefile](../Makefile) | WAV/XM conversion rules |
