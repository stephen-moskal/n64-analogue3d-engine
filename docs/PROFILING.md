# Profiling

Tools for answering "where does the frame go?" on the Analogue 3D and in ares. Numbers and the benchmark baseline are in [BENCHMARKS.md](BENCHMARKS.md); logging and validation in [DEBUGGING.md](DEBUGGING.md).

## At a glance

| Tool | Where | What it measures | Cost |
|---|---|---|---|
| HUD `FPS … CPU x.x ms` | demo HUD | frame rate and average CPU work per frame | — |
| Overlay pages | D-Up / Debug → Overlay | stats, per-phase CPU, memory, frame-time histogram, RDP load | 2.3–4.2 ms with a page up, 0 when off |
| CSV dump | D-Down / Debug → Dump CSV | STATS, PROF_AVG/PEAK, RDP, RSP, FT, MEM rows over the debug log, ~2 s (120 menu-closed frames) after the request | one-off |
| Benchmark scene | Debug → Scene = Benchmark, or a `BENCH=1` ROM | stress run (26 steps for All), one BENCH and one BENCH_PROF row per step | — |
| `tools/bench_compare.py` | host or container | regression check between two benchmark runs | — |

All modules are in `src/debug/`.

## CPU profiler (`profiler.c/h`)

The engine times named scopes with the CPU tick counter (libdragon's `profile.h` only reports through `profile_dump()`, so it cannot feed the overlay):

```c
PROF_BEGIN(PROF_FLOOR);
floor_draw(&scene->camera, &scene->lighting);
PROF_END(PROF_FLOOR);
```

- Scopes compile out in release (`ENGINE_PROFILE=0`) and are skipped at runtime when Debug → Profiler is Off. Overhead measured on the A3D: ~0.1 ms per frame.
- A slot can be entered many times per frame (once per mesh); time and calls accumulate. Nested slots are timed separately, so a parent includes its children.
- Values: exponential moving average over ~32 frames (`avg_us`), last frame (`last_us`), and peak since boot or Reset Peaks (`peak_us`).
- `frame`, `wait_display` and `limiter` are always measured. **CPU work = frame − wait_display − limiter** (the HUD's CPU value).

Slots and nesting:

```
frame                 loop top to loop top (wall time)
  wait_display        blocked in display_get() for a free framebuffer = idle headroom
  limiter             30 FPS busy-wait
  update              scene_manager_update
    input / physics / particle_upd / scene_sys (camera + collision)
  draw                scene_manager_draw
    sky / floor / shadows / objects / particle_draw / hud / menu
      objects > mesh_cull / mesh_light / mesh_tris
  overlay             debug overlay page
  audio               snd_update, at the sound poll point (default: right after display_get; AUDIO.md)
```

`sky` and `objects` are timed in `scene_draw()` (the sky is the frame's background there; the benchmark times its own object loop); `floor`, `shadows`, `particle_draw`, `hud` and `menu` in the scene callbacks; the `mesh_*` slots inside `mesh_draw()`.

To add a scope: add a slot to `ProfSlot` in `profiler.h` and its name/depth to `slot_info` in `profiler.c`, then wrap the code. `PROF_BEGIN` declares a variable, so use a slot at most once per C scope. A new slot appears in the PROF CSV rows automatically, but on the Profiler overlay page only when it is added to the row list in `page_profiler()` (`overlay.c`). Step-by-step: [EXTENDING.md](EXTENDING.md).

## Unified stats (`stats.c/h`)

Per-frame counters written with `STATS_INC(field)`, `STATS_ADD(field, n)` and `STATS_SET(field, v)`; `stats_get()` returns the last complete frame. Kept in release builds. A new counter needs a field in `EngineStats`, a column appended to the end of the `STATS_HDR` / `STATS` rows in `stats.c` (existing column positions stay stable), and optionally a line on the Stats overlay page ([EXTENDING.md](EXTENDING.md)). Glossary:

| Counter | Meaning |
|---|---|
| `tris_mesh / floor / shadow / particle / ui` | triangles submitted to the RDP, by source |
| `tris_rejected_near / guard` | mesh triangles dropped at the near plane / guard band |
| `mesh_draws`, `mesh_culled_frustum` | `mesh_draw()` calls and those rejected by the bounding-sphere test |
| `groups_drawn`, `groups_culled_backface` | face groups submitted / skipped as back-facing |
| `tris_culled_backface` | curved-group triangles skipped by the per-triangle winding test (CSV column `tris_backface`, last) |
| `mode_changes` | `rdpq_set_mode_standard()` in `mesh_draw` |
| `tex_uploads`, `tex_upload_bytes` | texture loads issued (only for groups that survive culling) |
| `fill_rects` | fill rectangles (sky, benchmark layers) |
| `particles_alive / drawn` | particle pool usage |
| `colliders`, `collision_pairs`, `raycasts` | collision world size, overlapping pairs, raycasts this frame |
| `physics_bodies`, `physics_steps` | active bodies, fixed steps run this frame |
| `snd_voices`, `snd_buffers` | sound-effect voices playing; audio buffers mixed this frame (about 0.8 on average at 60 FPS; 2 after a long frame) |

## Memory (`memstats.c/h`)

RDRAM size and Expansion Pak flag, heap total/used/peak (`sys_get_heap_stats`), **heap delta** since boot or Reset Peaks (leak indicator: Reset Scene ×N should return to 0; Debug → Reset Soak automates the check), framebuffer and Z-buffer sizes, and the **stack high-water mark**: the 64 KiB stack at the top of RDRAM is painted at startup (interrupts disabled) and scanned every 30 frames.

## Frame time (`frametime.c/h`)

A 256-frame ring of loop time and CPU time: fps, average/min/max, p99, 1 % low (1000 / mean of the slowest 1 % of frames), CPU average/max, frames whose CPU work exceeded the budget by 10 %, and a 24-bucket histogram (1.5 ms per bucket). Pure C, unit-tested on the host.

With triple buffering the loop is paced by framebuffer availability, not vsync, so loop times alternate short/long (≈12.5 / 21 ms) at a steady 60 FPS (defect D19). Judge load by CPU time and fps, not by individual loop times.

## RDP load (hardware counters)

The RDP's cycle counters `DP_CLOCK`, `DP_BUSY`, `DP_PIPE_BUSY` and `DP_TMEM_BUSY` are read and reset once per loop and scaled by the measured counter rate. **On the Analogue 3D they tick at 93.75 MHz** (1.5× the 62.5 MHz RCP clock), so the code scales by the measured clock rather than assuming a frequency.

- **busy**: cycles the RDP had commands to process. Close to the frame time means RDP-bound.
- **pipe**: cycles the pixel pipeline was active (fill work).
- **tmem**: cycles spent loading TMEM.

Shown on the overlay's RSP page and in `RDP` CSV rows. Result so far: the demo is **CPU-bound**. RDP busy is 5–7 ms of 16.7, and pipe is only ~30 % of busy.

## libdragon's RSP profiler (blocked)

libdragon can also time each RSP microcode overlay (`rspq_profile.h`), but only when built with `RSPQ_PROFILE=1`, a hard `#define` in `libdragon/include/rspq_constants.h`. It cannot be used with this engine yet:

- At the original pin (`10f3bd43e`) the core `rsp_rdpq` microcode (and H.264) overflowed the RSP's 4 KB IMEM by 96 bytes.
- At the current pin (`39d0d6096`, Phase 2 S4b) `rsp_rdpq` fits, but the audio mixer's microcode overflows its DMEM data region by 8 bytes, and the mixer cannot be left out.

`tools/rspq_profile.ps1` and `tools/patches/rspq_profile.patch` were written for the original pin; the patch no longer applies. Retry at the next libdragon upgrade. Until then RDP load comes from the hardware counters (RDP rows) and CPU time from the profiler above.

When it works, the RSP page adds per-overlay RSP time and "Wait RDP" / "Wait CPU" (RSP blocked on the RDP, or starved by the CPU). Never commit the submodule while the patch is applied.

## CSV rows

All rows go through `debugf()` (debug builds), so the same capture works over `sc64deployer debug` and ares. The STATS, PROF, FT and MEM headers are printed once per boot, before the first dump; the BENCH headers at the start of every benchmark run:

```
STATS_HDR / STATS,<frame>,...            stats counters (stats.c)
PROF_HDR / PROF_AVG / PROF_PEAK          per-slot µs, averages and peaks (profiler.c)
RDP,<frame>,counter_mhz=...,busy_us=...  RDP counters (profiler.c)
RSP,<frame>,...                          RSP profile, "unavailable" unless RSPQ_PROFILE (profiler.c)
FT_HDR / FT,<frame>,count,fps,...        frame-time window + histogram (frametime.c)
MEM_HDR / MEM,<frame>,rdram,...          memory (memstats.c)
BENCH_META / BENCH_HDR / BENCH,...       benchmark run and steps (benchmark_scene.c, BENCHMARKS.md)
BENCH_PROF_HDR / BENCH_PROF,...          per-step CPU breakdown (profiler on)
BENCH_LAYOUT,...                         data addresses, once per run: render stack, pillar geometry and Mesh struct, plus one row per Layout copy (D26)
BENCH,END / BENCH,ABORTED                end of a benchmark run
SOAK,... / SWEEP,...                     Reset Soak and Menu Sweep (testbed.c, DEBUGGING.md)
RDPLOG_BEGIN ... RDPLOG_END              RDP command capture (rdp_debug.c, DEBUGGING.md)
```
