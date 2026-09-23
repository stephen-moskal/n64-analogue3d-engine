# CLAUDE.md - N64 Dev Engine

## Project Overview
Nintendo 64 homebrew game engine built on libdragon (`preview` branch, vendored as the `libdragon/` git submodule pinned at `10f3bd43e`, 2026-02-27). Verified on real hardware (Analogue 3D via SummerCart64) and in the ares emulator. Long-term goal: an action-RPG engine supporting souls-like combat and Final Fantasy Tactics-style battles, general enough for other genres.

Current state (2026-09-23): engine features from v1 (mesh system, multi-object scenes, camera, collision, physics, lighting + shadows, billboards, particles, fog/atmosphere, audio, action-mapped input, tabbed menu, text) plus the Phase 1 developer tooling in `src/debug/` (profiler, stats, memory, frame time, overlay pages, RDP counters, RDP capture, crash test), a benchmark scene, host unit tests and CI. Planning lives in `docs/ROADMAP_v2.md`: Phases 0 and 1 are done; Phase 2 (engine hardening, defect register D1–D19) is next, then CPU-path graphics features and Tiny3D. The engine is CPU-bound (see `docs/BENCHMARKS.md`).

## Build & Deploy
Development happens on Windows 11 (PowerShell) and macOS. The `libdragon` npm CLI runs `make` inside the Docker container `ghcr.io/dragonminded/libdragon:latest` (config in `.libdragon/config.json`, vendor strategy = submodule). Full setup: `docs/SETUP.md`.

```powershell
libdragon make                    # debug build -> engine-debug.z64 (validator, asserts, profiler)
libdragon make BUILD=release      # release build -> engine.z64 (debug code compiled out)
libdragon make clean              # also deletes generated filesystem/ assets; next make regenerates them
libdragon install                 # rebuild libdragon into the container after touching the submodule

ares .\engine-debug.z64             # emulator (macOS: open -a ares engine-debug.z64); Homebrew Mode on
sc64deployer upload .\engine-debug.z64   # cart over USB, then power on / reset the console
sc64deployer debug                # second terminal: debugf()/usblog output from the ROM
```

VS Code tasks (`.vscode/tasks.json`) wrap the same commands with per-OS variants; `Ctrl+Shift+B` builds.

## Repository Gotchas
- **Line endings must be LF.** The container reads `Makefile`/`n64.mk`/`build.sh` from the bind mount. `.gitattributes` forces LF; on Windows the repo and the `libdragon/` submodule also need `core.autocrlf=false` + `core.eol=lf` (SETUP.md step 6).
- **Generated assets are not committed.** `filesystem/*.sprite` and `filesystem/audio/**` are built from `assets/` by `mksprite`/`audioconv64`; their format depends on the libdragon version (a stale `.wav64` asserts `invalid version` at boot). Run `libdragon make clean` after changing the submodule.
- `libdragon make` does not rebuild libdragon; `libdragon install` does.
- The Makefile compiles every `src/*.c` and `src/*/*.c` automatically, per variant into `build/debug/` or `build/release/`, and includes the generated `.d` files so header edits rebuild dependents.
- `debugf`/`assertf` are compiled out of release builds (`NDEBUG`), so logs, CSV dumps and benchmark output need the debug ROM. Both variants are `-O2`.
- The RDP validator is off at boot and toggled from the Debug tab (it costs CPU and causes flicker on the A3D when frames overrun, D18); it must be toggled at a frame boundary (`rspq_wait()` first).
- `sc64deployer debug` holds the COM port (stop it before `upload`) and exits when stdin closes.
- New `.c` files: add a profiler slot / stats counter where they do per-frame work; new Debug tab items go at the end of `DebugMenuItem`.
- Particle emitters keep the `ParticleEmitterDef` pointer: definitions must have static storage.

## Architecture

