# Debugging

How to see what the engine is doing, catch hardware-only mistakes, and read crashes, on the Analogue 3D (via SummerCart64) and in ares. Performance tools are in [PROFILING.md](PROFILING.md); hardware facts in [HARDWARE.md](HARDWARE.md).

## Build variants

| | Debug (`libdragon make`) | Release (`libdragon make BUILD=release`) |
|---|---|---|
| ROM | `engine-debug.z64` | `engine.z64` |
| `debugf` / `assertf` / USB + ISViewer log | on | compiled out (`NDEBUG`) |
| RDP validator, RDP capture, crash test | available (Debug tab) | compiled out, items greyed |
| CPU profiler scopes | on (Debug tab toggle) | compiled out, item greyed |
| Stats, memory, frame time, RDP counters, overlay | on | on |
| Reset Soak, Menu Sweep | run, results in the log | run, on-screen status only |
| Optimisation | `-O2` | `-O2` |

Use the debug ROM day to day; its extra cost is about 0.1 ms per frame. Switches live in `src/debug/engine_debug.h` (`ENGINE_DEBUG`, `ENGINE_PROFILE`, `ENGINE_STATS`, `ENGINE_ASSERT`, `ENGINE_LOG`).

Two more debug variants build into their own directory and ROM: `make BENCH=1` boots straight into a benchmark ([BENCHMARKS.md](BENCHMARKS.md)), `make TOUR=1` runs the screenshot tour (below).

## The Debug tab

Start → L/R to the **Debug** tab (the Start menu exists in the demo scene). Values apply when the menu closes with A; B reverts. One-shot items (`Dump!`, `Reset!`, `Capture!`, `Assert!`, `Run!`) fire once and reset to `---`.

| Item | Options | What it does |
|---|---|---|
| Overlay | Off / Stats / Profiler / Memory / Frame / RSP / Input | on-screen page (see PROFILING.md; Input: INPUT.md, "Debugging"); hidden while the menu is open. Text is cached per row (docs/UI.md); ~120 KB while a page is shown |
| Profiler | On / Off | CPU scope timing (debug) |
| RDP Check | Off / On | runtime RDP validator (debug) |
| Dump CSV | --- / Dump! | writes STATS, PROF, RDP, RSP, FT, MEM rows to the log, 120 frames (~2 s) after the menu closes so the averages no longer include the menu |
| Reset Peaks | --- / Reset! | clears profiler peaks, frame-time window, heap baseline |
| Scene | Demo / Benchmark | switches scene with a fade |
| Bench | All / Objects / Particles / Lights / Textures / Shadows / Fillrate / Overload / Layout / Audio / UI / Latency / Mesh / RSP | which benchmark the Benchmark scene runs (BENCHMARKS.md) |
| RDP Log | --- / Capture! | logs two frames of RDP commands (debug) |
| Crash Test | --- / Assert! | triggers `assertf()` (debug) |
| Reset Soak | --- / Run! | one warm-up and 10 measured scene resets, logs the heap delta (see below) |
| Menu Sweep | --- / Run! | steps every menu option, restores the originals (see below) |

Shortcuts with the menu closed: **D-Up** cycles overlay pages, **D-Down** dumps CSV (with the same ~2 s delay). They are the engine's debug context (`action_ctx_debug`, [INPUT.md](INPUT.md)), which the Debug tab pushes for player 1 below every game context. A game binding on D-Up or D-Down (the Controls tab) therefore takes the button first, and so do a menu or a dialog, whose UI context is modal. They work in every scene, the benchmark's included.

The tab is built by `debug_menu_init()` and applied by `debug_menu_update()` (`src/debug/debug_menu.c`), whose item order is the `DebugMenuItem` enum; how to add an item is in [EXTENDING.md](EXTENDING.md).

## Reset Soak and Menu Sweep

Automated robustness checks in `src/debug/testbed.c`, started from the Debug tab. While one runs, an orange status line (`RESET SOAK n/10`, `SWEEP <tab>: <item> <option>`) is drawn at the top of the screen.

