# Benchmarks

Measured performance of the engine on real hardware and in ares. Every feature that changes performance adds a row here (ROADMAP_v2 principle 6: measure before and after). The benchmark scene and CSV compare tool arrive in ROADMAP_v2 P1.8; until then rows come from the in-engine profiler dump.

## How to capture

1. Build and upload the debug ROM: `libdragon make`, `sc64deployer upload engine-debug.z64`.
2. In a second terminal: `sc64deployer debug | Tee-Object capture.log` (PowerShell).
3. Reset the console, set up the view, wait 2–3 seconds for the averages to settle, then press **D-Down** (or Start → Debug → Dump CSV).
4. Each dump writes one `STATS` row and `PROF_AVG` / `PROF_PEAK` rows (microseconds). Column names are in the `STATS_HDR` / `PROF_HDR` lines printed with the first dump.

Definitions:
- **frame** = wall time per loop iteration (16.7 ms at 60 FPS).
- **wait_display** = time blocked in `display_get()` for a free framebuffer: idle headroom.
- **CPU** = frame − wait_display − limiter: CPU work per frame (the HUD's `CPU:` value).
- **avg** = exponential moving average over ~32 frames; **peak** = maximum since boot or Reset Peaks.
- Slot hierarchy: `update` ⊃ input, physics, particle_upd, scene_sys; `draw` ⊃ sky, floor, shadows, objects, particle_draw, hud, menu; `objects` ⊃ mesh_cull, mesh_light, mesh_tris.

## Environment

| Item | Value |
|---|---|
| Hardware | Analogue 3D (HDMI, 60 Hz 4K TV) via SummerCart64 fw v2.20.2 |
| Emulator | ares v148 (Homebrew Mode) |
| Toolchain | libdragon `10f3bd43e` (preview, 2026-02-27), GCC 16.2.0 |
| Debug ROM | 360,448 bytes (2026-09-23, P1.1) |
| Release ROM | 327,680 bytes |

## Baseline — CPU profile, demo scene (2026-09-23, commit after P1.3, debug build, Analogue 3D)

Default demo settings (no fog, no sky, no shadows, point lights off), camera at the start position. 252 triangles (floor 200, meshes 52), 6 texture uploads, 8 mesh draws, 19 face groups back-face culled.

| Phase | Validator off: avg ms | Validator on: avg ms |
|---|---|---|
| frame (wall) | 16.6 | 16.9–17.4 |
| wait_display (idle) | 8.6 | 3.9–4.2 |
| **CPU work** | **8.0** | **12.7–13.2** |
| update | 0.33 | 0.6–0.8 |
| — input / physics / particles / scene_sys | 0.05 / 0.02 / 0.02 / 0.17 | 0.2 / 0.03 / 0.06 / 0.2 |
| draw | 6.0 | 8.3–11.2 |
| — floor | 2.79 | 2.2–3.8 |
| — objects | 2.01 | 1.6–2.0 |
| — of which mesh triangle loop | 1.08 | 0.9–1.1 |
| — of which culling + lighting | 0.12 | 0.10 |
| — hud (6 text lines) | 1.13 | 4.0–5.2 |
| — sky / shadows | 0 / 0 (disabled) | 0 / 0 |
| audio (`snd_update`) | 1.30 (peak 6.7) | 0.65–0.78 |

Other measurements from the same session:

| Case | CPU ms | Notes |
|---|---|---|
| Particle burst (B), validator off | ~10.4 | 60–76 particles alive, 106–152 particle triangles; particles cost ≈2.3 ms |
| Particle burst, validator on | ~16.0 | wait_display 0.6–1.0 ms: at the edge of the frame budget |
| Menu open, validator on | 19 (user reading) | `menu` peak 6.1–9.0 ms in one frame → frame overruns → D18 flicker |
| Profiler off vs on (quiet view) | 7.9 vs 8.0 | profiler overhead ≈ 0.1 ms (P1.3 acceptance: ≤ 0.1 ms) |
| Peaks seen once after starting the validator | frame 118 ms, floor 99 ms | one-off stall; to be characterised with the frame-time histogram (P1.5) |

Takeaways for later phases:
- The **floor** (2.8 ms for 200 flat triangles) is the most expensive CPU item. Candidate fixes: vertex cache (P3.1), fewer or larger tiles, or a textured floor.
- **Text** is expensive: 1.1 ms for six HUD lines, and the menu costs several ms in a single frame. Text caching or fewer `rdpq_font_style` calls belong in Phase 2 (they are the root of D18's overrun).
- **Audio** averages 1.3 ms with 6.7 ms peaks on the CPU; worth a look when music is replaced by XM (P2 / audio work).

## Memory and frame pacing (2026-09-23, P1.4 / P1.5, debug build, Analogue 3D)

| Measurement | Value |
|---|---|
| RDRAM | 8,388,608 bytes: the Analogue 3D reports an Expansion Pak |
| Heap available to malloc | 7,860,744 bytes |
| Heap in use, demo scene after boot | 913,544 bytes |
| Heap after 5× Reset Scene | 1,014,432 bytes: **+101,616 B, ≈ 20 KB leaked per reset** (defect D1) |
| Framebuffers (3 × 320×240×16-bit) / Z-buffer | 460,800 / 153,600 bytes |
| Stack high-water mark | 3,096 bytes of 65,536 (4,520 with the RDP validator on) |

Frame-time window (256 frames), columns `FT`:

| Case | fps | CPU avg / max ms | Loop time histogram (1.5 ms buckets) |
|---|---|---|---|
| Quiet view | 60.1 | 7.3 / 11.7 | bimodal: 103 frames at 12–13.5 ms, 90 at 19.5–21 ms (avg 16.65) |
| After 5 resets | 60.0 | 11.4 / 26.5 | reload frames up to 26.5 ms |
| Menu open + RDP validator | 52.4 | 18.7 / 23.7 | CPU over budget in most frames (D18) |

Notes:
- The bimodal loop time at a steady 60 FPS is triple-buffer pacing: the loop waits for a free framebuffer, not for vsync, so iterations alternate short and long. Use `fps` and CPU time to judge load; the jitter itself is tracked as defect D19 because `dt` inherits it.
- `over_budget` counts frames whose CPU work exceeds the budget by 10 % (changed after this run; the first run counted wall time and flagged 102 of 256 healthy frames).

## Debug overlay cost (2026-09-23, P1.6, debug build, Analogue 3D)

Overlay pages drawn over the demo in a quiet view (demo HUD also on). `overlay` = PROF_OVERLAY average.

| Implementation | Profiler page | Stats | Memory | Frame | RSP (4 lines) | Off |
|---|---|---|---|---|---|---|
| One `text_draw` per line | 5.4–5.9 ms | 4.3 | 4.5 | 4.3 | — | 0.0 |
| One multi-line paragraph per page (`^xx` styles) | 4.5 | 4.6 | 4.0–4.2 | 4.1–4.4 | 2.9 | 0.02 |
| Paragraph built at 4 Hz, rendered every frame | 4.1 | 4.2 | 3.7–3.9 | 3.0–3.8 | 2.3–2.9 | 0.04 |

Findings:
- Text costs roughly 15–20 µs per glyph on the A3D with the builtin outlined debug font, and **most of it is not layout**: caching the built paragraph saved only ~0.4 ms. The cost is in issuing the glyphs (texture-rectangle commands and atlas loads), so the CPU is probably stalling on a full RSP command queue while the RDP draws. P1.7 (RSP/RDP profiling) is needed to separate CPU from RDP time.
- The same cost explains the demo HUD (1.1 ms for 6 lines) and the menu (D18). Candidate Phase 2 fixes: a non-outlined font for dense debug text, fewer glyphs, drawing the overlay at a lower rate into a cached surface, or blitting pre-rendered static labels.
- With a page up the demo still holds 60 FPS (CPU 10–11 ms); Overlay Off costs nothing.

## RDP load from hardware counters (2026-09-23, P1.7, debug build, Analogue 3D)

The RSP page reads the RDP's cycle counters (`DP_CLOCK`, `DP_BUSY`, `DP_PIPE_BUSY`, `DP_TMEM_BUSY`) every loop and scales them by the measured counter rate. **On the Analogue 3D the counters tick at 93.75 MHz** (1.5× the 62.5 MHz RCP clock); percentages are rate-independent. `RDP` rows in the CSV dump.

| Case | CPU ms | RDP busy ms | RDP pipe ms | TMEM ms | RDP busy % |
|---|---|---|---|---|---|
| Quiet view, overlay off | 8.8 | 5.4 | 1.7 | 0.10 | 32 |
| Profiler page up | 11.6 | 7.2 | 2.2 | 0.11 | 44 |
| RSP page up | 11.0 | 6.3 | 1.9 | 0.11 | 38 |
| Particle burst (39 alive, 387 tris) + RSP page | 14.7 | 6.9 | 2.0 | 0.11 | 41 |
| Menu open (then closed) + RSP page | 13.3 | 6.9 | 2.2 | 0.16 | 42 |

Findings:
- **The engine is CPU-bound, not RDP-bound.** RDP busy never exceeds ~7 ms of the 16.7 ms frame; CPU work is what approaches the budget. Phase 2/3 optimisation should target CPU: text issue (overlay +4 ms CPU vs +1.8 ms RDP), the floor, particles.
- `pipe` (pixel pipeline active) is only ~30 % of `busy`: most RDP busy time is command/memory overhead rather than filling pixels, so fill rate has lots of headroom.
- TMEM loading is negligible (~0.1 ms) at the demo's 6 uploads per frame.

## Benchmark scene (P1.8)

### Running it

1. Build and upload the **debug** ROM (`engine-debug.z64`). Release builds compile `debugf` out, so they print no CSV. Both variants are `-O2`; the debug build adds asserts and profiler scopes, measured at ~0.1 ms, so its numbers stand for release.
2. Start the capture: `sc64deployer debug | Tee-Object docs/benchmarks/<date>-<label>.log` (the SC64 must not be busy with another `debug` session).
3. Reset the console, then Start → **Debug** → **Bench** (All or one test) → **Scene = Benchmark** → A. The scene fades in, runs every step (60 warm-up + 240 measured frames each, fixed camera path), and fades back to the demo. Start aborts.
4. For unattended runs, `libdragon make BENCH=1` boots straight into "All".
5. Keep only the `BENCH` lines (`Select-String '^BENCH' capture.log | % Line > file.csv`) and compare:

```powershell
python tools/bench_compare.py docs/benchmarks/2026-09-23-baseline-debug-a3d.csv new.csv
# exit 0 = OK, 1 = regression (CPU +5 % and +0.15 ms, or a step that held 60 FPS no longer does)
```

With the profiler on (the debug default), each step also prints a `BENCH_PROF` row: the moving-average µs of `update`, `draw`, `objects`, `mesh_cull`, `mesh_light` and `mesh_tris`, to pin a CPU regression to a stage of `mesh_draw`. `bench_compare.py` ignores these rows.

The tests, all on a dark background without floor, sky or fog (demo settings are restored afterwards):

| Bench | Steps | Load |
|---|---|---|
| all step 0 | — | empty scene: engine + status text baseline |
| objects | 8, 16, 24, 32, 48, 64 | flat-shaded pillars (32 tris each) on a grid |
| particles | 32, 64, 96, 128 | target particle counts from 4 continuous additive emitters |
| lights | 0, 1, 2, 4 | 16 pillars under N coloured point lights (sun and ambient dimmed) |
| textures | 1, 2, 4, 8 | 16 boxes cycling through N distinct 32×32 RGBA16 textures |
| shadows | 0, 1, 2 | 16 pillars with shadows off / blob / projected |
| fillrate | 1, 2, 4, 8 | N full-screen blended rectangles (RDP read-modify-write) |

### Baseline (2026-09-23, debug build, Analogue 3D, `docs/benchmarks/2026-09-23-baseline-debug-a3d.csv`)

| Bench | Param | FPS | 1 % low | CPU avg / max ms | RDP busy ms (%) | Tris | Uploads |
|---|---|---|---|---|---|---|---|
| empty | 0 | 60.0 | 55.5 | 1.20 / 3.7 | 1.1 (7) | 0 | 0 |
| objects | 8 | 60.0 | 54.6 | 4.53 / 7.8 | 2.4 (14) | 128 | 0 |
| objects | 16 | 60.0 | 51.0 | 7.88 / 11.9 | 3.6 (22) | 256 | 0 |
| objects | 24 | 59.9 | 48.0 | 11.35 / 16.3 | 5.0 (30) | 384 | 0 |
| objects | 32 | **54.6** | 42.8 | 18.09 / 23.4 | 6.2 (33) | 512 | 0 |
| objects | 48 | **36.7** | 28.4 | 27.27 / 35.3 | 8.9 (32) | 768 | 0 |
| objects | 64 | **27.6** | 21.4 | 36.21 / 46.7 | 11.2 (31) | 1024 | 0 |
| particles | 32 (27 alive) | 60.0 | 57.6 | 2.59 / 4.9 | 1.3 (8) | 55 | 0 |
| particles | 64 (58) | 60.0 | 57.2 | 4.07 / 6.3 | 1.4 (8) | 117 | 0 |
| particles | 96 (89) | 60.0 | 57.0 | 5.60 / 8.0 | 1.5 (9) | 178 | 0 |
| particles | 128 (121) | 60.0 | 56.6 | 7.21 / 9.7 | 1.6 (10) | 242 | 0 |
| lights | 0 | 60.0 | 51.0 | 7.91 / 12.1 | 3.7 (22) | 256 | 0 |
| lights | 1 | 60.0 | 51.0 | 8.06 / 12.2 | 3.6 (22) | 256 | 0 |
| lights | 2 | 60.0 | 51.1 | 8.13 / 12.1 | 3.6 (22) | 256 | 0 |
| lights | 4 | 60.0 | 51.1 | 8.37 / 12.5 | 3.7 (22) | 256 | 0 |
| textures | 1 | 60.0 | 54.5 | 7.54 / 10.8 | 2.5 (15) | 96 | 48 |
| textures | 8 | 60.0 | 54.6 | 7.72 / 9.8 | 2.5 (15) | 96 | 48 |
| shadows | off | 60.0 | 51.0 | 8.06 / 12.1 | 3.7 (22) | 256 | 0 |
| shadows | blob | 60.1 | 48.0 | 8.99 / 14.0 | 4.9 (29) | 288 | 0 |
| shadows | projected | **40.5** | 32.0 | 24.70 / 31.2 | 7.2 (29) | 768 | 0 |
| fillrate | 1 | 60.0 | 57.1 | 1.14 / 3.8 | 2.8 (17) | 0 | 0 |
| fillrate | 2 | 60.0 | 57.1 | 1.14 / 3.7 | 4.5 (27) | 0 | 0 |
| fillrate | 4 | 60.0 | 57.1 | 1.15 / 3.6 | 7.9 (48) | 0 | 0 |
| fillrate | 8 | 60.0 | 56.9 | 1.15 / 3.7 | 14.7 (88) | 0 | 0 |

(textures 2 and 4 match 1 and 8 within 0.2 ms; full rows in the CSV.)

What the baseline says about the CPU path:
- **Object ceiling ≈ 24–28 pillars (~400–450 triangles) at 60 FPS**, CPU-bound: ~0.42 ms CPU per 32-triangle pillar (≈13 µs per triangle), with the RDP only ~33 % busy. This is the number Tiny3D (Phase 4) and the vertex cache (P3.1) must beat.
- **Particles** cost ~1.5 ms CPU per 30 particles (≈50 µs each) and little RDP.
- **Point lights** are cheap on this path (~0.12 ms each for 16 objects) because lighting is per face group; per-vertex lighting (P3.2) will change that.
- **Textures:** 48 uploads per frame whatever the number of distinct textures, because every visible face re-uploads (no residency, P3.3). TMEM upload cost is low at this scale.
- **Projected shadows** are the most expensive feature measured: +16.6 ms CPU for 16 casters (defect D14). Blob shadows cost ~0.9 ms.
- **Fill rate:** ~1.7 ms of RDP per full-screen blended layer; 8 layers use 88 % of the RDP and still hold 60 FPS. The RDP has plenty of headroom for post-effects; the CPU is the bottleneck.
- The 1 % lows (≈51–58 fps even when the average is 60) reflect the loop-pacing jitter in D19, not dropped frames.

## Phase 2 kickoff (2026-09-23)

Phase 2 (engine hardening, ROADMAP_v2 §6) is measured against these start references:

| Reference | Value |
|---|---|
| Benchmark baseline | `docs/benchmarks/2026-09-23-baseline-debug-a3d.csv` (26 steps, debug build, A3D) |
| Demo quiet-view CPU | 8.0 ms (floor 2.8, objects 2.0, audio 1.3, HUD 1.1) |
| Demo heap after init | 913,544 bytes |
| Reset Scene leak (D1) | ~20 KB per reset |
| Menu (validator on) | `menu_draw` peaks 6–9 ms |

Each Phase 2 stage commits its own CSV as `docs/benchmarks/<date>-p2-s<N>-debug-a3d.csv`; it becomes the comparison point for the next stage (moving baseline). The exit comparison (S13) is against the 2026-09-23 baseline.

## Phase 2 · S0 measurement prep (2026-09-23, debug build, Analogue 3D)

New tools: **Reset Soak** and **Menu Sweep** (Debug tab, `src/debug/testbed.c`), the **Overload** benchmark kind, and a static sphere in the demo (the S2 shading test object; it adds ~1 ms of CPU for its 60 triangles).

| Check | Result |
|---|---|
| Reset Soak (1 warm-up + 10 resets) | heap 960,488 → 1,095,368 B: **+134,880 B, 13.5 KB per reset** (D1) |
| Menu Sweep with RDP Check on | 24 items, 123 options, no crash; no validator warnings except the known 15 start-up artifacts |
| Demo dumps with the sphere | objects 3.05 ms (was 2.0), floor 2.87, HUD 1.4, menu 2.6 avg while open; heap after boot 913,552 B |

Overload (`docs/benchmarks/2026-09-23-p2-s0-overload-debug-a3d.csv`, validator off; a validator-on run is in `...-overload-validator-debug-a3d.csv`): floor + 16 pillars + a fixed CPU burn.

| Burn | FPS | CPU avg / max ms | RDP busy ms | Flicker on the A3D |
|---|---|---|---|---|
| 0 | 60.0 | 11.2 / 17.1 | 5.9 | no |
| +10 ms | 46.3 | 21.6 / 27.0 | 6.0 | no |
| +14 ms | 39.1 | 25.6 / 30.9 | 6.0 | no |
| +17 ms | 34.8 | 28.7 / 34.0 | 5.9 | no |
| +20 ms | 30.9 | 32.4 / 37.1 | 6.0 | no |
| +25 ms | 26.4 | 37.8 / 42.0 | 6.0 | no |

Findings:
- **Frame overruns alone do not flicker** on the A3D, with or without the validator. D18 needs the menu to be on screen during the overrun, so the menu drawing path (translucent background triangles, text) is the suspect, not the frame loop.
- Under overload the frame rate degrades smoothly (46 → 26 FPS) instead of snapping to 30/20: with triple buffering the loop never waits for vsync. Relevant to D19 and the S6 pacing work.

## Phase 2 · S1 resource lifecycle (2026-09-23, debug build, Analogue 3D)

The demo now declares its eight textures (six cube faces, two billboards) in `Scene.texture_paths`; `scene_init`/`scene_cleanup` load and free them. `texture_init()` is idempotent and no longer touches slots above 5.

| Check | Result |
|---|---|
| Reset Soak (1 warm-up + 10 resets), run 1 | heap 946,992 → 946,992 B: **0 B** (was +13.5 KB per reset, D1) |
| Bench = Textures, then back to the demo | CPU 7.66–7.93 ms vs baseline 7.54–7.72, 60 FPS; demo textures intact after the scene switch (D21) |
| Reset Soak, run 2 (after the scene switch) | 946,992 → 946,992 B: **0 B** |

Heap after the warm-up reset is 13.5 KB lower than in S0 (960,488 B), because the stale cube sprites are gone.