### Source Layout
```
src/main.c                 entry point: debug init, display (320x240 16-bit, triple-buffered), rdpq, DFS,
                           menu construction (5 tabs), audio, Z-buffer, scene manager, variable-timestep loop
src/math/vec3.h            vec3 math (header-only)
src/render/                camera (orbital/fixed/follow, frustum, collision), mesh (builder + mesh_draw()),
                           mesh_defs (pillar/platform/pyramid/sphere), cube, lighting (Blinn-Phong, sun,
                           4 point lights), shadow (blob + projected), billboard, particle (128 pool, direct
                           RDP batch), atmosphere (fog, sky, 7 presets), floor (10x10 grid), texture (16 slots)
src/input/                 action (remappable ActionContext), input (camera adapter)
src/collision/             sphere/AABB colliders, raycasts, layers (64 max)
src/physics/               semi-fixed timestep bodies, gravity, bounce, ground raycast
src/scene/                 Scene/SceneObject lifecycle, SceneManager, transitions, soft reset
src/scenes/demo_scene.c    the demo (objects, menu semantics, HUD) — 1,325 lines, the largest file
src/audio/                 snd_* mixer wrapper (BGM ch0, SFX ch2-7), sound_bank table
src/ui/                    text (rdpq_text, builtin fonts), menu (tabbed, snapshot/revert)
src/debug/                 engine_debug.h (build switches), debug_menu (Debug tab + D-Up/D-Down), stats,
                           profiler (+ RDP counters), memstats, frametime, overlay, rdp_debug
src/scenes/benchmark_scene.c   26-step stress test, BENCH CSV rows
tests/host/                host unit tests with a libdragon shim (libdragon exec make -C tests/host run)
tools/                     bench_compare.py, rdp_log_to_hex.py, rom_budget.py, ci_build.sh, rspq_profile.ps1
assets/                    source PNGs and WAVs; filesystem/ holds the generated outputs (ignored)
tools/gen_placeholder_audio.py   regenerates the placeholder WAVs
```

### Rendering Pipeline
CPU software transform + hardware RDP rasterization, hardware 16-bit Z-buffer (no painter's sort):
1. CPU (`mesh_draw()`): bounding-sphere frustum cull, MVP transform per vertex, per-face-group Blinn-Phong lighting and backface cull, near-plane/guard-band clipping, viewport map.
2. RDP: `rdpq_triangle()` with `TRIFMT_ZBUF_TEX` / `TRIFMT_ZBUF_SHADE(_TEX)` (fog uses shade alpha), per-frame TMEM uploads (32x32 RGBA16 sprites), fill rectangles for the sky gradient and UI panels.

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
- All source in `src/`, headers alongside their `.c` files; one subsystem per directory; each subsystem gets a `docs/*.md`.
- Makefile uses libdragon's `n64.mk` include system; assets convert at build time into `filesystem/` (bundled into the `.dfs`).
- Work is incremental and measurable: every feature in ROADMAP_v2 has a test plan (ares + hardware) and a benchmark metric; a feature is done only when it runs on the Analogue 3D.
- IDE will show clang errors for libdragon headers — expected (cross-compilation toolchain).

## Reference Documentation
- Planning: `docs/ROADMAP_v2.md` (current), `docs/ROADMAP.md` (v1 record of Features 1–10)
- Environment/workflow: `docs/SETUP.md`, `docs/WORKFLOW.md`
- Tooling: `docs/DEBUGGING.md`, `docs/PROFILING.md`, `docs/BENCHMARKS.md`, `docs/HARDWARE.md`
- Systems: `docs/ARCHITECTURE.md`, `docs/RENDERING.md`, `docs/MESH_SYSTEM.md`, `docs/CAMERA.md`, `docs/TEXTURES.md`, `docs/COLLISION.md`, `docs/PHYSICS.md`, `docs/SCENE_SYSTEM.md`, `docs/INPUT.md`, `docs/MENU_SYSTEM.md`
- External: libdragon sources in `libdragon/` (submodule); optional N64 reference collection `../awesome-n64-development/` if checked out beside this repo
