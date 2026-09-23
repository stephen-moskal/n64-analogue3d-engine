# Development Workflow

Daily loop for this engine on Windows 11 or macOS. Environment setup is in [SETUP.md](SETUP.md).

## Build Cycle

```powershell
# 1. Edit code in src/
# 2. Build (make runs inside the libdragon Docker container)
libdragon make

# 3. Test in the emulator
ares .\engine-debug.z64             # macOS: open -a ares engine-debug.z64

# 4. Test on hardware (Analogue 3D + SummerCart64 over USB)
sc64deployer upload .\engine-debug.z64
# then power on / reset the console — the cart is set to boot the ROM directly
```

`libdragon make clean` removes `build/`, the ROM and the generated `filesystem/*.sprite` / `filesystem/audio/**` outputs; the next `make` regenerates them from `assets/`. Do this whenever the libdragon submodule changes (asset formats are version-specific).

### VS Code tasks

`Ctrl+Shift+B` (macOS `Cmd+Shift+B`) runs the default build task. Tasks have per-OS commands.

| Task | Action |
|------|--------|
| Build ROM | `libdragon make` |
| Clean Build | `libdragon make clean` |
| Rebuild | Clean Build, then Build ROM (sequential) |
| Run in ares | Build, then launch the emulator with the ROM |
| Upload to SummerCart64 | Build, then upload to the cart |
| Debug (USB Log) | `sc64deployer debug` in a dedicated terminal |

## Debugging

### Log channels

`src/main.c` enables both channels at startup:

```c
debug_init_isviewer();   // emulator: ares shows it (Homebrew Mode on)
debug_init_usblog();     // hardware: sc64deployer debug shows it
```

Use `debugf()` anywhere:

```c
debugf("Player position: %f, %f\n", x, y);
```

- **Hardware:** `sc64deployer debug` in a second terminal, before you reset the console. The ROM prints `SMozN64 Dev Engine` on boot, so a silent log means the ROM did not start or the debug tool is not attached.
- **Emulator:** ares prints ISViewer output to its terminal/log window.

### Crashes and assertions

libdragon's inspector takes over the screen on an exception or `assertf()` failure and shows the message, the failed expression and a symbolized backtrace (the `.sym` file is embedded in the ROM by n64.mk). The same text goes to the debug log. Example seen on 2026-09-12: `wav64 rom:/audio/sfx/menu_open.wav64: invalid version` from `snd_init` — stale generated assets, fixed by a clean rebuild.

### RDP validation

In debug builds (`engine-debug.z64`) the RDP validator is toggled from **Start menu → Debug → RDP Check**. It validates every RDP command and reports mistakes that ares tolerates but real hardware does not (fill-mode triangles, a textured triangle format with a colour combiner that ignores the texture, missing Z-buffer). Messages go to the debug log (`sc64deployer debug` / ares). Release builds compile it out.

Two cautions, both learned on the Analogue 3D on 2026-09-23:

- **Keep the log quiet.** Each validator message is printed over USB, and a defect that repeats every triangle floods the log and stalls every frame. The first run flagged roadmap defect D2 about 41,000 times and the demo crawled; fixing D2 restored 60 FPS.
- **It costs CPU time.** With the validator on, heavy frames (the menu open) run past 16.7 ms. On the A3D that shows as flicker in the lower part of the screen; ares only shows the FPS drop, and release builds do not flicker. That is why the validator is off at boot. Turn it on to check a feature, turn it off to judge performance. Tracked as defect D18 in ROADMAP_v2.
- **Toggling is done at a frame boundary.** The engine drains the RSP/RDP (`rspq_wait()`) before starting or stopping the validator; switching it mid-frame produced bogus `SET_COLOR_IMAGE` errors and once left the RSP halted (RSP crash in the audio mixer). Right after it starts you may still see about 15 "textured primitive ... combiner" warnings on text glyphs, logged as `SET_COMBINE_MODE last sent at 0x0`: the text mode was set before the validator started. They appear once per start and can be ignored; persistent repeats of a warning are real.

### Emulator tools

ares (Homebrew Mode on): Tools → Tracer (CPU trace), Tools → Memory. Remember that ares is lenient: an RDP misuse that works there can hang the console, so test on hardware before calling a feature done.