- **Reset Soak** requests a soft reset of the current scene ([SCENE_SYSTEM.md](SCENE_SYSTEM.md)) 11 times, 30 frames apart: one warm-up reset, then 10 measured ones. It logs `SOAK,start,resets=10` and at the end `SOAK,resets=10,heap_before=…,heap_after=…,delta=…,per_reset=…`, then resets the Memory page's heap baseline. A leak-free scene reports `delta=0`.
- **Menu Sweep** runs with the menu closed (it pauses while the menu is open). It steps every item of every tab except Controls and Debug (and skips Reset Scene) through all its options, holding each for 15 frames so the scene applies it, restores each item's original value, and logs one `SWEEP,<tab>,<item>,<options>` line per item and `SWEEP,END,items=…,options=…`. Run it with RDP Check on to validate every menu combination.

## Screenshot tour

`libdragon make TOUR=1` builds `engine-debug-tour.z64`: the demo walks itself through a fixed list of states on a timer, no controller needed (`src/scenes/demo_tour.c`). Each state logs `TOUR,<step>,<name>,<seconds since boot>` as it begins:

| Step | Seconds | On screen |
|---|---|---|
| hero | 0–6 | Clear Day, projected shadows, the default front view |
| menu_settings | 6–11 | the Start menu, Settings tab, cursor on Latency |
| menu_classic | 11–16 | the Lighting tab in the Classic style |
| sunset | 16–21 | the Sunset preset |
| night | 21–26 | the Night preset with point lights (torches) |
| profiler / frame / input | 26–41 | the three overlay pages, 5 s each (HUD off for the Profiler page) |
| dialog | 41–48 | the demo conversation's first page |
| benchmark | 48– | Bench = Objects (8 to 64 pillars), then back to the demo |

The README images are ares window captures taken in the middle of each state and cropped to the picture. Re-shoot them after a visible change, and compare a tour run before and after a change that should not alter the picture. The tour only sets options and the Debug tab through their public calls (`settings_set_choice`, `menu_set_value`), so it also checks that those paths still reach the scene.

## Log channels

`engine_init()` (`src/engine/engine.c`) enables both channels:

- **Hardware:** `debug_init_usblog()` → `sc64deployer debug`. Start it before resetting the console. It holds the cart's COM port, so stop it before `sc64deployer upload`. It exits when its stdin closes; from scripts keep stdin open, e.g. `ping -n 86400 127.0.0.1 >nul | sc64deployer debug > log.txt` (cmd) or `sleep 86400 | sc64deployer debug` (bash).
- **Emulator:** `debug_init_isviewer()` → ares with **Homebrew Mode** enabled.

Every boot prints the texture/audio load lines and `SMozN64 Dev Engine [debug build, <date>]`. A silent log means the ROM did not start or the capture is not attached.

**Capture files for the Python tools.** `tools/bench_compare.py` and `tools/rdp_log_to_hex.py` read UTF-8 as well as the UTF-16 files that Windows PowerShell 5.1's `Tee-Object` and `>` write, so any of the capture forms above works.

**Keep the log quiet.** Each line goes over USB; a message printed every triangle floods the link and stalls every frame (the first validator run printed one warning ~41,000 times and the demo crawled).

## RDP validator (RDP Check)

libdragon's `rdpq_debug_start()` checks every RDP command against the hardware rules and prints `[RDPQ_VALIDATION] WARN/ERROR` lines. It catches the mistakes ares forgives but the Analogue 3D does not.

