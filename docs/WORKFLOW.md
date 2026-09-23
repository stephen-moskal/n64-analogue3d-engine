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
| Build ROM (debug) | `libdragon make` → `engine-debug.z64` (default build task) |
| Build ROM (release) | `libdragon make BUILD=release` → `engine.z64` |
| Clean Build | `libdragon make clean` |
| Rebuild | Clean Build, then Build ROM (debug) |
| Run in ares (debug / release) | Build, then launch the emulator with the ROM |
| Upload to SummerCart64 (debug / release) | Build, then upload to the cart |
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

### RDP validation and crash diagnostics

Moved to [DEBUGGING.md](DEBUGGING.md): the Debug tab, RDP Check (validator) cautions, one-frame RDP capture with offline `rdpvalidate`, the crash inspector and the Crash Test item.

### Emulator tools

ares (Homebrew Mode on): Tools → Tracer (CPU trace), Tools → Memory. Remember that ares is lenient: an RDP misuse that works there can hang the console, so test on hardware before calling a feature done.

### Common patterns

- **Crash on startup** — asset format mismatch (see above), a `sprite_load`/`wav64_open` path typo (paths are `rom:/...`), or a DMA buffer that is not uncached/8-byte aligned.
- **Graphics wrong on hardware only** — RDP mode/format mismatch (fill mode with triangles, `TRIFMT_ZBUF_*` without an attached Z-buffer, combiner vs vertex format). Enable `rdpq_debug_start()`.
- **RSP timeout in `display_get`** — RDP pipeline misconfiguration; same checks.
- **Input not working** — `action_init()` (which calls `joypad_init()`) must run before polling; check the port.

## Performance

The in-engine tools (all in `src/debug/`, toggled from the Start menu's **Debug** tab) are described in [PROFILING.md](PROFILING.md):

- HUD `FPS … CPU x.x ms`; **D-Up** cycles overlay pages (Stats, Profiler, Memory, Frame, RSP); **D-Down** dumps CSV rows to the debug log.
- **Benchmark scene** (Debug → Scene = Benchmark): 26 deterministic steps, one CSV row each. Capture and compare against the committed baseline:

```powershell
sc64deployer debug | Tee-Object capture.log          # then run the benchmark on the console
Select-String '^BENCH' capture.log | % Line > new.csv
python tools/bench_compare.py docs/benchmarks/2026-09-23-baseline-debug-a3d.csv new.csv
```

Rule of thumb from the baseline (BENCHMARKS.md): the engine is CPU-bound; ~24–28 flat 32-triangle objects fit in a 60 FPS frame on the current CPU path.

## Tests and CI

```powershell
libdragon exec make -C tests/host run      # host unit tests (pure-logic modules)
libdragon exec bash tools/ci_build.sh      # what CI runs: both ROMs, tests, ROM/RAM budgets, hot-text layout
```

GitHub Actions (`.github/workflows/build.yml`) runs the same on every push and uploads the ROMs, `.sym` and `.map` files as artifacts. Hardware checks stay manual (see ROADMAP_v2 §11).

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
