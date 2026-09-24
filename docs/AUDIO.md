# Audio

The sound module (`src/audio/`, `snd_*`) sits on libdragon's audio, mixer and wav64 modules and uses only their stable API. It provides:
- music that crossfades between tracks, and stops decoding while muted;
- eight sound-effect voices that steal by priority;
- positional sound effects, panned and attenuated from a listener;
- master, music and SFX volumes that ramp instead of clicking.

Game code names sounds by `SoundId`; files and mix settings live in one data table, the sound bank. Adding a sound is a recipe in [EXTENDING.md](EXTENDING.md#add-a-sound). The module was rewritten in Phase 2 S4b.2 (ROADMAP_v2 §6); the measurements below are from that stage.

```c
snd_play(SFX_MENU_OPEN);                              // one-shot
snd_play_at(SFX_COLLISION, ball_pos, 0.5f);           // positional, half gain
snd_music_play(BGM_DEMO, 1.0f);                       // crossfade over 1 s
snd_set_volume(SND_VOL_MASTER, 0.0f, 0.25f);          // fade everything out
```

## Setup and channels

`main.c` calls `snd_init()` once, after `dfs_init()`:

```c
audio_init(22050, 4);          // AUDIO_FREQ, AUDIO_BUFFERS
mixer_init(12);                // MIXER_CHANNELS
```

`snd_init()` then opens every SFX entry of the sound bank with `wav64_open()`. Opening parses the header, and libdragon streams the samples from ROM during playback. Music files are opened when a track starts and closed when it has faded out.

| Mixer channels | Use |
|---|---|
| 0–1 | music slot 0 |
| 2–3 | music slot 1: the second slot holds the outgoing track during a crossfade |
| 4–11 | SFX voices 0–7 |

- A stereo waveform plays on two consecutive channels, `ch` and `ch + 1`, so each music slot owns a pair. The placeholder track is mono and uses only the first channel of its pair.
- **Sound effects must be mono.** A stereo effect would also take the next voice's channel. Debug builds assert at boot if an effect has more than one channel.
- libdragon keeps decoder state per channel, so one sound can play on several voices at once.
- Output is 22,050 Hz. libdragon fills 50 buffers per second, so a buffer is 448 samples (20 ms). Four buffers give about 80 ms of slack before the output runs dry. At 60 FPS the mixer runs about 0.8 times per frame.
- **Channel rate limits.** A mixer channel plays waveforms up to its frequency limit, by default the output rate. libdragon asserts on a faster waveform, and release builds do not check (D30).
  - At boot, `snd_init()` reads the rate of every sound in the bank and raises the limit of the music channels and of the voices to their fastest sound.
  - Music files are opened briefly for this, so a broken or missing track fails at boot rather than when it first plays.
  - Opus is always 48 kHz, so with an Opus track in the bank every music channel's ring is sized for 48 kHz.

## API (`audio.h`)

| Function | Behaviour |
|---|---|
| `snd_init()` / `snd_cleanup()` | Set up audio and the mixer, open every SFX / close everything. `snd_cleanup()` is not called today, because the main loop never exits. |
| `snd_update(dt)` | Advance the volume ramps and music fades, release voices that finished, then mix every free audio buffer. Called once per frame by `main.c` at the poll point below. |
| `snd_play(id)` | Play a sound effect. Returns the voice used, or −1 if it was dropped. |
| `snd_play_at(id, pos, gain)` | Positional sound effect (see below). `gain` (0–1) scales it, for example by impact speed. Returns −1 if dropped or out of range. |
| `snd_stop(voice)` / `snd_stop_all_sfx()` | Cut one or every sound effect. |
| `snd_set_listener(pos, right)` | The listener for positional sounds: a position and a unit "right" vector. The demo passes the camera. |
| `snd_music_play(id, fade_s)` | Crossfade from the current track to `id` over `fade_s` seconds (0 = cut). Does nothing if `id` is already playing. Music loops. |
| `snd_music_stop(fade_s)` / `snd_music_current()` | Fade the music out and close it / the current track (`SOUND_NONE` if none). |
| `snd_set_volume(which, v, fade_s)` / `snd_get_volume(which)` | Set `SND_VOL_MASTER`, `SND_VOL_MUSIC` or `SND_VOL_SFX` (0–1, clamped), ramping over `fade_s` / read the target. |
| `snd_available(id)` | True if the sound's file is in this ROM. It looks the file up, so don't call it every frame. |
| `snd_set_poll_point(p)` / `snd_get_poll_point()` | Where in the frame `snd_update` runs (`SndPollPoint`). |
| `snd_stats()` | `SndStats`: active and maximum voices, sounds played / stolen / dropped since boot, buffers mixed in the last update, whether the music is paused. |

Calls with an invalid id, with an id on the wrong bus (for example `snd_play(BGM_DEMO)`), or made before `snd_init()` do nothing.

### Volumes

Every sound's volume is the product of four factors:
- its sound bank `volume`;
- for music, its fade (0–1); for positional effects, the distance and pan gains;
- its bus volume (`SND_VOL_MUSIC` or `SND_VOL_SFX`);
- `SND_VOL_MASTER`.

Changes ramp over `fade_s` seconds, and never faster than 50 ms, so no change clicks. A bus or master change also reaches effects that are already playing.

### Sound effects and voices

`snd_play` picks a voice in this order:
1. A free voice.
2. Otherwise, the busy voice with the lowest priority, as long as that priority is at most the new sound's. Among equal priorities, the oldest goes first. That voice is stolen: its sound is cut.
3. Otherwise, the new sound is dropped.

The demo gives UI sounds priority 20 and world sounds 10. A burst of bounces can never silence the menu, but a menu sound can cut a bounce. `snd_mix.c` holds this choice and the positional maths as pure functions, tested on the host (`tests/host/test_audio.c`).

**Positional sounds.** An entry with `max_dist > 0` is positional; `snd_play_at` on any other entry plays it centred at `gain`.
- Distance: full volume within `min_dist`, falling linearly to silence at `max_dist` (world units).
- Pan: equal power, from where the source lies along the listener's right vector. Hard right sends everything to the right channel; straight ahead sends 0.707 to each side.
- The gains are fixed when the sound starts. Effects are short, so a moving source or listener is not tracked.
- A sound that would be silent is not started, so it doesn't take a voice.

### Music

`snd_music_play` marks the current track as closing and starts its fade-out. It then opens the new track in the other slot and fades it in. A track is released once its fade reaches silence. If a third track starts while a crossfade is still running, the track that was already fading out is cut.

**Muted music does not decode.** Once a track's effective volume and its target are both 0 (Master Off, or BGM Vol 0), its channel stops. When the track becomes audible again, it restarts from the beginning.

It does not resume mid-track because compressed wav64 audio can only seek to precomputed skip points, and there are none unless `audioconv64 --wav-seek` adds them. A seek anywhere else asserts in libdragon's VADPCM decoder.

A music file missing from the ROM asserts in debug builds and is ignored in release builds.

**A fresh sample ring per track (D31).** The mixer keeps a sample ring per channel and reuses it for the next waveform. In libdragon `39d0d6096`, reusing a ring that last served Opus (or ULC) for a waveform with another sample unit, such as VADPCM, asserts `samplebuffer too small`: the ring is resized with the old codec's margin.
- A music slot therefore gets a new ring whenever a different track starts on it (`fresh_ring()` in `audio.c`). `mixer_ch_set_limits()` frees a channel's ring when its limits change, so the limit is bumped and restored.
- Tracks can use any mix of encodings, at the cost of one ring reallocation per track change.
- Sound effects keep their rings, so **all effects must share one encoding**. The Makefile converts the whole directory alike.
- Bench = Audio plays Opus first and later VADPCM on the same channel, as a regression test.

## Poll point: where in the frame the mixer runs

Each mixed buffer runs libdragon's mixer as a high-priority RSP job, and the CPU waits for it. That wait, not the decoding, is most of the audio cost, and it depends on what the RSP is doing at that moment. `main.c` offers three points and runs `snd_update` at the selected one, inside the `audio` profiler slot:

```c
audio_poll(SND_POLL_BEFORE_DISPLAY, dt);   // after the scene update
surface_t *fb = display_get();
audio_poll(SND_POLL_AFTER_DISPLAY, dt);    // default
...draw...
rdpq_detach_show();
audio_poll(SND_POLL_AFTER_PRESENT, dt);    // where the pre-S4b.2 module ran
```

Measured with Bench = Audio on the A3D (2026-09-23, debug build). Each step runs the floor and 16 pillars with the music at full volume. Figures are the `audio` slot's average per frame:

| Poll point | No music | VADPCM music | Raw PCM music | Opus music | VADPCM + a new effect every 4 frames | Frame time p99 / 1 % low (VADPCM) |
|---|---|---|---|---|---|---|
| after `rdpq_detach_show()` (old placement) | — | 2.07 ms | 1.81 ms | 4.23 ms | 1.99 ms | **21.5 ms / 46 FPS** |
| before `display_get()` | — | 3.34 ms | — | — | — | 17.0 ms / 59 FPS |
| after `display_get()` (**default**) | 0.09 ms | **0.48 ms** | 0.52 ms | 3.15 ms | 0.94 ms | 17.6 ms / 57 FPS |

The Opus figures come from a `SND_OPUS=1` build, in which VADPCM at the default point measured 0.46 ms.

- **After `display_get()` is 4–7× cheaper than the other two points.** At 60 FPS the loop is paced by `display_get()` (the limiter is off; D19). The update is short, so both other points run just after the previous frame was queued, while the RSP is most likely still working through it. After `display_get()` returns, that frame has usually drained.
- **The old placement drops frames.** Its wait lands inside the frame and pushes some frames past 16.7 ms, which explains the 2.7 ms average and 10.2 ms peak of the demo's `audio` slot after the libdragon upgrade (BENCHMARKS.md, S4b.1). The upgrade also doubled libdragon's buffer rate (25 → 50 per second), so the mixer runs twice as often.
- **Before `display_get()` has the highest average but keeps frames on time.** Its wait apparently replaces time the loop would otherwise spend blocked in `display_get()`.
- **VADPCM costs the same as raw PCM**, so decoding VADPCM is not the bottleneck.
- **Opus costs ~2.7 ms more CPU per frame** than VADPCM, because most of libdragon's Opus decoder is CPU code. At this load (about 10 ms of CPU) it pushes frames over budget: p99 22 ms, 1 % low 44 FPS. It also adds ~33 KB of heap while it plays (decoder state and a 48 kHz ring). VADPCM stays the default; Opus suits projects short of ROM with CPU to spare.
- **Busy effect voices add about 0.45 ms** per frame at the default point.

The best point depends on the loop's structure and on the RSP's load. Rerun Bench = Audio when either changes: S6 moves frame pacing to `display_set_fps_limit()`, and Tiny3D (Phase 4) moves transform work to the RSP.

## Encodings and build options

`audioconv64` encodes every WAV at build time. The Makefile sets the encoding per directory:

```make
AUDIOCONV_SFX_FLAGS   ?= --wav-compress 1     # assets/audio/sfx/
AUDIOCONV_MUSIC_FLAGS ?= --wav-compress 1     # assets/audio/music/
SND_OPUS ?= 0                                 # 1: link the Opus decoder
```

| Encoding | `--wav-compress` | Demo track (4 s, mono) | Cost (after `display_get()`) | Notes |
|---|---|---|---|---|
| raw PCM | 0 | 176,432 B | 0.52 ms / frame | |
| **VADPCM** (default) | 1 | 46,099 B (3.8× smaller) | 0.48 ms / frame | libdragon's default |
| ULC | 2 | — | not measured | |
| Opus | 3 | 12,676 B (14× smaller) | 3.15 ms / frame | needs `SND_OPUS=1`; always 48 kHz; +33 KB heap while playing |

- **Opus is opt-in.** `wav64` always links the Opus and ULC codecs' small parts (~40 KB). Decoding Opus needs `wav64_init_compression(3)`, which pulls in the full decoder: about 94 KB more RAM in the release build. `snd_init()` calls it only when the build sets `SND_OPUS=1`, and libdragon asserts if an Opus file is opened without it. To ship Opus music, build with `SND_OPUS=1 AUDIOCONV_MUSIC_FLAGS="--wav-compress 3"`.
- **Changing `SND_OPUS` rebuilds the variant.** make doesn't track compiler flags, so the option values are kept in `build/<variant>/options.stamp`, which every object and the DFS depend on.
- **Changing an `AUDIOCONV_*` flag does not re-encode existing files.** Delete `filesystem/audio/` or run `libdragon make clean` afterwards.
- **Generated audio is tied to the libdragon version.** It is git-ignored; a file built by another version asserts `wav64 …: invalid version` at boot (D17). Run `libdragon make clean` after a submodule change.
- **Missing sound files.** A path in the sound bank must name the converted `.wav64`. For an effect, `snd_init()` asserts in debug builds; in release builds that sound stays silent. For music, see above.

**Debug-only data.** The audio benchmark's extra encodings of the demo track (`rom:/audio/bench/demo_raw.wav64`, plus `demo_opus.wav64` with `SND_OPUS=1`) are built under `build/<variant>/fs-debug/`, not `filesystem/`. A debug ROM packs a staging copy of `filesystem/` plus those files (`build/<variant>/fs-stage/`). A release ROM packs `filesystem/` alone, which saves 176 KB. The benchmark skips any track that `snd_available()` doesn't find.

**Placeholders.** All nine WAVs are 22,050 Hz, 16-bit mono and come from `tools/gen_placeholder_audio.py`:
- the effects are chirps, tones and a seeded noise burst;
- the music is a 4-second Am–F–C–G chord loop with a 200 ms crossfade.

`libdragon exec python3 tools/gen_placeholder_audio.py` rewrites them in `assets/audio/`, overwriting any replacements.

## The sound bank

`sound_bank.h` declares `SoundId`, `SndBus` and `SoundDef`; `sound_bank.c` holds the table:

```c
[SFX_MENU_NAV]  = { "rom:/audio/sfx/menu_nav.wav64",  SND_BUS_SFX,   0.60f, PRIO_UI,    0,      0 },
[SFX_COLLISION] = { "rom:/audio/sfx/collision.wav64", SND_BUS_SFX,   0.85f, PRIO_WORLD, 150.0f, 1200.0f },
[BGM_DEMO]      = { "rom:/audio/music/demo.wav64",    SND_BUS_MUSIC, 0.65f, 0,          0,      0 },
```

| Field | Meaning |
|---|---|
| `path` | DFS path of the `.wav64` |
| `bus` | `SND_BUS_SFX`: opened at boot, played on a voice. `SND_BUS_MUSIC`: opened on demand, looped in a music slot |
| `volume` | 0–1, before the bus and master volumes |
| `priority` | SFX only: the voice-stealing order |
| `min_dist`, `max_dist` | SFX only: `max_dist > 0` makes `snd_play_at` positional |

| Id | File (`assets/audio/…`) | Volume | Priority | Played by the demo when (default bindings) |
|---|---|---|---|---|
| `SFX_MENU_OPEN` | `sfx/menu_open.wav` | 0.80 | UI | Start opens the menu |
| `SFX_MENU_CLOSE` | `sfx/menu_close.wav` | 0.80 | UI | Start closes the menu |
| `SFX_MENU_NAV` | `sfx/menu_nav.wav` | 0.60 | UI | the menu cursor or tab moves; cycling objects in select mode |
| `SFX_MENU_SELECT` | `sfx/menu_select.wav` | 0.80 | UI | the menu closes with A or B |
| `SFX_OBJ_SELECT` | `sfx/obj_select.wav` | 0.70 | UI | Z enters object mode |
| `SFX_OBJ_DESELECT` | `sfx/obj_deselect.wav` | 0.60 | UI | Z leaves object mode; B steps back from transform to select |
| `SFX_MODE_CHANGE` | `sfx/mode_change.wav` | 0.70 | UI | B spawns or relaunches the ball; A enters or cycles transform mode |
| `SFX_COLLISION` | `sfx/collision.wav` | 0.85 | world, 150–1,200 units | the physics ball bounces: when its falling speed (above 60) turns upward, at gain speed / 400 (max 1), from the ball's position |
| `BGM_DEMO` | `music/demo.wav` | 0.65 | — | from `demo_init()` (1 s fade-in) to `demo_cleanup()` |
| `BGM_BENCH_RAW`, `BGM_BENCH_OPUS` | the demo track in other encodings | 0.65 | — | Bench = Audio (debug ROMs only) |

## The demo's Sound tab

| Item | Options | Default | Effect |
|---|---|---|---|
| Master | On / Off | **Off** | `SND_VOL_MASTER` 1 or 0, with a 0.25 s fade |
| SFX Vol | 0–100 % in 10 % steps | 80 % | `SND_VOL_SFX` = index / 10 |
| BGM Vol | 0–100 % in 10 % steps | 60 % | `SND_VOL_MUSIC` = index / 10 |

The demo applies the tab when a value changes, and once in `demo_init()` before the music starts. Master defaults to Off, so **the demo boots silent**. The music is opened, but it doesn't decode until Master is switched On, and it then starts from the beginning.

The benchmark scene plays no sound, except in Bench = Audio, which sets every volume to 1 and restores the poll point afterwards.

## Measuring and debugging

- **Profiler:** the `audio` slot times `snd_update()` at the active poll point (PROFILING.md).
- **Stats page:** the line `Snd <voices> voices <buffers> buf` shows the active effect voices and the buffers mixed this frame. The same values are the `snd_voices` and `snd_buffers` columns of the Dump CSV `STATS` row. A frame that mixes 2 or more buffers followed a long frame.
- **Bench = Audio** (Debug tab, or `libdragon make BENCH=1 BENCH_KIND=AUDIO` for a ROM that boots straight into it):
  - the steps are numbered `codec × 100 + poll point × 10 + effects`;
  - codec: 0 none, 1 raw, 2 VADPCM, 3 Opus;
  - poll point: `SndPollPoint` 0 after present, 1 before `display_get`, 2 after;
  - effects: 1 = a new sound effect every 4 frames.
  
  `BENCH_PROF` rows carry `audio_us`. Opus steps run only in `SND_OPUS=1` builds, first, so the VADPCM steps later reuse the channel Opus played on (D31).
- **`snd_stats()`** counts sounds played, stolen and dropped since boot. A high `dropped` count means too few voices or too many high-priority sounds.

## Limits

- wav64 only. The Makefile converts `*.xm` to `.xm64`, but nothing plays XM or YM modules.
- Sound effects are mono one-shots, with no pitch control and no looping effects, and must all share one encoding (D31).
- A positional sound's pan and distance are fixed when it starts.
- Muted music restarts from the top when it becomes audible again.
- Eight effect voices and two music slots, so a third track during a crossfade cuts the oldest. There is one listener.
- The sound list is fixed at compile time (`SOUND_COUNT`).

## Source files

| File | Purpose |
|---|---|
| [src/audio/audio.h](../src/audio/audio.h), [src/audio/audio.c](../src/audio/audio.c) | `snd_*` API: mixer setup, voices, music slots, volume ramps, per-frame update |
| [src/audio/snd_mix.h](../src/audio/snd_mix.h), [src/audio/snd_mix.c](../src/audio/snd_mix.c) | pure helpers: voice choice and positional gains (host-tested) |
| [src/audio/sound_bank.h](../src/audio/sound_bank.h), [src/audio/sound_bank.c](../src/audio/sound_bank.c) | `SoundId`, `SndBus`, `SoundDef` and the `sound_bank[]` table |
| [src/main.c](../src/main.c) | `snd_init()`, and `audio_poll()` at the three poll points |
| [tests/host/test_audio.c](../tests/host/test_audio.c) | voice stealing and positional gain tests |
| [tools/gen_placeholder_audio.py](../tools/gen_placeholder_audio.py) | regenerates the placeholder WAVs |
| [Makefile](../Makefile) | WAV/XM conversion, `AUDIOCONV_*_FLAGS`, `SND_OPUS`, debug-only data |