- Off at boot. Turn it on to check a feature, off to judge performance: it costs CPU time (quiet demo 8 → 13 ms) and pushes heavy frames past 16.7 ms.
- **The screen tears while it is on (D18).** libdragon validates each RDP buffer inside the RSP and RDP interrupt handlers, with interrupts disabled (`__rdpq_trace_fetch` → `__rdpq_trace_flush`, marked `FIXME` in libdragon, still there upstream). A command-heavy buffer, text above all, takes milliseconds, so the vblank interrupt that flips the framebuffer runs late and the flip lands while the picture is being scanned: the lines below it show the new frame, the lines above the old one. You see it in the lower part of the screen when the picture changes. libdragon logs each late vblank as `VI WARNING: __vblank_interrupt outside of vblank period`, and the Frame overlay page counts torn frames ("torn N"). Nothing in the engine causes it: without the validator no run has logged a late vblank. ares shows the late interrupts but draws no tear.
- For unattended checks, `make BENCH=1 BENCH_VALIDATOR=1` boots a benchmark with RDP Check already on.
- The engine drains the RSP/RDP (`rspq_wait()`) before starting or stopping it; stopping it mid-frame once halted the RSP (an RSP crash in the audio mixer's `rspq_highpri_sync`).
- **Ignore the first frame after it starts.** The validator begins with the rest of the RDP's current command buffer, commands it sees without the state they were set up with. That frame reports errors such as `drawing command before a SET_COLOR_IMAGE was sent`, `... before a SET_SCISSOR was sent` and `Z buffer image not configured` (S10: 45 errors in that one frame, then clean frames with the menu open and particles bursting), or about 15 `textured primitive ... combiner` warnings on text glyphs with `SET_COMBINE_MODE last sent at 0x0`. An error that keeps repeating is real.

Messages seen in this codebase:

| Message | Meaning | Fix |
|---|---|---|
| `textured primitive drawn but the color combiner does not use TEX0...` (repeating) | textured triangle format with a flat combiner | use `TRIFMT_ZBUF` for flat materials (was defect D2) |
| `drawing command before a SET_COLOR_IMAGE was sent` | validator started mid-frame | toggle at a frame boundary (the Debug tab does) |
| `Z buffer image not configured but Z buffer mode was requested` | Z-buffered drawing without `rdpq_attach(fb, &zbuf)` (or a capture trimmed after `SET_Z_IMAGE`) | attach the Z-buffer |
| fill-mode triangles | `rdpq_set_mode_fill()` then `rdpq_triangle()` | standard mode for triangles; fill mode only for rectangles |

## One-frame RDP capture and offline validation

For a full listing of what the RDP receives:

1. Start the USB capture into a file (see Log channels).
2. Debug tab → **RDP Log → Capture!**, close with A. The game pauses a few seconds while two frames of commands are printed (every triangle in full; libdragon's `RDPQ_LOG_FLAG_SHOWTRIS`). The capture spans two frames because logging begins when the RSP reaches the marker, part-way through the first frame. libdragon prints from its RSP interrupt with interrupts off, one command buffer at a time, so the picture stalls and tears while it runs (D18). `rdp_debug_frame_end()` waits for the RSP without a time limit before its `rspq_wait()`: that wait gives up after 200 ms and checks the clock before the RSP, so a long print inside it reported an RSP crash although the RSP was idle (Phase 3 S1, in ares at ~1,000 lines; USB prints slower still).
3. Extract the complete frame and validate it in the container (the capture file must be inside the repo, which is what the container sees; host Python works too: `py` on Windows, `python3` on macOS):

```powershell
libdragon exec python3 tools/rdp_log_to_hex.py capture.log frame.rdp   # keeps SET_Z_IMAGE..next frame
libdragon exec bash -c '$N64_INST/bin/rdpvalidate frame.rdp'        # validate
libdragon exec bash -c '$N64_INST/bin/rdpvalidate -d -t frame.rdp'  # disassemble incl. triangles
```

Reference result (2026-09-23, demo scene): 1,740 command words, 188 `TRI_Z`, 95 `TEX_RECT` (text glyphs), 63 `SET_OTHER_MODES`, 56 `SET_COMBINE_MODE`; **0 warnings, 0 errors**. Mode and combiner changes are a third of the frame's commands, which is where state-batching work (Phase 2/3) pays off.

### Golden captures of a benchmark (Phase 3 S1)

`libdragon make BENCH=1 BENCH_KIND=MESH BENCH_RDPLOG=1` builds a ROM that captures one frame of every step, 30 frames into its warm-up, each after a `BENCH_RDPLOG,<kind>,<index>,<param>` row. The camera path is frame-locked, so every build captures the same frame. Run it in ares with its output redirected (SETUP.md). The emulated console stops for a few seconds at every capture; Bench = Mesh's 18 steps take about 15 minutes. Then:

```powershell
libdragon exec python3 tools/rdp_log_to_hex.py build/ares.log build/rdplog --tagged --golden docs/benchmarks/2026-09-25-p3-s1-mesh-rdplog-ares.csv
```

`--tagged` writes one `<kind>-<param>.rdp` per step and a `manifest.csv`: per step, the triangle count per command and a SHA-256 of the triangle commands. Only triangles are compared, because the rest of the frame holds buffer addresses that move between builds and HUD text that changes with timing. `--golden` compares the manifest with a committed one and exits 1 when a step's triangles differ. The S1 set covers `TRI_Z`, `TRI_TEX_Z`, `TRI_SHADE_Z` and `TRI_TEX_SHADE_Z` and the sphere's curved groups. Three builds with different code layouts gave the same hashes.

## Crashes and assertions

libdragon's inspector takes over the screen on an exception or a failed `assertf()` and shows the message, the failed expression, the file/line/function and a **symbolized backtrace** (the `.sym` file is embedded in the ROM by n64.mk). The same text is printed to the debug log. Debug tab → **Crash Test → Assert!** triggers one on purpose; on the A3D it reports `file "src/debug/rdp_debug.c", line …, function: rdp_debug_crash_test` followed by the backtrace. Reset the console afterwards.

Crashes seen so far:

| Symptom | Cause | Fix |
|---|---|---|
| `ASSERTION FAILED: wav64 ...: invalid version` at boot | generated assets from another libdragon version | `libdragon make clean; libdragon make` |
| `RSP CRASH ... rspq_highpri_sync ... wait loop timed out` | RDP validator stopped mid-frame while the RSP was paused for a trace fetch | toggle at a frame boundary (fixed) |
| `RSP CRASH ... rspq_syncpoint_wait ... wait loop timed out (200 ms)` from `rdp_debug_frame_end`, RSP halted and idle | an RDP capture's print, inside libdragon's RSP interrupt, outlasted `rspq_wait()`'s 200 ms | wait for the RSP without a time limit first (fixed, Phase 3 S1) |
| RSP timeout in `display_get` | RDP pipeline misconfiguration | enable RDP Check, fix what it reports |

`debug_backtrace()` prints the current call stack at any point, and `rdpq_debug_get_tmem()` returns a 32×64 surface with the current TMEM contents (free it with `surface_free`).

## Unit tests

Modules without rendering dependencies have host tests in `tests/host`: vec3, collision (including sparse collider slots), physics (including kinematic bodies), scene objects (collider and body ownership, the per-frame sync, flag queries), the action layer (contexts, chords, players, analog), the menu model and the settings table, camera math and the camera's dirty/follow behaviour, frame-time statistics, mesh building and the built-in shapes (winding, planar groups), and the particle simulation. They are compiled with the host compiler against a small libdragon stand-in, `tests/host/shim/libdragon.h` (colour types, `debugf`/`assertf`, `TICKS_READ()`; the action layer takes `PadState` snapshots, so no joypad), with `-Wall -Werror`:

```powershell
libdragon exec make -C tests/host run      # 1280 checks, 0 failures (S9)
```

They run in CI on every push (`.github/workflows/build.yml` → `tools/ci_build.sh`, which also builds both ROMs, checks ROM/RAM budgets with `tools/rom_budget.py` and the I-cache layout of the render path with `tools/hot_text.py`). How to add a test: [EXTENDING.md](EXTENDING.md).
