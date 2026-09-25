# Benchmarks

Measured performance of the engine on real hardware and in ares. Every feature that changes performance adds a row here (ROADMAP_v2 principle 6: measure before and after). Comparable numbers come from the benchmark scene ([Benchmark scene](#benchmark-scene-p18)); demo-view measurements come from Dump CSV rows (below).

## How to capture

1. Build and upload the debug ROM: `libdragon make`, `sc64deployer upload engine-debug.z64`.
2. In a second terminal: `sc64deployer debug | Tee-Object capture.log` (PowerShell; Windows PowerShell 5.1 writes the file as UTF-16, which the Python tools in `tools/` read).
3. Reset the console, set up the view, then press **D-Down** (or Start → Debug → Dump CSV). The dump is written 120 frames (~2 s) later, once the ~32-frame averages have settled without the menu; keep the view steady until it appears.
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
4. For unattended runs, `libdragon make BENCH=1` builds a ROM that boots straight into "All" (or into one kind with `BENCH_KIND=<kind>`, e.g. `AUDIO`): `engine-debug-bench.z64`, built in its own directory (`build/debug-bench/`), so the normal build and ROM are untouched and no clean is needed. Upload that ROM instead. On the A3D every reset is followed by ~7.5 s of slower CPU, RSP and RDP (D32), so such a run first holds its first step's scene, unmeasured, until 15 s after reset ("settling", `BENCH_SETTLE` rows).
5. Compare the capture with the baseline. `bench_compare.py` reads the `BENCH` step rows (and `BENCH_META` as a label) and ignores every other line, so a raw capture works; it reads UTF-8 and UTF-16 files. To keep just the benchmark rows: `Select-String '^BENCH' capture.log | % Line > new.csv`.

```powershell
py tools/bench_compare.py docs/benchmarks/2026-09-23-baseline-debug-a3d.csv new.csv
# or without host Python: libdragon exec python3 tools/bench_compare.py <baseline> <new>
# exit 0 = OK, 1 = regression (CPU +5 % and +0.15 ms, or a step that held 60 FPS no longer does), 2 = no BENCH rows / bad file
# --prof mesh_tris,particle adds the named BENCH_PROF slots per step, with µs per drawn triangle (not gated)
```

Timings depend on code and data layout (D25, D26): compare runs of the same day where possible, and compare renderer changes inside one ROM when a result is close to the limit.

With the profiler on (the debug default), each step also prints a `BENCH_PROF` row: the moving-average µs of `update`, `draw`, `objects`, `mesh_cull`, `mesh_light`, `mesh_tris`, `audio`, `menu`, `hud`, `dialog`, `input`, `wait_input`, `particle_draw` and `rsp_wait`, to pin a CPU regression to a stage of `mesh_draw` (or to the mixer, or to waiting for the RSP). Once per run a `BENCH_LAYOUT` row logs the addresses of `bench_draw`'s stack frame and of the pillar mesh's vertex and index arrays, to relate timing changes to D-cache aliasing (D26). `bench_compare.py` ignores both row types.

The tests run with fog and sky off (restored afterwards) on the benchmark scene's dark background; only Overload draws the floor. "All" runs the empty scene and the objects, particles, lights, textures, shadows and fill-rate steps, 28 in total; the other kinds run on their own:

| Bench | Steps | Load |
|---|---|---|
| all step 0 | — | empty scene: engine + status text baseline |
| objects | 8, 16, 24, 32, 48, 64 | flat-shaded pillars (32 tris each) on a grid |
| particles | 32, 64, 96, 128, 1128, 2128 | 4 continuous emitters; param = blend × 1000 + particles: additive, all alpha (1128), two of each (2128) |
| lights | 0, 1, 2, 4 | 16 pillars under N coloured point lights (sun and ambient dimmed) |
| textures | 1, 2, 4, 8 | 16 boxes cycling through N distinct 32×32 RGBA16 textures |
| shadows | 0, 1, 2 | 16 pillars with shadows off / blob / projected |
| fillrate | 1, 2, 4, 8 | N full-screen blended rectangles (RDP read-modify-write) |
| layout | 32, 64 × variants 0–5 | data-placement check (D26): the shared pillar (0), copies at D-cache colours 0/2/4/6 KB (1–4), a reversed-winding copy (5); param = variant × 1000 + pillars. Debug only, not in All |
| overload | 0, 10, 14, 17, 20, 25 | floor + 16 pillars + N ms of CPU busy-wait per frame: deliberate overruns, for the D18 flicker (not in All) |
| ui | 0, 10, 1, 11, 2, 12, 3, 13, 4, 14, 20, 30, 40, 41, 50, 51 | floor + 16 pillars with UI on top. 0–14: the Start menu (a copy), param = mode × 10 + input (mode 0 direct, 1 cached; input 0 none, 1 cursor every 8 frames, 2 value every 2, 3 tab every 30, 4 closed 30 of every 100 frames). 20 / 30: the cached menu in the Classic / Minimal style. 40 / 41: a demo-like HUD (title + six readouts changing every frame) drawn direct every frame / cached at ~6 Hz. 50 / 51: the demo conversation in the text box at reading pace / skipping. Not in All ([UI.md](UI.md)) |
| audio | 8 steps (10 with `SND_OPUS=1`) | floor + 16 pillars with the music at full volume; param = codec × 100 + poll point × 10 + effects (codec 0 none, 1 raw, 2 VADPCM, 3 Opus; poll point 0 after present, 1 before `display_get`, 2 after; effects 1 = a new sound every 4 frames). Opus runs first, so VADPCM later reuses its channel (D31). Steps whose track is not in the ROM are skipped; not in All ([AUDIO.md](AUDIO.md)) |
| latency | 0, 100, 200, 4, 104, 204, 8, 108, 208 | floor + 16 pillars + a CPU burn; param = latency setting × 100 + burn ms (0 Classic, 1 Low, 2 Lowest): input lag per setting at three loads. Not in All ([INPUT.md](INPUT.md)) |
| mesh | 16, 32, 64 × variants 0–5 | `mesh_draw`'s costs (Phase 3 S1); param = variant × 1000 + objects: pillars (0), pillars not submitted (1), spheres (2), spheres not submitted (3), pillars, spheres and textured boxes in turn (4), the same with fog (5). A not-submitted variant transforms, culls and lights every triangle but skips `rdpq_triangle()` (`mesh_debug_set_skip_submit`, debug builds): the difference in `mesh_tris` is the cost of submitting. Variants 4 and 5 use all four triangle formats (golden RDP captures, [DEBUGGING.md](DEBUGGING.md)). Not in All |

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

## Phase 2 progress snapshot: baseline → S6.3 (2026-09-24, debug build, Analogue 3D)

Bench = All at the start of Phase 2 (`docs/benchmarks/2026-09-23-baseline-debug-a3d.csv`) against the build after S6.3 (`docs/benchmarks/2026-09-24-p2-s6_3-all-bootbench-debug-a3d.csv`, a boot-to-benchmark run with the D32 settle and the profiler **on**, which costs ~0.1 ms the baseline did not pay). `bench_compare.py`: **all 26 steps improved, 0 regressions.**

| Step | FPS | CPU avg ms | CPU change | RDP busy ms | 1 % low FPS |
|---|---|---|---|---|---|
| empty scene | 60 → 60 | 1.20 → 0.88 | −27 % | 1.11 → 0.93 | 55.5 → 59.3 |
| objects 8 | 60 → 60 | 4.53 → 3.92 | −13 % | 2.36 → 2.20 | 54.6 → 59.2 |
| objects 16 | 60 → 60 | 7.88 → 6.68 | −15 % | 3.63 → 3.44 | 51.0 → 59.5 |
| objects 24 | 59.9 → 60 | 11.35 → 9.42 | −17 % | 4.95 → 4.64 | 48.0 → 59.2 |
| **objects 32** | **54.6 → 60** | 18.09 → 12.60 | **−30 %** | 6.16 → 5.86 | 42.8 → 59.2 |
| objects 48 | 36.7 → 40.3 | 27.27 → 24.82 | −9 % | 8.85 → 8.44 | 28.4 → 39.9 |
| objects 64 | 27.6 → 30.3 | 36.21 → 32.97 | −9 % | 11.19 → 11.00 | 21.4 → 30.0 |
| particles 32 | 60 → 60 | 2.59 → 2.06 | −21 % | 1.28 → 1.10 | 57.6 → 59.2 |
| particles 64 | 60 → 60 | 4.07 → 2.80 | −31 % | 1.37 → 1.19 | 57.2 → 59.1 |
| particles 96 | 60 → 60 | 5.60 → 3.56 | −36 % | 1.47 → 1.29 | 57.0 → 59.2 |
| **particles 128** | 60 → 60 | 7.21 → 4.35 | **−40 %** | 1.60 → 1.40 | 56.6 → 59.1 |
| lights 0 / 1 / 2 / 4 | 60 → 60 | 7.91–8.37 → 6.69–7.33 | −12..−15 % | 3.64–3.72 → 3.46 | 51.0 → 59.4 |
| **textures 1 / 2 / 4 / 8** | 60 → 60 | 7.54–7.72 → 5.23–5.49 | **−29..−31 %** | 2.45 → 1.94 | 54.5 → 59.2–59.5 |
| shadows off | 60 → 60 | 8.06 → 6.72 | −17 % | 3.68 → 3.50 | 51.0 → 59.4 |
| shadows blob | 60 → 60 | 8.99 → 7.32 | −19 % | 4.85 → 4.66 | 48.0 → 59.4 |
| **shadows projected** | **40.5 → 60** | 24.70 → 11.13 | **−55 %** | 7.21 → 5.25 | 32.0 → 59.3 |
| fill rate 1 / 2 / 4 / 8 layers | 60 → 60 | 1.14–1.15 → 0.90–0.91 | −21 % | 2.82–14.71 → 2.66–14.52 | 57.1 → 59.3 |

**Headlines**
- **Object ceiling at 60 FPS: ~24–28 → 32+ pillars** (512 triangles): objects 32 went from 54.6 FPS to a steady 60 with 4 ms to spare.
- **Projected shadows: 40.5 → 60 FPS** for 16 casters (S3: vertices transformed once per caster and the caster frustum test; S6.3: the shadow scratch off the stack's cache lines).
- **Particles −21 to −40 %** (S3: emitter index instead of a scan; S6.3: the pool off the stack's cache lines, D33).
- **Textures −30 %** (S3: no re-upload of a texture already in TMEM; 48 → 16 uploads per frame in this bench).
- **Smoothness:** every step that runs at 60 FPS now shows all 239 measured frames for exactly one vblank (`BENCH_PRESENT`, S6.2), and the 1 % lows rose from 48–57.6 to 59.1–59.5 FPS. Over budget, frames repeat in whole vblanks: objects 48 averages 1.49 vblanks per frame, objects 64 1.98.
- **Reproducible numbers:** builds that differ only in unrelated code now agree within ±0.5 % (S6.3, D34); before, mesh-heavy steps moved 7–12 %.

**Outside the benchmark** (stage sections below):

| Measure | Phase 2 start | Now | Stage |
|---|---|---|---|
| Reset Scene leak (Reset Soak ×10) | ~13.5–20 KB per reset | **0 B** | S1 |
| Demo heap (Reset Soak `heap_before`) | 946,992 B (S1) | 843,336 B (−101 KB) | S4 |
| Start menu, open and idle | 4.07 ms per frame | **0.31 ms** | S5.1 |
| Demo HUD | 1.1–1.4 ms | ~0.6 ms (cached, with shadow) | S5.2 |
| Audio with music playing | 1.3 ms (2.69 avg, 10.2 peak and dropped frames after the S4b.1 upgrade) | 0.46 ms (VADPCM, mixing right after `display_get`) | S4b.2 |
| Dialog text box | — | 0.30 ms reading, 0.53 ms skipping | S5.3 |
| Frame pacing (D19) | loop 12.5 / 21 ms, dt jittered | dt from the display; 256/256 demo frames on time | S6.2 |
| Slow start after reset (D32) | unexplained | A3D/cart: ~7.5 s at −35..−47 %; boot runs settle 15 s | S6.2 |

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

## Phase 2 · S2 renderer correctness (2026-09-23, debug build, Analogue 3D)

Spheres no longer vanish (D24). Flat face groups are culled by one exact plane test and lit once; curved groups (sphere bands) are culled per triangle by screen winding and lit per triangle. With the sphere in view the Stats page shows 30–34 triangles culled by the winding test (`bf`).

**The first S2 build was 17–20 % slower in the Objects benchmark**, with the same triangle counts and RDP time. Separate ROMs could not say why:

| Build | Objects CPU ms, 8 / 64 pillars |
|---|---|
| Phase 1 baseline | 4.53 / 36.21 |
| S2, first build | 5.33 / 42.54 |
| S1, rebuilt the same day | 4.37 / 34.82 |
| S1 plus the `BENCH_PROF` logging only | 4.64 / 36.91 |

The last two builds differ only in benchmark logging, yet CPU time moved 6 %: timings depend on code layout. A same-ROM A/B (Mesh A/B: the new renderer, the S1 renderer, and the new renderer on a second copy of the pillar data, interleaved) settled it. CPU ms:

| Pillars | New | S1 renderer | New, other data copy |
|---|---|---|---|
| 16 | 11.36 | 8.36 | 10.70 |
| 32 | 24.72 | 17.66 | 23.26 |
| 64 | 49.07 | 35.54 | 46.10 |

`BENCH_PROF` put the loss in the triangle loop (33.2 vs 21.2 ms at 64 pillars for the same triangles). The link map explained it: the new loop sat a multiple of 16 KB away from `rdpq_triangle_rsp` and shared all 46 of its I-cache lines, so each evicted the other on every triangle (D25). After the hot-text fix (HARDWARE.md), same ROM:

| Pillars | New | S1 renderer | New, other data copy |
|---|---|---|---|
| 16 | 8.49 | 8.19 | 7.79 |
| 32 | 18.78 | 17.25 | 16.90 |
| 64 | 37.41 | 34.82 | 34.07 |

The triangle loops now cost the same (20.9 vs 20.4 ms at 64 pillars). What remains is per-group work plus data layout: identical code on two copies of the same data differs by up to 9 % (D26, S3).

**Gate** against the Phase 1 baseline, `bench_compare.py`: 0 regressions. This run is the comparison point for S3 (`docs/benchmarks/2026-09-23-p2-s2-objects-debug-a3d.csv`).

| Pillars | CPU ms, baseline → S2 | FPS |
|---|---|---|
| 8 | 4.53 → 4.65 (+2.6 %) | 60 |
| 16 | 7.88 → 8.08 (+2.5 %) | 60 |
| 24 | 11.35 → 11.65 (+2.6 %) | 60 |
| 32 | 18.09 → 18.45 (+2.0 %) | 54.6 → 53.4 |
| 48 | 27.27 → 27.90 (+2.3 %) | 36.7 → 35.8 |
| 64 | 36.21 → 37.12 (+2.5 %) | 27.6 → 26.9 |

The Mesh A/B runs are in `2026-09-23-p2-s2-meshab-collision-debug-a3d.csv` (before the fix) and `...-meshab-hottext-...` (after). The A/B code itself is in commit 44203e6, so the measured build can be rebuilt.

## Phase 2 · S3 CPU hot paths (2026-09-23, debug build, Analogue 3D)

Full benchmark (All) against the Phase 1 baseline, `bench_compare.py`: **0 regressions**. `docs/benchmarks/2026-09-23-p2-s3-all-debug-a3d.csv` is the comparison point for S4.

| Target | Baseline → S3 CPU ms | Change | Acceptance |
|---|---|---|---|
| Shadows, projected (16 casters) | 24.70 → 13.20 (40.5 → 60 FPS) | **−46.6 %**; the shadow pass itself 16.6 → 5.1 ms (−69 %) | −40 % ✓ |
| Particles, 128 | 7.21 → 4.75 | **−34.1 %** (32/64/96: −14 / −24 / −31 %) | −20 % ✓ |
| Demo `update` (quiet view) | 0.33 → 0.22 | **−34 %**; `scene_sys` (camera + collision) 0.17 → 0.04 | −20 % ✓ |
| Textures, 1–8 | 7.54–7.72 → 5.80–6.04 | **−22 to −23 %** (uploads 48 → 16 per frame) | no regression ✓ |
| Objects, 8–64 | | −0.2 to −3.3 % | no regression ✓ |
| Everything else | | within ±2 % | ✓ |

What made the difference:
- **Projected shadows:** one composed matrix per caster (`VP × floor projection × model`), each vertex projected at most once, only light-facing faces drawn (flat groups tested once with their normal, curved groups by projected winding), and the whole shadow culled when off screen. Shadow triangles for 16 pillars: 512 → 256.
- **Particles:** the update walks each emitter's own pool slice (no per-particle owner search, constants hoisted); the renderer projects each particle's centre once, since a camera-facing quad is a screen-aligned square at one depth.
- **Demo update:** the camera rebuilds only when something changes (D6); collision scans stop at the highest active slot; menu settings are applied on change (D23).
- **Textures:** the first S3 build measured the Textures bench **+15 %** (7.54 → 8.72 ms, `...-p2-s3-first-build-all-...`). Each textured face group ran libdragon's sprite upload (~7 KB of code) between triangle batches, evicting ~70 lines of `mesh_draw` and lighting from the I-cache, about 25 µs per upload. Pinning that code into the hot-text block does not fit (the mesh phase would need ~17 KB and collide with `rdpq_triangle_rsp`), so `mesh_draw` now skips a texture already uploaded earlier in the same draw. The same-texture faces of each box share one upload.

Demo dump (`...-p2-s3-demo-dump-...`, taken with D-Down after boot): `update` 0.22 ms (input 0.05, physics 0.02, particles 0.04, scene_sys 0.04). The view at that moment drew 174 triangles instead of the usual 252 (the camera had moved), so its draw times are not compared with the baseline.

**D26 (data layout), not resolved.** `BENCH_LAYOUT` rows: in the first S3 build the pillar's vertex and index arrays (`0x8016FAF0`, `0x80173AF8`) shared D-cache sets with the render stack (`0x807FFCF8` and below); in the final build (`0x8016EAA0`, `0x80172AA8`) they did not. The two builds' Objects results differ by at most 3 %, so stack/vertex aliasing does not explain the 9 % seen in the S2 Mesh A/B.

## Phase 2 · S4 mesh right-sizing (2026-09-23, debug build, Analogue 3D)

Every mesh used to reserve 512 vertices + 1024 indices (18 KB) on its first vertex (D8). Build arrays now grow as needed and `mesh_finalize()` packs each mesh into one exact-size, 16-byte aligned block.

| Check | Result |
|---|---|
| Demo heap (Reset Soak `heap_before`, same measurement as S1) | 946,992 → 843,336 B: **−101 KB** (target −90 KB) |
| Benchmark scene heap (`heap_kb` column) | 1,049 → 828 KB (**−221 KB**: its 8 texture boxes too) |
| Reset Soak ×10 | delta **0 B** |
| Full benchmark vs S3 (`bench_compare.py`) | **0 regressions**, every step within ±5 % (objects −0.3 to +1.2 %) |

`docs/benchmarks/2026-09-23-p2-s4-all-debug-a3d.csv` is the comparison point for the next stage.

**D26, data placement (Bench = Layout, `...-p2-s4-layout-...`).** Same code, same pillar data, placed at chosen D-cache colours (address modulo the 8 KB direct-mapped D-cache). CPU ms:

| Variant | Geometry sets | `Mesh` struct sets | 32 pillars | 64 pillars |
|---|---|---|---|---|
| 1: copy at 0 KB | 0x0000–0x0700 | 0x0C88–0x0FD8 | 16.75 | 33.76 |
| 2: copy at 2 KB | 0x0800–0x0F00 | 0x0FA4–0x12F4 | **16.12** | **33.15** |
| 3: copy at 4 KB | 0x1000–0x1700 | 0x12C0–0x1610 | 16.64 | 33.49 |
| 4: copy at 6 KB | 0x1800–0x1F00 | 0x15DC–0x192C | 17.39 | 34.88 |
| 5: reversed winding at 0 KB | 0x0000–0x0700 | 0x18F8–0x1C48 | 17.69 | 35.34 |
| 0: the shared pillar | 0x15B0–0x1CB0 | 0x17D0–0x1B20 | **18.04** | **35.96** |

The render stack sits just below `0x807FFCF0` (`bench_draw` frame 280 B, `mesh_draw` 688 B, `rdpq_triangle` 48 B), on D-cache sets **≈0x1880–0x1CF0**. Every slow variant has its geometry or its `Mesh` struct (face groups, read per group) on those sets; the three copies that avoid them are the fastest. Aliasing between hot mesh data and the render stack costs **4–8.5 %** on object-heavy frames; the shared pillar, which aliases with both, is the worst case (and is what the Objects bench measures). The winding effect cannot be separated from variant 5's struct placement. Mitigation belongs with the vertex cache (P3.1): the triangle loop would read one static transformed-vertex buffer at a known colour instead of mesh data, and placement can be checked with this bench.

**RSP crash (D28).** After the Reset Soak, with the menu open, the console stopped with `rspq_highpri_sync ... wait loop timed out` (status 0x3403). The dump shows the RSP asleep at its idle `break` with HIGHPRI_RUNNING still set after the high-priority epilogue ran: a race between the audio mixer's high-priority work and a low-priority buffer switch in the pinned libdragon. Fixed upstream in `7c57c409d` (2026-08-23, "rspq: make lowpri buffer handoff atomic"), which depends on the April rspq rework `cd9d88c64`; the libdragon upgrade is scheduled as its own stage (S4b).

## Phase 2 · S4b.1 libdragon upgrade (2026-09-23, debug build, Analogue 3D)

libdragon `10f3bd43e` (2026-02-27) → `39d0d6096` (preview, 2026-09-15; 466 commits), toolchain image `:latest` (2026-08-29) → `:preview` (GCC 16.2, binutils 2.45, `-mfix4300`). The engine compiled without changes or warnings; assets were regenerated (sprites compressed, music VADPCM).

| Check | Result |
|---|---|
| Full benchmark vs S4 (`bench_compare.py`, `...-p2-s4b1-all-...`) | **0 regressions**; objects −4 to −5 % at 24–64 pillars, everything else within ±5 % |
| Reset Soak ×10 | delta **0 B** (heap after init 836,856 B, −6.5 KB vs S4) |
| Menu Sweep | 24 items, 123 options, no crash |
| RAM footprint (release ELF) | 453.5 → 502.0 KB (+48.5 KB: code +41 KB, bss +7 KB; mostly the Opus/ULC codecs that `wav64` now always links) |
| ROM (release) | 352 → 400 KB |
| Demo `audio` slot | **2.69 ms average, 10.2 ms peak** with the music playing: the music is now VADPCM (the new `audioconv64` default) and is decoded on the RSP while the mixer waits. Addressed by the S4b.2 audio rework |

## Phase 2 · S4b.2 sound module rework (2026-09-23, debug build, Analogue 3D)

The `snd_*` module was rewritten ([AUDIO.md](AUDIO.md)):
- music crossfades, and stops decoding while muted;
- eight effect voices steal by priority;
- positional effects (the physics ball's bounces now make a sound);
- master, music and SFX volumes ramp;
- the mixing runs right after `display_get()` instead of after `rdpq_detach_show()`.

The new Audio benchmark kind measures the mixer per encoding and per poll point.

**Bench = Audio** (`docs/benchmarks/2026-09-23-p2-s4b2-audio-debug-a3d.csv`): floor + 16 pillars, music at full volume. `audio` is the profiler slot's average per frame:

| Param | Music | Poll point | Effects | `audio` ms | CPU avg / max ms | p99 ms | 1 % low FPS |
|---|---|---|---|---|---|---|---|
| 20 | none | after `display_get` | — | 0.09 | 9.78 / 11.9 | 17.1 | 58.5 |
| 200 | VADPCM | after present (old) | — | 2.07 | 11.81 / 16.7 | **21.5** | **46.4** |
| 210 | VADPCM | before `display_get` | — | 3.34 | 13.05 / 14.6 | 17.0 | 58.7 |
| 220 | VADPCM | after `display_get` (default) | — | **0.48** | 10.26 / 12.5 | 17.6 | 56.8 |
| 100 | raw PCM | after present | — | 1.81 | 11.79 / 16.7 | 21.6 | 46.3 |
| 120 | raw PCM | after `display_get` | — | 0.52 | 10.30 / 12.5 | 17.7 | 56.4 |
| 201 | VADPCM | after present | every 4 frames | 1.99 | 12.09 / 16.8 | 21.7 | 46.1 |
| 221 | VADPCM | after `display_get` | every 4 frames | 0.94 | 10.74 / 13.3 | 18.5 | 53.9 |

- **The poll point decides the cost.** With music, mixing after `display_get()` costs 0.48 ms per frame, against 2.07 ms after present and 3.34 ms before `display_get`.
  - The CPU waits for each buffer's high-priority RSP job. At 60 FPS the loop has no limiter and is paced by `display_get()`, so the other two points run just after the previous frame was queued, while the RSP is most likely still busy with it.
  - Before `display_get` the wait apparently replaces time the loop would spend blocked there anyway: frames stay on time, but the CPU is busy longer.
- **The old placement drops frames with music playing** (p99 21.5 ms, 1 % low 46 FPS). That was S4b.1's demo `audio` slot of 2.69 ms average and 10.2 ms peak.
- **Encoding does not matter for cost.** VADPCM costs the same as raw PCM, so VADPCM stays the default (the demo track is 46 KB instead of 176 KB).
- **Busy effect voices add ~0.45 ms per frame.**
- **The mixer now runs twice as often.** The upgrade doubled libdragon's audio buffer rate (`BUFFERS_PER_SECOND` 25 → 50), so the mixer runs ~0.8 times per frame at 60 FPS instead of ~0.4.
- **Opus** was not in this build (`SND_OPUS=0`); it was measured separately, below.

**Opus** (`...-p2-s4b2-audio-opus-...`, a `make BENCH=1 BENCH_KIND=AUDIO SND_OPUS=1` ROM, which runs the Opus steps first):

| Param | Music | Poll point | `audio` ms | CPU avg / max ms | p99 ms | 1 % low FPS |
|---|---|---|---|---|---|---|
| 300 | Opus | after present | 4.23 | 14.88 / 35.2 | 32.0 | 29.9 |
| 320 | Opus | after `display_get` | **3.15** | 13.32 / 17.2 | 22.0 | 44.4 |
| 220 | VADPCM | after `display_get` | 0.46 | 10.19 / 12.2 | 17.4 | 57.4 |

- **Opus costs ~2.7 ms more CPU per frame than VADPCM** at the default point, because most of the decoder is CPU code. At this load it pushes frames over budget.
- **Opus adds ~33 KB of heap while it plays** (`heap_kb` 794 → 827): decoder state and a 48 kHz ring. The other steps match the first run within a few percent.
- **Step 0 (silence) was slow** in this run: CPU and RDP ~40 % slower than usual, and `audio` at 4.1 ms. It is the first ~5 s after reset, the only difference from the other steps and runs (D32).
- **The first Opus attempt asserted twice, both fixed in this stage:**
  - *frequency 48000 exceeds configured limit 22047 on channel 0*: channels are limited to the output rate unless raised, and Opus is 48 kHz (D30);
  - after that fix, ares hit *samplebuffer too small* when VADPCM followed Opus on the same channel, a libdragon ring-reuse bug worked around with a fresh ring per track (D31).

**Full benchmark vs S4b.1** (`...-p2-s4b2-all-...`, `bench_compare.py`): **0 regressions.** This file is the comparison point for the next stage.

| Step | S4b.1 → S4b.2 CPU ms | Change |
|---|---|---|
| empty | 1.14 → 0.97 | −14.9 % |
| fillrate 1–8 | 1.14–1.17 → 1.02–1.03 | −10 to −13 % |
| shadows blob / projected | 9.10 → 7.82 / 13.37 → 11.52 | −14.1 / −13.8 % |
| shadows off, lights 0–4 | 7.64–8.13 → 7.22–7.99 | −1.7 to −5.9 % |
| objects 16 / 24 | 7.58 → 7.19 / 10.91 → 10.14 | −5.1 / −7.1 % |
| objects 32 / 48 / 64 | 16.77 → 17.15 / 25.60 → 26.30 / 33.97 → 34.89 | +2.3 / +2.7 / +2.7 % |
| everything else | | within ±2 % |

The mixer runs even with no music playing, and it no longer waits behind the frame's RSP work. That saves most on the triangle-heavy steps.

Two things differ from the S4b.1 run:
- **The profiler was off** in this run and on in S4b.1. It costs ~0.1 ms per frame, which is most of the empty step's gain.
- **The pillar data landed partly on the render stack's D-cache sets** (`BENCH_LAYOUT` `pillar_vtx` 0x80153360, sets 0x1360–0x1A60; D26). That fits the +2–3 % on the overloaded objects steps.

| Check | Result |
|---|---|
| Sound check on the A3D | music fade-in, menu sounds, the positional bounce sound, Sound tab volumes and mute all as expected |
| Reset Soak ×3 (30 resets, music on) | **0 B, 0 B, +320 B.** A CSV dump fired inside the third soak, and the dump's own MEM row already shows the +320 B. The nine resets after it added nothing, so it is a one-time allocation during the dump, not a per-reset leak. |
| Stability (D28) | **no RSP crash** in about 40 minutes of testing across several boots: Reset Soak ×4 (40 resets; the fourth also 0 B), Menu Sweep (24 items, 123 options), Bench = All, and Bench = Audio three times, once with Opus. The first Opus attempt stopped on the D30 assert, which is a CPU assertion, not an RSP crash. |
| RAM (release ELF) | 502.0 → 511.6 KB (+9.6 KB: the sound module and the Audio benchmark). `SND_OPUS=1` would add ~94 KB (the full Opus decoder); keeping it opt-in saved that much against the first S4b.2 build. |
| ROM (release) | 400 KB, unchanged. The benchmark tracks are debug-only; the debug ROM carries the 176 KB raw track. |

## Phase 2 · S5.1 UI core and cached menu (2026-09-24, debug build, Analogue 3D)

The Start menu's text now renders into an offscreen RGBA16 surface only when a slot changes, and each frame is drawn with one copy-mode blit ([UI.md](UI.md)). Other changes in this stage:
- the menu is split into a model (`menu.c`, host-tested) and a view in a style (`menu_view.c`);
- a scroll bar replaces the "..." markers, and the cursor can visit disabled items;
- `text_draw` no longer draws rdpq_text's AA-fix rectangle (the display has no VI anti-aliasing) and sets standard mode before each print instead.

**Bench = UI** (`docs/benchmarks/2026-09-24-p2-s5_1-ui-debug-a3d.csv`, validator off): a copy of the Start menu over the floor and 16 pillars. Direct draws every text element every frame (the pre-S5 cost); cached re-renders only what changed.

| Input | `menu` ms direct → cached | CPU avg ms direct → cached | CPU max ms cached |
|---|---|---|---|
| none | 4.07 → **0.31** | 16.69 → 10.28 | 12.3 |
| cursor move every 8 frames | 4.09 → 0.49 | 16.71 → 10.91 | 18.3 |
| value change every 2 frames | 4.06 → 0.49 | 16.75 → 11.72 | 16.9 |
| tab switch every 30 frames | 3.77 → 0.49 | 16.25 → 10.53 | 18.3 |
| close 30 of every 100 frames | 1.54 → 0.12 | 14.25 → 10.16 | 12.3 |

- **The idle menu costs 92 % less** (4.07 → 0.31 ms), and the frame goes from 16.7 ms (at the budget, 1 % low 53 FPS) to 10.3 ms.
- **A change frame still costs a few ms.** It pays the text of what changed: two rows for a cursor move, the whole panel for a tab switch. The cached CPU maximum is 16.9–18.3 ms against 12.3 ms idle. S5.2 can spread re-renders over frames if that matters.
- **The S5 target is met on average** (`menu` ≤ 1.5 ms). The ≤ 3 ms peak target is not met on change frames.
- **Direct text now costs more** than the 2.6 ms measured in S0 (~4 ms). This benchmark draws over a heavier scene, and the benchmark's own status line (`hud_us`, ~3.3 ms in the direct steps against ~0.6 ms in the cached ones) waits behind the RSP work the direct menu queued.
- **Heap:** +96 KB for the menu's text surface (`heap_kb` 903 → 999).

**With the RDP validator on** (`...-s5_1-ui-validator-...`):
- direct menu: 7.1 ms per frame, CPU 22.9 ms (44 FPS);
- cached menu, idle: 0.31 ms, but CPU still 18.0 ms, because the validator makes everything slower (the two status lines cost 4–5 ms);
- cached menu, change frames: up to 32–48 ms.

The lower-screen flicker (D18) no longer appears with the validator on and the menu open (A3D, user check). The remaining over-budget frames under the validator are a whole-frame issue, so the rest of D18 moves to S6.

**The validator found a bug in the first S5.1 build.** Slot clears used fill mode with a scissor starting at x=14, and fill mode on a 16-bit surface needs 4-pixel alignment (undefined on hardware). Slot boxes now snap to 4-pixel columns; the next validator run showed 0 of these errors.

**Full benchmark vs S4b.2** (`...-p2-s5_1-all-...`, `bench_compare.py`): **4 steps over the gate**, in code this stage did not touch:

| Step | S4b.2 → S5.1 CPU ms | Where (BENCH_PROF against S4b.1, also profiler on) |
|---|---|---|
| particles 64 / 96 / 128 | 2.93 → 3.24 / 3.69 → 4.22 / 4.55 → 5.19 (+11 to +14 %) | `draw` 3.81 → 4.35 ms at 128: the particle renderer |
| textures 2 | 5.76 → 6.21 (+7.8 %) | `mesh_light` 0.58 → 0.78 ms; textures 1/4/8 +0.5 to +3.8 % |
| everything else | within ±5 % (most −0.5 to −4 %) | |

- **The profiler was on in this run and off in S4b.2**, which accounts for ~0.1 ms.
- **The rest is most likely data layout (D26 family).** Static data grew by 11 KB (the benchmark's menu copy and the views), which moves every static array. The 128-particle pool, which spans most of the 8 KB direct-mapped D-cache, now starts at cache offset 0x0F18. The RDP is slightly less busy than before (1.42 vs 1.60 ms at 128 particles).
- **This is not proven.** It is tracked as D33 and re-checked with the S5.2 build; a same-ROM A/B settles it if it persists.

`docs/benchmarks/2026-09-24-p2-s5_1-all-debug-a3d.csv` is the comparison point for S5.2.

## Phase 2 · S5.2 HUD, overlay and styles (2026-09-24, debug build, Analogue 3D)

The demo HUD and the debug overlay now use cached UI layers:
- the HUD as two `HudPanel`s, refreshed at ~6 Hz with at most 2 lines re-rendered per frame and a drop shadow;
- the overlay with one slot per row.

Two styles were added (Classic, Minimal), selectable live from Settings → UI Style ([UI.md](UI.md)).

**Bench = UI** (`docs/benchmarks/2026-09-24-p2-s5_2-ui-debug-a3d.csv`, profiler off, so no `BENCH_PROF` rows). CPU ms:

| Step | CPU avg / max | Note |
|---|---|---|
| cached menu, Debug style (10) | 10.24 / 12.4 | as in S5.1 (10.28) |
| cached menu, Classic (20) | 10.37 / 12.5 | +0.13 ms: 8 gradient bands and a 2 px frame |
| cached menu, Minimal (30) | 10.34 / 12.6 | +0.10 ms |
| HUD direct, every frame, with shadow (40) | 15.97 / 18.4 | 13 text prints per frame |
| HUD cached (41) | 10.56 / 15.5 | **−5.4 ms**; p99 22.1 ms, 1 % low 45 FPS: a refresh frame can overrun |

- **The cached HUD costs about 0.6 ms per frame on average.** That is the step's CPU minus the cached static menu step's, which shares the scene. It compares with 1.1–1.4 ms for the old demo HUD in S0, which had no shadow and fewer glyphs. The S5 target of ≤ 0.4 ms is not reached.
- **Refresh frames still spike** by ~3 ms, even with the 2-line budget, because the shadow doubles each line's glyphs. Two ways down: a budget of 1, or a backdrop instead of the shadow (the Classic style's HUD already uses backdrops).
- **Bench = All vs S5.1** (`...-s5_2-all-...`): **0 regressions.**
  - Textures swung back −7 to −13 % (S5.1's +7.8 % was layout); lights, shadows and objects +2 to +4 %.
  - Against S4b.2, particles 64–128 are still +11 to +16 % (D33). The particle update is up ~10 % as well, which is pure CPU. The pool no longer aliases the render stack's D-cache sets in this build, so data layout alone does not explain it; code placement (the update is outside the hot-text block) is the next suspect. A same-ROM A/B is planned.

`docs/benchmarks/2026-09-24-p2-s5_2-all-debug-a3d.csv` is the comparison point for the next stage.

## Phase 2 · S5.3 dialog system and text box (2026-09-24, debug build, Analogue 3D)

Dialog conversations are compiled from JSON into `.dlg` banks and shown by a `TextBox` ([DIALOG.md](DIALOG.md)). The text box renders each page once into a cached layer, then draws the typewriter reveal as copy-mode blits of that page.

Bench = UI gained two steps (the `dialog` profiler slot; `dialog_us` appended to `BENCH_PROF`):
- **50**: the demo conversation at reading pace (a scripted reader waits 0.4 s per finished page, 1 s per choice list);
- **51**: skipping (A every 3 frames, so a new page is laid out and rendered every few frames: the worst case).

**Bench = UI** (`docs/benchmarks/2026-09-24-p2-s5_3-ui-debug-a3d.csv`, profiler on):

| Step | `dialog_us` avg | CPU avg / max ms | p99 ms | 1 % low FPS |
|---|---|---|---|---|
| cached menu, static (10), for reference | — | 10.30 / 12.3 | 17.0 | 58.8 |
| dialog, reading pace (50) | **297** | 10.32 / 15.9 | 17.1 | 49.4 |
| dialog, skipping (51) | **534** | 10.81 / 15.8 | 22.1 | 45.0 |

(ares measured 450 / 658 µs: the emulator's timings are only indicative.)

- **A reading text box costs 0.3 ms per frame** on average, about the same CPU as an open static menu. The per-frame work doesn't depend on the text length: a few rectangles (box, frame, name plate, choice box) and two or three blits.
- **A new page costs one layout and one text render** of up to ~130 glyphs, up to ~5.5 ms in that frame (CPU max 15.9 ms against ~10.3 for a quiet frame). CPU never passes 16.7 ms, but when skipping (a page every ~6 frames) p99 frame time reaches 22 ms, the same pattern as the menu's change steps (3, 5, 7, 13: p99 21–24 ms with CPU max 17–18 ms). That is frame pacing around busy frames, which S6 takes up (D19). A render budget for pages (lay out on one frame, render on the next) is the fix if it shows in play.
- **Against S5.2** (`...-s5_2-ui-...`): 0 regressions at 5 % / 0.15 ms on the 14 shared steps, although the profiler was on in this run and off in the reference.

`docs/benchmarks/2026-09-24-p2-s5_3-ui-debug-a3d.csv` is the UI comparison point; `...-s5_2-all-...` stays the Bench = All reference.

## Phase 2 · S6.1 engine core (2026-09-24, debug build, Analogue 3D)

The frame loop and hardware init moved from `main.c` to `src/engine/engine.c`, and the screen and guard-band literals to `engine_config.h` ([ENGINE.md](ENGINE.md)). No behaviour change was intended; the hot-text block kept its size. Bench = All: `docs/benchmarks/2026-09-24-p2-s6_1-all-debug-a3d.csv` (profiler **on**, ~0.1 ms per frame; the S5.2 and S4b.2 references ran with it off).

- **Against S5.2: 0 regressions.** Most steps are 1–4 % faster; lights −4 to −6 %, particles 64–128 −7 to −10 %.
- **D33 mostly gone.** Against S4b.2 (before the slowdown), particles 32–128 are −1.4 / +2.7 / +5.1 / +5.1 % (+0.19 and +0.23 ms at 96 and 128), of which ~0.1 ms is the profiler. In S5.1–S5.2 they were +11 to +16 %. Nothing in the particle code changed, so this is layout again: moving the loop code shifted every later function. The same-ROM A/B (S6.3) still decides whether the particle update should join the hot-text block.
- Projected shadows (2): +1.5 % against S5.2, +5.5 % (+0.6 ms) against S4b.2, flagged by the 5 % gate with the profiler's share inside it. Watch it in S6.2.
- Textures −4 to −6 % and objects −0.3 to −1.9 % against S4b.2.

`docs/benchmarks/2026-09-24-p2-s6_1-all-debug-a3d.csv` is the comparison point for the next stage.

## Phase 2 · S6.2 frame pacing (2026-09-24, debug build, Analogue 3D)

The 30 FPS cap is libdragon's `display_set_fps_limit()` instead of a busy-wait, `dt` comes from `display_get_delta_time()`, the Z-buffer from `display_get_zbuf()`, and a vblank handler records how many vblanks each frame stays on screen ([ENGINE.md](ENGINE.md), "Pacing and time"). Bench = All: `docs/benchmarks/2026-09-24-p2-s6_2-all-debug-a3d.csv` (profiler on), now with a `BENCH_PRESENT` row per step.

**What reaches the screen** (the new measurement):

| Where | Presented frames | Shown for 1 / 2 / 3 vblanks | Late |
|---|---|---|---|
| demo, quiet view, 60 FPS (CSV dump) | 256 | 256 / 0 / 0 | 0 |
| every Bench = All step at 60 FPS (22 steps) | 239 each | 239 / 0 / 0 | 0 |
| objects 32 (57.7 FPS) | 240 | 231 / 9 / 0 | 9 |
| objects 48 (38.7 FPS) | 239 | 107 / 132 / 0 | 132 |
| objects 64 (29.1 FPS) | 239 | 0 / 225 / 14 | 239 |

- **D19 is a loop artefact, not a visible one.** In the same demo dump the loop still ranged 12.4–20.9 ms (the triple-buffer pattern), yet every frame was on screen for exactly one vblank. Since `dt` now follows the display rather than the loop, animation and physics no longer inherit the loop's jitter either.
- **Over budget, frames repeat in whole vblanks**, and the presentation rows show how many: at 38.7 FPS about half the frames are held for 2 vblanks. That is the measurement to watch for stutter from now on; loop p99 alone misreads it (step 0's loop p99 rose to 17.6 ms while all its frames were on time).
- **30 FPS** (checked in ares, and visually on the A3D): every frame 2 vblanks, the wait all inside `wait_display`, `limiter` 0.

**Against S6.1: 0 regressions.** Most steps are 1–3 % faster, projected shadows −5.7 % (back within 0.5 % of S4b.2) and particles 96–128 −6 %. The near-idle steps (step 0, fill rate) gained 0.08–0.14 ms, under the gate: the benchmark's status-line text (`hud_us` 444 → 515 µs) and `update` (+10 µs), code S6.2 didn't touch, so layout again (the engine file grew and everything linked after it moved).

**Z-buffer at the top of RDRAM:** no measurable RDP change on the A3D (objects 64, the most Z-heavy step: RDP busy 11.00 → 11.00 ms). Kept, since it costs nothing and frees the malloc heap of 150 KB of long-lived data. (`heap_used` doesn't move: libdragon counts top-of-RAM allocations as used.)

**Boot (D32):** the demo boot's `BOOT` rows show no slow start (16.67 ms frames and 0.1 ms audio from the first second). D32 was only ever seen in boot-to-benchmark (`BENCH=1`) runs; that case is checked separately.

### D32: the slow start after reset

Two boot-to-benchmark runs of the S6.2 code:
- `docs/benchmarks/2026-09-24-p2-s6_2-d32-bootbench-debug-a3d.csv`, starting at once: step 0 (the empty scene) CPU 1.19 ms and RDP 1.39 ms against 1.01 and 0.95 in the demo-launched run, step 1 CPU +13 %; every later step normal.
- `...-d32-bootbench-settle-debug-a3d.csv`, holding the empty scene until 15 s after reset: identical content every second, so the `BOOT` rows isolate time since reset.

| Seconds after reset | draw (CPU) | update | audio | RDP busy |
|---|---|---|---|---|
| 1–6 | 0.58 ms | 0.08 | 0.09 | 1.26–1.33 ms |
| 8–13 | 0.43 ms | 0.06 | 0.07 | 0.90 ms |

For ~7.5 s after every reset the same work costs ~35 % more CPU, ~47 % more RDP and ~25 % more audio mixing (an RSP job), then drops within one second. The demo boot shows the same window (seconds 1–7). Nothing in the engine changes at that moment and ares shows nothing, so it is the A3D or the cart after reset. The mixer's 4.1 ms from the original report is gone. After the settle, step 0 measures 0.91 ms CPU and 0.95 ms RDP, like a demo-launched run.

### D34: libdragon's queue pointer on the stack's cache sets

The two boot ROMs above differ only by the settle code (448 bytes, never run during the steps), yet every mesh-heavy step of the second is 7–12 % slower (`mesh_tris` +14 %, RDP unchanged). `BENCH_LAYOUT` and `nm` show why: libdragon's command-queue pointer (`rspq_cur_pointer`, read and written by every rdpq command) moved from D-cache set 0x1750 to 0x1920, onto the render stack's sets (≈0x1880–0x1CF0), so each triangle refetches both. Until S6.3 pins hot static data, compare mesh-heavy steps only between builds with the same layout, or inside one ROM.

`docs/benchmarks/2026-09-24-p2-s6_2-all-debug-a3d.csv` is the comparison point for the next stage.

## Phase 2 · S6.3 hot data pinned (2026-09-24, debug build, Analogue 3D)

The static data every drawing loop touches (libdragon's command queue and rdpq state, the triangle formats, fog, the profiler and stats counters, the particle pool, the shadow scratch, the floor grid) is pinned to fixed D-cache colours away from the render stack by `src/engine/hot_data.ld`, and `tools/hot_data.py` checks it in CI ([HARDWARE.md](HARDWARE.md), "Hot data"). Two boot-to-benchmark runs (with the D32 settle), profiler on:
- `docs/benchmarks/2026-09-24-p2-s6_3-all-bootbench-debug-a3d.csv`: the pinned build;
- `...-s6_3-all-bootbench-pad448-debug-a3d.csv`: the same build with `make LAYOUT_PAD=448`, 448 bytes of unused code moving all data after it, the shift that cost 7–12 % in D34.

**Layout stability: the two builds agree within ±0.5 %** on every step but one (objects 32, at the edge of the frame budget: −1.7 %); fill rate +1.1 %, i.e. 0.01 ms. Before S6.3 the same shift moved every mesh-heavy step by 7–12 %.

| Against | Mesh-heavy steps (lights, textures, shadows, objects) | Particles 32–128 | Other |
|---|---|---|---|
| the slow build of D34 (queue pointer on the stack) | −6..−10 %; objects 32 back to 60 FPS (17.8 → 12.6 ms) | −2.5..−3 % | fill rate −3..−4 % |
| the fast build of D34 (queue pointer just off the stack by luck) | ±2 % | −3..−5.5 % | fill rate −6..−8 % |
| S4b.2, before D33 (profiler off there) | shadows −3..−7 % | **−3.5..−5.5 %** | |

- **D33 is closed:** particles are faster than before the slowdown, although this run carries the profiler's ~0.1 ms. `hot_data.py` showed the particle pool on 47 of the particle loop's stack lines in every earlier build.
- The floor grid (58 stack lines before) is pinned too, but Bench = All draws no floor; the demo's `floor` slot shows it.
- The pads cost 9–14 KB of RAM (`rom_budget.py` now counts gaps between sections).

`docs/benchmarks/2026-09-24-p2-s6_3-all-bootbench-debug-a3d.csv` is the comparison point for the next stage. With the render path's static data pinned, boot-to-benchmark runs (unattended, `make BENCH=1`) compare directly with it.

## Phase 2 · S6.4 D18 root-caused: the RDP validator tears the display (2026-09-24, debug build, Analogue 3D)

Two boot-to-benchmark runs of Bench = UI, identical except for the validator (`make BENCH=1 BENCH_KIND=UI [BENCH_VALIDATOR=1]`). The UI bench still has the pre-S5 direct-menu steps (n = 0–4), the path that used to flicker. The engine now counts **torn** flips: framebuffer flips that happen after the VI started scanning the picture ([ENGINE.md](ENGINE.md), "Pacing and time").

| Step (n) | Validator on: FPS | late / torn of ~240 | worst torn half-line | Validator off: FPS | late / torn |
|---|---|---|---|---|---|
| direct menu, idle (0) | 46.8 | 67 / 26 | 90 | 60.0 | 0 / 0 |
| cached menu, idle (10) | 59.8 | 0 / 12 | 116 | 60.0 | 0 / 0 |
| direct, cursor moves (1) | 46.8 | 68 / 28 | 86 | 60.0 | 0 / 0 |
| cached, cursor moves (11) | 59.2 | 3 / 28 | 112 | 60.0 | 0 / 0 |
| direct, value changes (2) | 46.7 | 69 / 28 | 90 | 60.0 | 0 / 0 |
| cached, value changes (12) | 58.6 | 5 / 34 | 110 | 60.0 | 0 / 0 |
| direct, tab switches (3) | 48.9 | 54 / 26 | 88 | 60.0 | 0 / 0 |
| cached, tab switches (13) | 59.7 | 1 / 46 | 160 | 60.0 | 0 / 0 |
| direct, close/reopen (4) | 51.2 | 43 / 14 | 90 | 60.1 | 0 / 0 |
| cached, close/reopen (14) | 60.1 | 0 / 23 | 116 | 60.0 | 0 / 0 |
| Classic / Minimal styles (20, 30) | 58.4 / 58.2 | 6–8 / 34–45 | 178 / 190 | 60.0 | 0 / 0 |
| HUD direct / cached (40, 41) | 47.0 / 60.1 | 66 / 20, 0 / 0 | 88, — | 60.0 | 0 / 0 |
| dialog reading / skipping (50, 51) | 59.6 / 60.0 | 1 / 8, 0 / 1 | 408, 36 | 60.0 | 0 / 0 |

- **With the validator: 952 libdragon `VI WARNING: __vblank_interrupt outside of vblank period` lines; without it: 0** (and 0 in every earlier run without the validator). The user saw the lower-screen flicker return in the validator runs, with the scene or the menu changing.
- **Cause (libdragon):** `rdpq_debug` validates each RDP buffer inside the RSP and RDP interrupt handlers with interrupts disabled (`__rdpq_trace_fetch` → `__rdpq_trace_flush`, marked "FIXME: remove this", unchanged in upstream preview). Validation of a command-heavy buffer takes milliseconds, the vblank interrupt waits, and the display flips `VI_ORIGIN` mid-picture: the lines below the flip show the new frame. Text is command-heavy, which is why menus showed it first and why S5.1's cached menu (fewer commands) seemed to fix it. The only step with 0 torn flips is the cached HUD, which issues almost no text commands per frame.
- **Frame overruns are not the cause:** under the validator the cached-menu steps run at ~60 FPS with 0–5 late frames and still tear 12–46 times; without it every step presents all frames on time with 0 torn.
- **Validator off, against S5.3** (`...-s5_3-ui-...`): every UI step −4 to −8 % CPU, 0 regressions; the direct-menu steps now hold 60 FPS.

Captures: `docs/benchmarks/2026-09-24-p2-s6_4-ui-validator-bootbench-debug-a3d.csv` and `...-s6_4-ui-bootbench-debug-a3d.csv` (the libdragon warnings are in the raw USB logs only). Resolution: tearing while RDP Check is on is expected and documented ([DEBUGGING.md](DEBUGGING.md)); the engine's torn-frame counter shows it. `docs/benchmarks/2026-09-24-p2-s6_3-all-bootbench-debug-a3d.csv` stays the Bench = All comparison point.

## Phase 2 · S7 scene owns physics and colliders (2026-09-24, debug build, Analogue 3D)

Boot-to-benchmark Bench = All (`make BENCH=1`, 15 s settle), then the demo. In the demo the ball casts its shadow, is selected and held, and lands on the moved platform; the Demo↔Benchmark fade works both ways; no asserts or RSP errors. Against S6.3 (`bench_compare.py`, 5 % / 0.15 ms gate):

| Step | S6.3 CPU ms | S7 CPU ms | Δ | FPS |
|---|---|---|---|---|
| objects 8 / 16 / 24 | 3.92 / 6.68 / 9.42 | 4.15 / 7.14 / 10.10 | +5.9 / +6.9 / +7.2 % | 60 |
| objects 32 | 12.60 | 17.11 | +35.8 % | 60.0 → 56.8 |
| objects 48 / 64 | 24.82 / 32.97 | 26.21 / 34.81 | +5.6 % | 40.3 → 38.1, 30.3 → 28.7 |
| lights 0 / 1 / 2 / 4 | 6.69 / 6.82 / 6.95 / 7.33 | 7.12 / 7.31 / 7.52 / 8.02 | +6.4 / +7.2 / +8.2 / +9.4 % | 60 |
| textures 1–8 | 5.23–5.49 | 5.71–5.98 | +8.8 to +9.2 % | 60 |
| shadows off / blob / projected | 6.72 / 7.32 / 11.13 | 7.19 / 7.82 / 11.56 | +7.0 / +6.8 / +3.9 % | 60 |
| particles 32–128 | 2.06–4.35 | 2.14–4.47 | +2.8 to +3.9 % | 60 |
| idle, fill-rate 1–8 | 0.88–0.91 | 0.96–0.99 | +0.07 ms (under the noise floor) | 60 |

16 steps over the gate. **None of it is S7's code:** the benchmark scene has no objects, bodies or scene colliders, so S7 adds two empty loops per frame there. The per-stage profile (`BENCH_PROF`) places the cost:

- `mesh_tris`, the per-triangle loop, is unchanged (−0.5 %).
- The per-object part of `objects` outside the cull, light and triangle slots grew by **21.6 µs per object**, and `mesh_light` by **3.7 µs per object per point light**. The HUD's text costs 40–60 µs more in every step.
- **D-cache:** `SceneObject` grew from 56 to 72 bytes, so `Scene.camera` and `Scene.lighting` moved 512 bytes into the struct. In the benchmark scene they went from colours 0x1680–0x1908 to 0x18A0–0x1B28, onto the render stack's hot sets (D26's ≈0x1880–0x1CF0); the point lights now share lines with `mesh_draw`'s own frame. `mesh_draw` reaches both through pointers, which `tools/hot_data.py` does not follow. The triangle loop also re-reads the `Mesh` struct (group index range, index and vertex pointers) every triangle, and the pillar's `Mesh` moved from 0x1EA8 to 0x00B0, next to the pinned rdpq state.
- **I-cache:** `bench_draw`, the unpinned per-object loop around `mesh_draw`, now shares 36 I-cache lines with the mesh phase (0 in S6.3). `tools/hot_text.py` checks the phase's callees, not the loop that calls it.
- The same ROM's Debug-tab Objects run, with the pillar's vertex data at another heap colour (0x0E50 instead of 0x0490), agrees within 1 %: heap placement is not the cause.

S6.3's `LAYOUT_PAD` check shifted code only; the static data kept its colours. S7 is the first stage to resize static data the draw path reads. Filed as **D35**, fixed in S7.1. Captures: `docs/benchmarks/2026-09-24-p2-s7-all-bootbench-debug-a3d.csv` (the USB log also holds the Objects run from the Debug tab).

## Phase 2 · S7.1 per-object draw path pinned (D35) (2026-09-24, debug build, Analogue 3D)

Four boot-to-benchmark Bench = All runs (`make BENCH=1`, 15 s settle). The S7.1 code was measured before and after pinning libdragon's command-buffer switch, each time plain and with `make LAYOUT_PAD=448`, which moves every function and static variable that is not pinned by 448 bytes. Reset Soak ×10 in the demo: heap delta 0 B.

| Run | Build | vs S6.3 (5 % gate) | Notes |
|---|---|---|---|
| 1 | S7.1a: camera and lighting copies pinned, `Mesh` reads in locals, per-object loops pinned | 3 steps over: objects 32 +5.3 % (every frame still on time), particles 64 / 96 +5.7 / +5.1 % | vs S7: 0 regressions; objects 32 17.11 → 13.27 ms (56.8 → 59.8 FPS), mesh, light and shadow steps −5 to −9 % |
| 2 | S7.1a + `LAYOUT_PAD=448` | 0 | 1–4 % faster than run 1 in every step, idle included |
| 3 | S7.1: run 1 + the buffer switch pinned | **0** | mesh steps −3.9 to +1.5 %, textures −0.2 to −2.6 %, objects 32 12.17 ms (−3.4 %) at 60 FPS |
| 4 | S7.1 + `LAYOUT_PAD=448` | 0 | particles within ±1 % of run 3; mesh steps 1.5–2.8 % faster |

- **D35 fixed.** Run 1 against S7 removes the per-object (+21.6 µs) and per-light (+3.7 µs per object) costs S7 had added, and objects 32 is back at 60 FPS.
- **Runs 1 and 2 exposed one more layout effect.** libdragon's command buffers are 2 KB, so `rspq_next_buffer` runs every ~20–30 triangles and clears the next buffer with `memset`. `hot_text.py` listed it as a rare path. Unpinned, its ~40 lines fell on the shadow code in S6.3 and on the particle code in S7 and S7.1a (+3–4 % there). It is pinned since run 3, and the particle steps of runs 3 and 4 agree within ±1 %.
- **What still moves with layout (runs 3 and 4):**
  - The heap's vertex data (D26). In the plain build the pillar's vertices (heap colour 0x0490) overlap the pinned camera copy's matrix and frustum. `mesh_cull` goes 95 → 67 µs and the per-object setup ~90 µs for 16 objects: about 6 µs per object, ~2 % of a mesh step. The pinned per-triangle work (`mesh_tris`, `mesh_light`) agrees within ±0.5 %. The P3.1 vertex cache fixes this.
  - The benchmark's status text, which is cold libdragon code: `hud` 466 → 431 µs.
- **Against S6.3** a flat ~0.1 ms per frame remains: `hud` +50 µs, `update` +10–20 µs (S7's physics and sync steps and the moved update code), and ~5 µs for the two out-of-line object loops. It reads as +10 % in the 1 ms idle and fill-rate steps and +2–5 % in the particle steps; the particle renderer itself is within +1 %.

Captures: `docs/benchmarks/2026-09-24-p2-s7_1a-all-bootbench-debug-a3d.csv` and `...-s7_1a-...-pad448-...` (runs 1–2), `...-s7_1-all-bootbench-debug-a3d.csv` (run 3, the new Bench = All comparison point) and `...-s7_1-...-pad448-...` (run 4).

## Phase 2 · S8 settings module (2026-09-24, debug build, Analogue 3D)

Boot-to-benchmark Bench = All, then the demo: a walk-through of every tab, Cancel, Reset Scene with shadows and point lights on, and Debug → Menu Sweep (25 items, 126 options). No asserts. S8 does not touch the draw path.

- **Against S7.1: 0 regressions.** Every step is 1.2–4 % faster: objects 16 6.71 → 6.55 ms, objects 32 12.17 → 11.88 ms, projected shadows 10.70 → 10.57 ms.
- The gain is layout, not S8's code. The pillar's heap vertex data moved to colour 0x0650, off the pinned camera copy's lines (D26). S7.1's `LAYOUT_PAD=448` build had the same heap colour and the same result (objects 16: 6.54 ms). The idle and fill-rate steps are 3–7 % faster from the HUD text's cold code alone.

Capture: `docs/benchmarks/2026-09-24-p2-s8-all-bootbench-debug-a3d.csv` (the new comparison point).

## Phase 2 · S9 input: players, contexts, lowest-latency polling (2026-09-25, debug build, Analogue 3D)

Demo walk-through with the debug ROM: menu, D-Up/D-Down (all seven overlay pages, the new Input page), the dialog (the shortcuts stay quiet while it is open), a Controls remap onto D-Up, the Latency choices, and a second controller: it shows on the Input page, its Start opens the menu and only it drives the menu, its B launches the ball. No asserts. Then boot-to-benchmark Bench = All and Bench = Latency from the Debug tab.

**Input lag, Bench = Latency** (floor + 16 pillars, plus a CPU burn; lag = vblanks from the controller read a frame used to the vblank it reached the screen, over the 240 measured frames):

| Load (CPU) | Classic (latest, render ahead) | Low (fresh, render ahead; default) | Lowest (fresh, low-latency pacing) |
|---|---|---|---|
| light (9.6 ms) | 60.0 FPS, lag **3.00** | 60.0 FPS, lag **2.00** | 60.0 FPS, lag **1.00** |
| +4 ms (16.5 ms) | 59.0 FPS, lag 2.08 | 58.6 FPS, lag 2.05 | 30.0 FPS, lag 2.00 |
| +8 ms (21.8 ms) | 45.8 FPS, lag 2.38 | 45.6 FPS, lag 2.34 | 30.0 FPS, lag 2.00 |

- The A3D's controller read arrives **0.69 ms** after its vblank (ares: 1.76 ms). The fresh-read wait costs 0.57 ms of idle time per frame in a light scene and 0.02–0.05 ms once the CPU is the bottleneck (the read is already in); no wait timed out.
- Lowest removes a vblank of lag while a frame fits the budget and halves the frame rate when it does not; Classic and Low converge once the CPU is late, because frames no longer queue.
- `input` (`input_poll`: four pads sampled and mapped to four players' actions) costs **70–83 µs** per frame; `update` fell by ~35 µs (input left it).

**Bench = All against S8: 20 steps +8–11 % CPU, objects 32 60 → 56.1 FPS. Not S9's code: the pillar's vertex data (D26).** `BENCH_PROF`: `mesh_tris` +12–13 % (objects 16 4.27 → 4.85 ms), `mesh_light` +9 %, the textured boxes' `mesh_cull` +20–36 %, particles' draw +3–9 %; the empty scene and fill-rate steps are flat (0.92 → 0.94 ms). `BENCH_LAYOUT`: S9's larger static data moved the heap by 38 KB, and the pillar's vertices (1,600 bytes, read for every triangle) went from D-cache colour 0x0650 (clear of everything hot; S8's gain was this) to 0x1BB0–0x21F0, which covers the mesh phase's stack lines (0x1750–0x1DC0) and wraps onto the pinned rspq/rdpq/profiler state (0x0000–0x01F0). S9.1 places mesh geometry by colour.

Captures: `docs/benchmarks/2026-09-25-p2-s9-all-bootbench-debug-a3d.csv`, `docs/benchmarks/2026-09-25-p2-s9-latency-debug-a3d.csv`. The comparison point stays S8 until S9.1.

## Phase 2 · S9.1 mesh geometry by colour, input sync AUTO (2026-09-25, debug build, Analogue 3D)

Three boot-to-benchmark Bench = All runs and a Bench = Latency run. `mesh_finalize()` now puts each geometry block at a fixed D-cache colour, inside a window (0x0520–0x0F1F) that is clear of the render stack and of the pinned data the mesh phase reads. `BENCH_LAYOUT` shows the pillar's vertices at colour 0x0520 and its indices at 0x0B60 in every build. After run 2 the default input sync became `INPUT_SYNC_AUTO`: wait for the vblank's controller read only while the CPU work is under 70 % of the budget.

| Run | Build | vs S8 (5 % gate) | Notes |
|---|---|---|---|
| 1 | S9.1a: geometry by colour | 8 steps over: particles 64–128 +7.7 to +9.8 %, textures 1–8 +6.6 to +8.3 %, objects 32 at 58.8 FPS | mesh steps +3.4 to +4.2 % (S9: +8 to +11 %) |
| 2 | S9.1a + `LAYOUT_PAD=448` | 1 over: objects 32 CPU +13.7 %, at 60.1 FPS | 2–6 % faster than run 1 in the mesh, texture and particle steps: vs S8, mesh steps +1.1 to +2.1 %, textures +1.2 to +2.1 %, particles +3.8 to +4.2 % |
| 3 | S9.1: run 1 + `INPUT_SYNC_AUTO` | 7 over: particles 64–128 +7.0 to +8.7 %, textures 1, 2 and 8 +5.2 to +6.5 %, objects 32 at 59.7 FPS | **vs S9: 0 regressions.** Mesh, light and shadow steps −6 to −7 %; objects 32 56.1 → 59.7 FPS, 48 37.8 → 40.1, 64 28.5 → 30.2. Mesh steps vs S8: +2.3 to +3.1 % |

- **The geometry no longer moves.** `mesh_tris` at 16 pillars: S8 4.27 ms, S9 4.85, run 1 4.54, run 3 4.40, run 2 4.39. A static-data change can no longer put the pillar's vertices on the render stack's lines (S9: +13 %).
- **`INPUT_SYNC_AUTO`.** In run 1 the fresh-read wait (0.4–0.6 ms of idle time in light steps) also ran in the heavy steps. At 32 pillars it made frames end late, and the audio mix then waited behind them (D38): 58.8 FPS, `audio` 2.9 ms. AUTO skips the wait while the average CPU work is at or above 70 % of the budget: 151 of 240 frames at 32 pillars, 7 at 48, 6 at 64, none in the lighter steps, where it waits like FRESH.
- **What still differs from S8 is not the geometry:**
  - **Particles +7–9 % (+4 % in run 2).** Since S9 the particle renderer costs 23.6 µs per particle. S6.3–S8 measured 20.8–21.5 µs, and run 2 21.8 µs. Every function and variable of the particle phase is pinned, and `hot_text.py` and `hot_data.py` report the same placement as in S8, so the cost follows something the tools don't track. Filed as D37; S10 measures it with libdragon's time accounting (interrupt time, CPU waits on the RSP).
  - **Textures +5–6.5 % (+1–2 % in run 2).** The texture upload path is unpinned by design ([HARDWARE.md](HARDWARE.md)). The textured boxes' `Mesh` structs (6.4 KB of face groups and materials, read per group) are benchmark statics, and 90 of their lines share colours with the mesh stack in this build. Across the S6.3–S9.1 builds the textures steps' `draw` has ranged 4.65–5.34 ms.
  - **Objects 32: 59.7 FPS** (1 of 240 frames late), `audio` 2.2 ms. The step sits at the 60 FPS edge: a frame that ends late makes the next audio mix wait for the RSP. That wait is 0.1–0.6 ms or 2.2–3.2 ms depending on the build (S7.1, S8 and run 2 the first, S7, S9 and runs 1 and 3 the second), and 6 ms at 48 pillars and 10 ms at 64 in every build since S6. Filed as D38.
- **Heap:** each geometry block is 8 KB-aligned and starts at its colour, and the bytes before it stay unused: +22 KB in the benchmark scene (799 → 821 KB).

**Input lag, Bench = Latency** (the S9 scene and loads):

| Load (CPU) | Classic (latest, render ahead) | Low (AUTO, render ahead; default) | Lowest (fresh, low-latency pacing) |
|---|---|---|---|
| light (9.3 ms) | 60.0 FPS, lag **3.00** | 60.0 FPS, lag **2.00** | 60.0 FPS, lag **1.00** |
| +4 ms (15.0 ms) | 59.9 FPS, lag 2.21 | 59.8 FPS, lag 2.16 | 30.0 FPS, lag 2.00 |
| +8 ms (21.4 ms) | 46.6 FPS, lag 2.38 | 46.5 FPS, lag 2.39 | 30.0 FPS, lag 2.00 |

Low now keeps up with Classic near the budget (S9 with FRESH: 58.6 vs 59.0 FPS) and keeps its vblank of lag in light scenes. At +4 ms AUTO skipped 38 of 240 waits; in the other frames the read was already in (3 µs of waiting on average).

Captures: `docs/benchmarks/2026-09-25-p2-s9_1a-all-bootbench-debug-a3d.csv` and `...-s9_1a-all-bootbench-pad448-...` (runs 1–2), `...-s9_1-all-bootbench-debug-a3d.csv` (run 3, the new Bench = All comparison point), `...-s9_1-latency-debug-a3d.csv`.

## Phase 2 · S10 particle blend modes (2026-09-25, debug build, Analogue 3D)

Boot-to-benchmark Bench = All, now 28 steps (Particles gained an all-alpha step, param 1128, and a two-and-two step, 2128), then the demo: bursts with the new smoke, RDP Check on while bursting and in the menu, and the physics ball at 60 and 30 FPS (for S11). `BENCH_PROF` gained `particle_us` and `rsp_wait_us`, the CPU's spin-waits on the RSP from libdragon's time accounting.

| Particles step | CPU ms | `particle_draw` µs | RDP busy ms | RSP wait µs |
|---|---|---|---|---|
| 32 additive | 2.27 | 1155 | 1.17 | 308 |
| 64 additive | 3.05 | 1854 | 1.26 | 300 |
| 96 additive | 3.85 | 2545 | 1.36 | 307 |
| 128 additive | 4.68 | 3260 | 1.47 | 286 |
| 128 alpha (1128) | 4.69 | 3279 | 1.47 | 307 |
| 64 + 64 (2128) | 4.70 | 3277 | 1.48 | 309 |

- **Alpha costs what additive costs**, on the CPU and the RDP, and two batches with a blender change in between cost nothing measurable. Against S9.1 the particle steps are −1.7 to +2.7 % (the step's longer description line costs the HUD ~70 µs); the renderer costs 21.9 µs per particle (S9.1: 23.6, S8: 20.8, by `draw`).
- **D37, one candidate ruled out:** the CPU waits for the RSP a constant ~0.3 ms per frame in these steps, whatever the particle count, so the per-particle cost is CPU work, not waiting.
- **Mesh steps +3.0 to +4.9 % against S9.1 (each under the gate), objects 32 at 57.3 FPS (59.7); not S10's code,** which runs nothing new in those steps. `mesh_tris` is +7 % (objects 16: 4.40 → 4.72 ms, ~116 CPU cycles per drawn triangle). Every mesh-phase function moved by exactly 160 bytes (the particle renderer at the head of the hot-text block grew), so the phase's I-cache layout is the same pattern shifted; its data, geometry and stack sit on the same D-cache colours (`hot_data.py`, `BENCH_LAYOUT`); and the CPU waits 56–65 µs per frame for the RSP. The same kind of move as the particles in S9: per-primitive costs change between builds in a way the cache tools don't capture. D37 now covers both.
- **D38, measured:** at 32 and 48 pillars the frame waits 4.0 and 7.2 ms for the RSP, of which 3.1 and 6.4 ms in the audio mix; the other ~0.9 ms is spent at full command buffers, against ~60 µs in the lighter steps.
- **RDP Check** was clean with smoke, fire and the menu, apart from one burst of errors in the frame it started (commands already in the RDP's buffer, seen without their setup; DEBUGGING.md). The screen tore while it was on (D18: 230 late vblanks), as documented.
- **Demo:** the smoke looks right on the A3D, and the ball behaved the same at 60 and 30 FPS ("framerate fine").

Capture: `docs/benchmarks/2026-09-25-p2-s10-all-bootbench-debug-a3d.csv` (the new Bench = All comparison point).

## Phase 2 · S11 physics on hardware (2026-09-25, debug build, Analogue 3D)

The demo's ball relaunched five times, twice at 60 FPS and three times at 30 FPS (Settings → Frame Rate). Debug builds log `BALL,rest` once the ball has stayed grounded for 0.5 s.

| Frame rate | Launch to rest | Bounces (≥ 60 units/s) | At rest, y |
|---|---|---|---|
| 60 FPS | 2.23 s, 2.22 s | 4, 4 | −65.0 |
| 30 FPS | 2.23 s, 2.23 s, 2.23 s | 4, 4, 4 | −65.0 |

The host simulation of the same launch gives 2.23 s at dt = 1/60 and 1/30 s (2.24 s at the NTSC frame times). Physics steps at a fixed 1/60 s, so the frame rate only changes when the result is seen. Before the resting-contact fix (D39), `grounded` flipped every step at rest and was never set in a 30 FPS frame; the ball looked the same either way. No benchmark: S11 does not touch the render path. Capture: `docs/benchmarks/2026-09-25-p2-s11-ball-debug-a3d.csv`.

## Phase 2 exit (S13): start of Phase 2 → exit (2026-09-25, debug build, Analogue 3D)

Bench = All at the start of Phase 2 (`docs/benchmarks/2026-09-23-baseline-debug-a3d.csv`) against the exit build (S12's code: a boot-to-benchmark run with the D32 settle and the profiler **on**, which costs ~0.1 ms the baseline did not pay). `bench_compare.py`: **all 26 steps faster, 0 regressions.** Every step that fits the budget presented all 239 measured frames on time, with 2 vblanks of input lag.

| Step | FPS | CPU avg ms | CPU change | RDP busy ms | 1 % low FPS |
|---|---|---|---|---|---|
| empty scene | 60 → 60 | 1.20 → 0.92 | −23 % | 1.11 → 0.95 | 55.5 → 59.6 |
| objects 8 | 60 → 60 | 4.53 → 4.03 | −11 % | 2.36 → 2.20 | 54.6 → 59.4 |
| objects 16 | 60 → 60 | 7.88 → 6.85 | −13 % | 3.63 → 3.44 | 51.0 → 59.6 |
| objects 24 | 59.9 → 60 | 11.35 → 9.64 | −15 % | 4.95 → 4.64 | 48.0 → 59.5 |
| objects 32 | 54.6 → 58.6 | 18.09 → 16.15 | −11 % | 6.16 → 5.87 | 42.8 → 57.0 |
| objects 48 | 36.7 → 39.4 | 27.27 → 25.39 | −7 % | 8.85 → 8.43 | 28.4 → 38.8 |
| objects 64 | 27.6 → 29.7 | 36.21 → 33.67 | −7 % | 11.19 → 10.99 | 21.4 → 29.3 |
| particles 32 / 64 / 96 / 128 | 60 → 60 | 2.59 / 4.07 / 5.60 / 7.21 → 2.24 / 2.99 / 3.76 / 4.55 | −14 / −27 / −33 / −37 % | 1.28–1.60 → 1.17–1.46 | 56.6–57.6 → 58.8–59.2 |
| particles 128 alpha, 64 + 64 (new in S10) | 60 | 4.57, 4.58 | — | 1.47 | 59.4, 58.8 |
| lights 0 / 1 / 2 / 4 | 60 → 60 | 7.91–8.37 → 6.85–7.36 | −12..−13 % | 3.64–3.72 → 3.45–3.46 | 51.0 → 59.5 |
| textures 1 / 2 / 4 / 8 | 60 → 60 | 7.54–7.72 → 5.58–5.74 | −26 % | 2.45 → 1.93 | 54.5 → 59.3–59.5 |
| shadows off | 60 → 60 | 8.06 → 6.89 | −15 % | 3.68 → 3.49 | 51.0 → 59.6 |
| shadows blob | 60 → 60 | 8.99 → 7.50 | −17 % | 4.85 → 4.66 | 48.0 → 59.5 |
| **shadows projected** | **40.5 → 60** | 24.70 → 11.18 | **−55 %** | 7.21 → 5.25 | 32.0 → 59.5 |
| fill rate 1 / 2 / 4 / 8 layers | 60 → 60 | 1.14–1.15 → 0.95–0.96 | −17 % | 2.82–14.71 → 2.66–14.52 | 57.1 → 59.4–59.5 |

**Headlines**
- **Every step needs less CPU, 7 to 55 %,** and every step that fits the budget shows each frame for exactly one vblank (1 % lows 48–57.6 → 57.0–59.6 FPS).
- **Projected shadows 40.5 → 60 FPS** for 16 casters (S3, S6.3). **Particles −14 to −37 %**, now also alpha-blended at the same cost (S3, S6.3, S10). **Textures −26 %** (S3).
- **Objects:** 24 pillars hold 60 FPS with 7 ms to spare (9.6 ms). 32 pillars went 54.6 → 58.6 FPS (6 of 239 frames late): at that load the audio mix waits ~3 ms for the RSP (D38, 4.0 ms of RSP waits per frame), which held 32 at 60 FPS in some builds (S6.3, S7.1, S8) and not in others. 48 and 64 pillars +2 FPS.

**Outside the benchmark:**

| Measure | Phase 2 start | Exit | Stages |
|---|---|---|---|
| Demo quiet-view CPU | 8.0 ms (floor 2.8, objects 2.0, audio 1.3, HUD 1.1) | **4.1–4.7 ms** (draw 3.73, update 0.21, audio 0.08), with the S0 test sphere the start did not have | S3, S5.2, S6, S7.1 |
| Reset Scene leak (Reset Soak ×10) | 13.5–20 KB per reset | **0 B** (964,592 → 964,592 B) | S1 |
| Demo heap | 913,544 B after boot | ~866 KB after init; 964,592 B once the menu has been opened (its 98 KB text cache) | S4 (−101 KB of meshes), S5 (+98 KB cache), S9.1 (+~22 KB placement) |
| Start menu (Bench = UI, validator off) | 4.07 ms per frame open | 0.33 ms idle; frames that re-render text (cursor move, value change, tab switch) peak at 16.3–17.5 ms in the benchmark's floor + 16 pillars scene (~9.8 ms), about 12 ms over the demo's view | S5 |
| Input lag, light scene | 3 vblanks | 2 (default), 1 (Lowest) | S9, S9.1 |
| Audio with music | 1.3 ms per frame | 0.08 ms in the quiet view | S4b.2 |
| Physics | verified in ares only | A3D: at rest in 2.22–2.23 s at 60 and 30 FPS, as in the host simulation | S11 |

Measured: the quiet view from the BOOT rows of two demo boots (seconds 10–19, after the A3D's slow start; frame minus the display wait, so the upper value includes the ~0.5 ms input wait); the menu from Bench = UI (0 regressions against S6.4); the soak from the Debug tab. Captures: `docs/benchmarks/2026-09-25-p2-s13-all-bootbench-debug-a3d.csv`, `...-s13-ui-debug-a3d.csv`, `...-s13-soak-debug-a3d.csv`, `...-s13-demo-boot-debug-a3d.csv`. The exit criteria and their results are in ROADMAP_v2 §6.3.

## Phase 3 kickoff (2026-09-25)

Phase 3 (ROADMAP_v2 §7) is measured against the Phase 2 exit: `docs/benchmarks/2026-09-25-p2-s13-all-bootbench-debug-a3d.csv` (Bench = All, 28 steps), `...-p2-s13-ui-debug-a3d.csv` (Bench = UI) and the quiet view of `...-p2-s13-demo-boot-debug-a3d.csv` (4.1–4.7 ms). Stages commit their CSVs as `docs/benchmarks/<date>-p3-s<N>-...-a3d.csv`; each becomes the next stage's comparison point, and the Phase 3 exit compares against the Phase 2 exit files.

## Phase 3 · S1 measurement: transform vs submission, heap placement (2026-09-25, debug build, Analogue 3D)

Runs (`make BENCH=1`, boot to Bench = All, 15 s settle):

| File | ROM | What |
|---|---|---|
| `...-p3-s1-all-bootbench-debug-a3d.csv` | S1 | Bench = All: this stage's reference |
| `...-p3-s1-mesh-debug-a3d.csv` | S1, same boot | Bench = Mesh from the Debug tab (18 steps) |
| `...-p3-s1-all-bootbench-heappad-debug-a3d.csv` | S1 + `HEAP_PAD=0x100040` | every heap block 1 MB + 64 B further into RDRAM; code, symbols and static data identical |
| `...-p3-s1-all-bootbench-aa-debug-a3d.csv` | S1 again | a first `HEAP_PAD` build whose pad GCC had deleted (its `BENCH_META` says `heap_pad=1048640`, its `BENCH_LAYOUT` rows show the heap unmoved): the same layout booted twice |
| `...-p3-s1-mesh-rdplog-ares.csv` | S1 + `BENCH_RDPLOG=1`, ares | golden triangle hashes of the 18 Mesh steps ([DEBUGGING.md](DEBUGGING.md)) |

**Against the Phase 3 start (S13): 0 regressions.** Every mesh step is 3–6 % faster (`mesh_tris` at 16 pillars 4.66 → 4.36 ms) with the heap at the same addresses in both `BENCH_LAYOUT` rows. S1 added only debug code to `mesh_draw` (a skip branch, 10 instructions, otherwise the same code with renamed registers) and to the benchmark's pinned object loop. Objects 32 is 60.1 FPS with CPU 12.40 ms (S13: 58.6 FPS, 16.15 ms): in S13 the audio mix waited 3.0 ms for the RSP (D38) and here it did not, a threshold effect that flips between builds.

**Noise floor.** The A/A pair (the same layout, two boots) matches within ±0.3 % on every `BENCH_PROF` slot and ±0.4 % CPU, except where D38's wait is active (objects 32–64: CPU ±2 %, `mesh_tris` ±1.3 %).

**D37: not RDRAM placement.** Moving the heap by 1 MB (framebuffers, the RDP command buffers, libdragon's queue, textures, the pillar's geometry at its fixed colour) left every step's CPU within ±0.2 % (fill rate 8: +0.01 ms), RDP time unchanged, and every per-primitive slot within ±0.5 % (`mesh_tris` at 48/64 ±1.3 %, as in the A/A pair). The 6 % that S1 moved against S13 came with an unchanged heap and an unchanged loop body, so what moves per-primitive costs between builds is where code sits, beyond what `hot_text.py` models; the next candidate is code that runs while a phase draws but is not in its model, the interrupt handlers (ROADMAP_v2 S12).

**Transform vs submission (Bench = Mesh, one ROM).** A variant and its not-submitted twin run the same transforms, culls and lighting; the second skips `rdpq_triangle()`:

| Shape | n | Drawn tris | `mesh_tris` µs | not submitted | Submission share | per drawn triangle: all / engine / submission |
|---|---|---|---|---|---|---|
| pillars | 16 | 253 | 4,353 | 1,937 | 55.5 % | 17.21 / 7.66 / 9.55 |
| pillars | 32 | 506 | 8,430 | 3,850 | 54.3 % | 16.66 / 7.61 / 9.05 |
| pillars | 64 | 1,011 | 16,915 | 7,613 | 55.0 % | 16.73 / 7.53 / 9.20 |
| spheres | 16 | 467 | 16,088 | 11,615 | 27.8 % | 34.45 / 24.87 / 9.58 |
| spheres | 32 | 935 | 31,851 | 23,300 | 26.8 % | 34.07 / 24.92 / 9.15 |
| spheres | 64 | 1,867 | 63,519 | 46,478 | 26.8 % | 34.02 / 24.89 / 9.13 |

- libdragon's submission (`rdpq_triangle()`: the CPU converts the corners and writes the triangle to the RSP queue) costs 9.1–9.6 µs (~870 cycles) per triangle for both shapes, more than the engine's own work on a pillar. A vertex cache cannot reduce it.
- A pillar's engine share, 7.6 µs per drawn triangle, is mostly its three corner transforms: planar groups are lit once per face (3.1 µs per drawn triangle, outside `mesh_tris`).
- A sphere's engine share, 24.9 µs, is its per-triangle lighting (7.9 µs; curved groups light inside the triangle loop) and the transform of all 60 triangles to cull about half by winding: 180 corner transforms for 29 drawn triangles.
- Fog adds 4.8–5.2 µs per drawn triangle (mixed shapes: the per-corner fog factor and the larger shaded commands).
- The mixed variants, 16–64 objects: 27.6–28.5 µs per drawn triangle, 32.6–33.7 with fog.

**S3's target from this split.** The drawn triangles of a pillar use 25 unique vertices for 48 corners (−48 % transforms), a sphere's 72 for 180 (−60 %). At 1.6–2.1 µs per corner (transform, divide, clip tests and copies, estimated from the code) that is −13 to −21 % `mesh_tris` per drawn triangle on pillars and −16 to −22 % on spheres. S3's acceptance stays −12 % on pillars and becomes −15 % on spheres (was −25 %): a sphere's lighting stays per triangle until S5, and submission is a quarter of its cost. Build-to-build moves of this size (above) mean S3 is judged inside one ROM, with the current path still selectable.