### Common patterns

- **Crash on startup** — asset format mismatch (see above), a `sprite_load`/`wav64_open` path typo (paths are `rom:/...`), or a DMA buffer that is not uncached/8-byte aligned.
- **Graphics wrong on hardware only** — RDP mode/format mismatch (fill mode with triangles, `TRIFMT_ZBUF_*` without an attached Z-buffer, combiner vs vertex format). Enable `rdpq_debug_start()`.
- **RSP timeout in `display_get`** — RDP pipeline misconfiguration; same checks.
- **Input not working** — `action_init()` (which calls `joypad_init()`) must run before polling; check the port.

## Performance

Today the HUD shows FPS (`display_get_fps()`), triangle count and TMEM uploads (`T:`/`U:`), object counts and collision stats. Ad-hoc timing uses the CPU tick counter, as in `main.c`:

```c
uint32_t t0 = TICKS_READ();
// ... work ...
float ms = TICKS_DISTANCE(t0, TICKS_READ()) / (float)(TICKS_PER_SECOND / 1000);
debugf("update: %.2f ms\n", ms);
```

Per-phase profiling, RDP busy time, memory stats, a benchmark scene and a CSV export are Phase 1 of [ROADMAP_v2.md](ROADMAP_v2.md).

## Asset Pipeline

Sources live in `assets/`; `make` converts them with the container's tools into `filesystem/`, which `mkdfs` bundles into the ROM.

| Source | Tool (Makefile rule) | Output | Loaded with |
|--------|----------------------|--------|-------------|
| `assets/*.png` (32×32) | `mksprite --format RGBA16` | `filesystem/*.sprite` | `sprite_load("rom:/name.sprite")` |
| `assets/audio/sfx/*.wav` | `audioconv64` | `filesystem/audio/sfx/*.wav64` | `wav64_open("rom:/audio/sfx/name.wav64")` via `snd_*` |
| `assets/audio/music/*.wav` / `*.xm` | `audioconv64` | `filesystem/audio/music/*.wav64` / `*.xm64` | `snd_play_bgm()` |

Generated outputs are ignored by git. Placeholder WAVs can be regenerated with `python tools/gen_placeholder_audio.py`. Models (`*.t3dm` via Tiny3D) arrive in ROADMAP_v2 Phase 4.

Adding a texture: drop `name.png` in `assets/`, rebuild, load it into a slot with `texture_load_slot()` (see [TEXTURES.md](TEXTURES.md)). Adding a sound: drop the WAV in `assets/audio/sfx/`, add a `SoundDef` in `src/audio/sound_bank.c`.

## Version Control

Committed: `src/`, `assets/`, `docs/`, `Makefile`, `.vscode/`, `.libdragon/config.json`, `.gitattributes`, the `libdragon` submodule pointer.
Ignored: `build/`, `*.z64/*.elf/*.dfs/*.sym`, generated `filesystem/*.sprite` and `filesystem/audio/`, `*.pak` (emulator saves), `*.log`.

Line endings are forced to LF by `.gitattributes`; on Windows also set `core.autocrlf=false` in the repo and the submodule (SETUP.md step 6).

```bash
git checkout -b feature/thing
libdragon make && ares engine-debug.z64      # iterate
sc64deployer upload engine-debug.z64         # verify on hardware before merging
git commit -am "Feature: thing"
```

## Real Hardware Testing

1. Build: `libdragon make`
2. Connect the cart (USB); the console may be off during upload.
3. Upload: `sc64deployer upload engine-debug.z64` (sets boot mode to "Bootloader → ROM").
4. Start `sc64deployer debug` in another terminal, then power on / reset the console.
5. Check the HUD FPS and exercise the feature; note anything that differs from ares.

| Aspect | ares | Analogue 3D (FPGA N64) |
|--------|------|------------------------|
| Speed / timing | close, not exact | real |
| RDP strictness | lenient (fill-mode triangles "work") | strict (hangs / RSP timeout) |
| Debug output | ISViewer (Homebrew Mode) | USB log via sc64deployer |
| Inspector / backtrace | yes | yes (also over USB) |

A feature is not done until it runs on the console. See ROADMAP_v2 §11 for the verification checklist.
