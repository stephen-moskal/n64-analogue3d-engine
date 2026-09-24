# CLAUDE.md - N64 Dev Engine

## Project Overview
Nintendo 64 homebrew game engine built on libdragon (`preview` branch, vendored as the `libdragon/` git submodule pinned at `39d0d6096`, 2026-09-15; upgraded from `10f3bd43e` in Phase 2 S4b). Verified on real hardware (Analogue 3D via SummerCart64) and in the ares emulator. Long-term goal: an action-RPG engine supporting souls-like combat and Final Fantasy Tactics-style battles, general enough for other genres.

Current state (2026-09-24): engine features from v1 (mesh system, multi-object scenes, camera, collision, physics, lighting + shadows, billboards, particles, fog/atmosphere, audio, action-mapped input, tabbed menu, text) plus the Phase 1 developer tooling in `src/debug/` (profiler, stats, memory, frame time, overlay pages, RDP counters, RDP capture, crash test, Reset Soak, Menu Sweep), a benchmark scene, host unit tests and CI. Planning lives in `docs/ROADMAP_v2.md`: Phases 0 and 1 are done; Phase 2 (engine hardening, stages S0–S13) is in progress with S0–S4b verified on the A3D (S4b: libdragon upgrade fixing the RSP race D28, sound module rework); S5 (UI core: cached menu, HUD and overlay text, three styles; dialog system with JSON source and text box) and S6 (engine core in src/engine/, frame pacing, hot data pinned to fixed D-cache colours, D18 root-caused in libdragon's validator) verified 2026-09-24; next S7 (scene owns physics and colliders), then CPU-path graphics features and Tiny3D. The engine is CPU-bound (see `docs/BENCHMARKS.md`).

## Build & Deploy
Development happens on Windows 11 (PowerShell) and macOS. The `libdragon` npm CLI runs `make` inside the Docker container `ghcr.io/dragonminded/libdragon:preview` (config in `.libdragon/config.json`, vendor strategy = submodule). Full setup: `docs/SETUP.md`.

```powershell
libdragon make                    # debug build -> engine-debug.z64 (validator, asserts, profiler)
libdragon make BUILD=release      # release build -> engine.z64 (debug code compiled out)
libdragon make BENCH=1            # engine-debug-bench.z64: boots straight into the full benchmark (BENCH_KIND=AUDIO: one kind)
libdragon make clean              # also deletes generated filesystem/ assets; next make regenerates them
libdragon install                 # rebuild libdragon into the container after touching the submodule
libdragon exec bash tools/ci_build.sh   # what CI runs: both ROMs, host tests, budgets, hot-text layout

ares .\engine-debug.z64             # emulator (macOS: open -a ares engine-debug.z64); Homebrew Mode on
sc64deployer upload .\engine-debug.z64   # cart over USB, then power on / reset the console
sc64deployer debug                # second terminal: debugf()/usblog output from the ROM
```

VS Code tasks (`.vscode/tasks.json`) wrap the same commands with per-OS variants; `Ctrl+Shift+B` builds.

## Repository Gotchas
- **Line endings must be LF.** The container reads `Makefile`/`n64.mk`/`build.sh` from the bind mount. `.gitattributes` forces LF; on Windows the repo and the `libdragon/` submodule also need `core.autocrlf=false` + `core.eol=lf` and a re-checkout (SETUP.md step 6).
- **Generated assets are not committed.** `filesystem/*.sprite`, `filesystem/audio/**`, `filesystem/fonts/**` and `filesystem/dialog/**` are built from `assets/` by `mksprite`/`audioconv64`; their format depends on the libdragon version (a stale `.wav64` asserts `invalid version` at boot). Run `libdragon make clean` after changing the submodule.
- `libdragon make` does not rebuild libdragon; `libdragon install` does. To move an existing project to another toolchain image: `libdragon init -i <image>`.
- Toolchain (the `:preview` image): GCC 16.2, binutils 2.45; every file is built with `-mfix4300` (VR4300 multiply errata). The Makefile sets `LIBDRAGON_PREVIEW = 1`: libdragon APIs still marked preview are allowed but each use warns.
- At this libdragon version `mksprite` compresses sprites and `audioconv64` encodes `.wav64` as VADPCM by default; any `.wav64` use links the Opus and ULC codecs (~40 KB). Decoding Opus needs the full decoder (~94 KB more RAM): opt-in with `make SND_OPUS=1` (docs/AUDIO.md).
- Build options that change code (`SND_OPUS`) are recorded in `build/<variant>/options.stamp`; changing one rebuilds that variant. Debug-only ROM data (the audio benchmark's extra tracks) is built under `build/<variant>/fs-debug/` and staged into debug ROMs only; release ROMs pack `filesystem/` alone. Changing an `AUDIOCONV_*` flag does not re-encode existing files (`libdragon make clean`).
- **Audio mixing runs right after `display_get()`** (`audio_poll()` in `main.c`). Each mixed buffer is a high-priority RSP job the CPU waits for; after `rdpq_detach_show()` it waited behind the frame's RSP work (2.1 ms per frame with music, dropped frames; D29). Rerun Bench = Audio when the frame loop or the RSP load changes. Sound effects must be mono (one mixer channel per voice) and share one encoding.
- libdragon audio quirks the sound module works around (docs/AUDIO.md):
  - Mixer channels are limited to the output rate unless raised, and Opus is always 48 kHz: `snd_init()` raises the limits to the fastest sound in the bank (D30).
  - A channel's sample ring reused across encodings after Opus/ULC asserts `samplebuffer too small`, so music slots get a fresh ring per track change (D31).
  - VADPCM can only seek to skip points, so muted music restarts from the top.
- The Makefile compiles every `src/*.c` and `src/*/*.c` automatically, per variant into `build/debug/`, `build/release/` (or `build/<variant>-bench/` with `BENCH=1`), and includes the generated `.d` files so header edits rebuild dependents. `-Wall -Werror` applies to the engine and to the host tests.
- `debugf`/`assertf` are compiled out of release builds (`NDEBUG`), so logs, CSV dumps and benchmark output need the debug ROM. Both variants are `-O2`.
- Python tools run in the container: `libdragon exec python3 tools/<tool>.py ...` (no host Python needed).
- The RDP validator is off at boot and toggled from the Debug tab; it must be toggled at a frame boundary (`rspq_wait()` first). While it is on, the A3D tears in the lower screen: libdragon validates inside interrupt handlers with interrupts off, so the vblank flip lands mid-picture (D18, root-caused S6.4; libdragon `VI WARNING` lines, the torn-frame counter). Not an engine bug; judge visuals and timing with it off.
- `sc64deployer debug` holds the COM port (stop it before `upload`) and exits when stdin closes.
- New `.c` files: add a profiler slot / stats counter where they do per-frame work; new Debug tab items go at the end of `DebugMenuItem`. Recipes for scenes, meshes, textures, menu items, profiler slots, stats counters, benchmark kinds and host tests: `docs/EXTENDING.md`.
- **Render-path code placement matters.** The VR4300 I-cache is 16 KB direct-mapped; a triangle loop that collides with `rdpq_triangle_rsp` costs ~1,100 cycles per triangle. Per-triangle code is `ENGINE_HOT` (`src/engine/hot.h`) and linked in the hot-text block (`src/engine/hot_text.ld`, inserted into libdragon's `n64.ld` as `build/<variant>/engine.ld`). `hot_text.ld` selects code by **object file name**, so renaming or splitting a render `.c` file means updating it; `tools/hot_text.py` runs in CI and must stay clean. Texture uploads (~7 KB of libdragon code) cannot fit in the mesh phase, so `mesh_draw` skips repeat uploads instead of pinning them. Per-triangle scratch arrays take `ENGINE_NOINIT` (libdragon's `-ftrivial-auto-var-init=pattern` memsets them otherwise). Static data the drawing loops touch is pinned to fixed D-cache colours away from the stack by `src/engine/hot_data.ld` (by input-section name, libdragon's included; D34); `tools/hot_data.py` runs in CI, and a new per-element array or counter must be added to both. See `docs/HARDWARE.md`.
- Every mesh builder ends with `mesh_finalize()` (bounds, group analysis, exact-size geometry block); nothing can be added afterwards.
- Mesh winding: front faces are counter-clockwise seen from outside (the curved-group cull and the projected-shadow cull depend on it; `tests/host/test_mesh.c` checks the `mesh_defs` shapes).
- The camera rebuilds its matrices only when `dirty` (set by the `camera_*` setters) or when following a target; code that edits `Camera` fields directly must set `dirty`.
- Scenes poll input themselves (`action_update()` in `on_update`); the Start menu is driven by the demo scene only. The menu is nearly full (6/6 tabs; Debug uses 12 of 12 items, Controls 11) and `menu_add_item` returns -1 past the limits.
- Particle emitters keep the `ParticleEmitterDef` pointer: definitions must have static storage.
- Frame pacing (docs/ENGINE.md): `dt` is `display_get_delta_time()` and the 30 FPS cap is `engine_set_fps_limit()` (libdragon's display limit, no busy-wait). Under triple buffering the loop time jitters (~12.5/21 ms) while every frame still reaches the screen on time, so judge smoothness by presented frames (Frame page "Shown late", `FTP` / `BENCH_PRESENT` rows), not loop p99.
- Measuring on the A3D: the first ~7.5 s after every reset run ~35–47 % slower (D32; boot-to-benchmark ROMs settle 15 s). Before S6.3, builds that differed only in unrelated code differed ~10 % on mesh-heavy steps because static data slid through the D-cache (D34); the render path's static data is now pinned, but mesh vertex data on the heap still moves (D26, fixed by the P3.1 vertex cache): near the 5 % gate, use a same-ROM A/B or `make LAYOUT_PAD=448` to check that a result is not layout.
- Text: render a laid-out paragraph with `text_render_paragraph()`, never `rdpq_paragraph_render()` directly. The font sets its mode in a recorded block that rdpq's CPU-side tracking misses; after a fill-mode clear this silently broke later `text_draw()` calls (docs/UI.md).
- UI text: `text_draw()` sets standard mode before each print (a font sets its mode in a recorded block the CPU-side tracking does not see; without it, text after fill/copy-mode drawing comes out invisible). Text cached in a `UiLayer` needs a plain monochrome font (`FONT_UI_*`, from `assets/fonts/`): libdragon's outlined builtins leave the alpha bit clear offscreen. Layer slot boxes are widened to 4-pixel columns (fill mode on 16-bit surfaces needs them). `menu_draw(menu, view)` takes a `MenuView`.

## Architecture

### Source Layout
```
src/main.c                 the demo game: Start menu construction (6 tabs incl. Debug), scene manager, scene switches (app_frame)
src/math/vec3.h            vec3 math (header-only)
src/render/                camera (orbital/fixed/follow, frustum, collision), mesh (builder mesh_build.c + mesh_draw()),
                           mesh_defs (pillar/platform/pyramid/sphere), cube, lighting (Blinn-Phong, sun,
                           4 point lights), shadow (blob + projected), billboard, particle (128 pool: simulation
                           particle.c, renderer particle_draw.c), atmosphere (fog, sky, 7 presets), floor (10x10 grid),
                           texture (16 slots)
src/input/                 action (remappable ActionContext), input (camera adapter)
src/collision/             sphere/AABB colliders, raycasts, layers (64 max)
src/physics/               semi-fixed timestep bodies, gravity, bounce, ground raycast
src/scene/                 Scene/SceneObject lifecycle, SceneManager, transitions, soft reset, background (sky or clear)
src/scenes/demo_scene.c    the demo (objects, menu semantics, HUD), the largest file
src/scenes/benchmark_scene.c   stress test (All = 26 steps, plus Layout, Overload, Audio, UI); BENCH / BENCH_PROF / BENCH_LAYOUT rows
src/audio/                 sound module (snd_*): crossfading music slots, 8 prioritised SFX voices, positional sound,
                           master/music/SFX volume ramps; snd_mix (pure, host-tested); sound_bank table
src/ui/                    text (fonts, text_draw), menu (model + input, host-tested), menu_view (drawing in a UiStyle),
                           ui_layer (cached text slots), ui_hud (HUD panels), ui_style (Debug/Classic/Minimal),
                           ui_draw (rectangles, gradients, gauges), textbox (dialog box: pages, reveal, choices); docs/UI.md
src/dialog/                dialog runner (pure, host-tested): banks compiled from assets/dialog/*.json; docs/DIALOG.md
src/debug/                 engine_debug.h (build switches), debug_menu (Debug tab + D-Up/D-Down), stats,
                           profiler (+ RDP counters), memstats, frametime, overlay, rdp_debug, testbed (Reset Soak, Menu Sweep)
src/engine/                engine.c/h (engine_init: display 320x240 16-bit triple-buffered, rdpq, DFS, input, text, audio,
                           Z-buffer; engine_run: the variable-timestep frame loop), engine_config.h (screen size, FB count,
                           max dt, guard band: use these, never literals), hot.h (ENGINE_HOT, ENGINE_NOINIT),
                           hot_text.ld / hot_data.ld (I- and D-cache placement of the render path, inserted into
                           n64.ld by engine_ld.awk), layout_pad.c (make LAYOUT_PAD=N); docs/ENGINE.md
tests/host/                host unit tests with a libdragon shim (libdragon exec make -C tests/host run)
tools/                     bench_compare.py, hot_text.py, hot_data.py, rdp_log_to_hex.py, rom_budget.py, ci_build.sh, rspq_profile.ps1,
                           gen_placeholder_audio.py, dialog_build.py (dialog JSON -> .dlg, run by the Makefile),
                           disasm.sh (one function's disassembly: check what the compiler made of a hot loop)
assets/                    source PNGs, WAVs, fonts, dialog JSON; filesystem/ holds the generated outputs (ignored)
```

### Rendering Pipeline
CPU software transform + hardware RDP rasterization, hardware 16-bit Z-buffer (no painter's sort):
1. CPU (`mesh_draw()`): bounding-sphere frustum cull, MVP transform per vertex, Blinn-Phong lighting and back-face cull once per flat face group (per triangle for curved groups such as sphere bands), near-plane/guard-band clipping, viewport map.
2. RDP: `rdpq_triangle()` with `TRIFMT_ZBUF` (flat) / `TRIFMT_ZBUF_TEX` (textured), or `TRIFMT_ZBUF_SHADE(_TEX)` when fog is on (fog uses shade alpha); per-frame TMEM uploads (32x32 RGBA16 sprites); fill rectangles for the sky gradient and UI panels.

### Critical Hardware Rules
- **Fill mode is ONLY for rectangles.** Triangles MUST use 1-cycle (standard) mode or they crash on real hardware:
  ```c
  // CORRECT for triangles:
  rdpq_set_mode_standard();
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
  rdpq_set_prim_color(color);
  rdpq_triangle(&TRIFMT_FILL, v1, v2, v3);

  // CORRECT for rectangles:
  rdpq_set_mode_fill(color);
  rdpq_fill_rectangle(x0, y0, x1, y1);

  // WRONG — crashes on hardware, works in emulators:
  rdpq_set_mode_fill(color);
  rdpq_triangle(...);
  ```
- `TRIFMT_ZBUF_*` formats need a Z-buffer attached (`rdpq_attach(fb, &zbuf)`); particles and shadows use Z-read on / Z-write off.
- Ares emulator is lenient — always verify on hardware/FPGA. `rdpq_debug_start()` catches many of these mistakes at runtime.
- RSP timeout in `display_get` usually means RDP pipeline misconfiguration.
- 4 MB RDRAM, 4 KB TMEM (a 32x32 RGBA16 texture is 2 KB), DMA buffers uncached and 8-byte aligned.

## Conventions
- All source in `src/`, headers alongside their `.c` files; one subsystem per directory; each subsystem has a `docs/*.md`.
- Makefile uses libdragon's `n64.mk` include system; assets convert at build time into `filesystem/` (bundled into the `.dfs`).
- Work is incremental and measurable: every feature in ROADMAP_v2 has a test plan (ares + hardware) and a benchmark metric; a feature is done only when it runs on the Analogue 3D. The stage gate (CI script, ares, A3D checklist, `bench_compare.py`, a BENCHMARKS.md row) is in ROADMAP_v2 §6.4 and `docs/EXTENDING.md`.
- IDE will show clang errors for libdragon headers — expected (cross-compilation toolchain).

## Reference Documentation
- Engine core: `docs/ENGINE.md`
- Planning: `docs/ROADMAP_v2.md` (current), `docs/ROADMAP.md` (v1 record of Features 1–10)
- Environment/workflow: `docs/SETUP.md`, `docs/WORKFLOW.md`, `docs/EXTENDING.md` (how-to recipes, contributing)
- Tooling: `docs/DEBUGGING.md`, `docs/PROFILING.md`, `docs/BENCHMARKS.md`, `docs/HARDWARE.md`
- Systems: `docs/ARCHITECTURE.md`, `docs/RENDERING.md`, `docs/MESH_SYSTEM.md`, `docs/CAMERA.md`, `docs/TEXTURES.md`, `docs/BILLBOARDS.md`, `docs/PARTICLES.md`, `docs/AUDIO.md`, `docs/COLLISION.md`, `docs/PHYSICS.md`, `docs/SCENE_SYSTEM.md`, `docs/INPUT.md`, `docs/MENU_SYSTEM.md`, `docs/UI.md`
- External: libdragon sources in `libdragon/` (submodule); optional N64 reference collection `../awesome-n64-development/` if checked out beside this repo
