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
