# Development Roadmap v2

*Supersedes [ROADMAP.md](ROADMAP.md) (2026-09-12). v1 remains the delivery record for Features 1–10; this document plans everything from here.*

The engine's purpose: a **flexible, modular N64 engine that is efficient for developers to build different games on**, with **extensive debugging and benchmarking tooling**, and **as many modern graphics-engine features as the hardware honestly allows** — a "state of the art" N64 engine. Every step is **incremental and testable**, verified on the Analogue 3D through the SummerCart64, and measured.

## 0. How to read this document

- **Feature IDs.** Numbers from v1 are kept (Feature 8, Features 11–21). New work is `P<phase>.<n>`, e.g. `P1.3`.
- **Status vocabulary** (in every feature header): `planned → built → verified-host → verified-ares → verified-hw`. Only `verified-hw` (Analogue 3D via SummerCart64) counts as done for anything that touches the RDP, VI, audio, timing or memory. Pure-logic modules may stop at `verified-ares` plus host tests.
- **Template.** Every Phase 0–3 feature uses Appendix A: Problem / Solution / Files / Dependencies / Test plan / Benchmark metric & acceptance / Docs / Definition of Done. Phases 4–6 carry the v1 text plus a test plan and one metric each.
- **Phases are sequential by default**, but items inside a phase are ordered by dependency and can be picked up individually. Each phase ends with a benchmark row, so regressions are visible per phase.

## 1. Vision and guiding principles

Long-term vision (unchanged from v1): an action-RPG engine supporting souls-like combat and Final Fantasy Tactics-style battles, general enough for other genres. The first playable is a 1v1 arena; the tactics battle is the second track on the same foundation.

1. **Make each layer boring before building the next.**
2. **Verify on real hardware early and often.** ares is lenient; the Analogue 3D via SummerCart64 is the source of truth.
3. **Document as you build.** Every system gets a `docs/*.md` with architecture, API and the why.
4. **Respect the hardware.** 4 MB RDRAM, 4 KB TMEM, 16-bit Z. Design around the constraints.
5. **Prefer libdragon's built-in systems.** Build custom only when libdragon lacks it or the educational value justifies it.
6. **Measure before and after.** *(new)* No feature merges without a row in `docs/BENCHMARKS.md` showing what it cost or saved.
7. **Debug builds are strict, release builds are lean.** *(new)* Validators, asserts and the profiler are on in `BUILD=debug` and compiled out in `BUILD=release`; both variants build in CI.

## 2. Current state (2026-09-23)

**Delivered (v1 Features 1–7, 9, 10):** mesh system + shape library, multi-object scenes with selection/manipulation, audio (BGM + SFX, 16-channel mixer), billboards, configurable sun + 4 point lights + blob/projected shadows, 128-particle system with direct RDP batching, fog/atmosphere with 7 presets and sky gradient, semi-fixed-timestep physics, remappable action mapping with a Controls tab, tabbed menu, text. Details in [ROADMAP.md](ROADMAP.md).

**Delivered in v2:** Phase 0 (Windows 11 environment, 2026-09-12) and Phase 1 (developer tooling, 2026-09-23): debug/release builds, a Debug menu tab, unified stats, CPU profiler, memory stats, frame-time history, overlay pages, RDP hardware-counter load, benchmark scene with a committed A3D baseline, host unit tests, CI, crash test and RDP frame capture. See §5.14–§5.15 and [BENCHMARKS.md](BENCHMARKS.md).

**Size:** 33 `.c` modules, ~10,300 lines under `src/` (of which `src/debug/` ~1,700), plus ~720 lines of host tests in `tests/host/`.

**Not started from v1:** Feature 8 (sprite animation), Milestone 1 (Tiny3D + GLTF), Milestones 2–3.

**In progress:** Phase 2, engine hardening (§6): S0–S3 verified on the A3D 2026-09-23; S4 (mesh right-sizing) next.

**Phase 0 environment numbers** (performance numbers are in [BENCHMARKS.md](BENCHMARKS.md)):

| Measurement | Value |
|---|---|
| Toolchain | libdragon submodule `10f3bd43e` (preview, 2026-02-27) compiled by GCC 16.2.0 in `ghcr.io/dragonminded/libdragon:latest` |
| `libdragon init` (build libdragon into the container) | ~50 s (16 cores) |
| `libdragon make` | ~5 s warm |
| ROM (2026-09-23) | `engine-debug.z64` 360 KB, `engine.z64` 352 KB (was `hello_cube.z64` 344,064 B at Phase 0) |
| Upload (sc64deployer 2.20.2, SC64 firmware v2.20.2) | 0.2 s |
| Emulator | ares v148 (Homebrew Mode) |

**Known-defect register** (2026-09 assessment plus Phase 1 findings). Status: `open`, `fixed <commit>`, or the Phase 2 stage that owns it (§6).

| # | Defect | Where (2026-09-23) | Status |
|---|---|---|---|
| D1 | Leak on Reset Scene: `texture_init()` re-loads slots 0–5 without freeing; `demo_cleanup` never calls `texture_cleanup()`. **Measured on the A3D: Reset Soak (S0) +134,880 B over 10 resets = 13.5 KB per reset** (P1.4 had estimated ~20 KB including a warm-up reset); the six sprites explain ~12 KB | `src/render/texture.c:17-26`, `src/scenes/demo_scene.c:1299-1324` | fixed S1 (Reset Soak ×10: 0 B) |
| D2 | Flat-color materials drawn with `TRIFMT_ZBUF_TEX`. Found by the RDP validator's first hardware run (~41,000 warnings, which also made the first debug build crawl) | `src/render/mesh.c` | **fixed** fad76ab |
| D3 | Lighting and back-face culling use one normal per face group (the group's first vertex) and the object-centre view direction: spheres shade wrong, near/large meshes cull wrong faces. Visible symptom: see D24 | `src/render/mesh.c:170-177, 243-265` | fixed S2 (flat groups: exact plane test; curved groups: per-triangle winding cull and lighting) |
| D4 | Texture upload happened before the group cull (wasted TMEM loads, `U:` over-reported) | `src/render/mesh.c` | **fixed** 907754d |
| D5 | `rdpq_mode_alphacompare(1)` is never cleared; state leaks to later groups/draws | `src/render/mesh.c:233-235` | fixed S2 (mode reset when alpha cutout changes) |
| D6 | `camera.dirty = true` forced every frame defeats the dirty flag (6 `sqrtf` + trig per frame) | `src/scenes/demo_scene.c:1000`, `benchmark_scene.c:336` | fixed S3 (camera rebuilds only when dirty or following; setters mark dirty) |
| D7 | HUD raycast and visible-object scan ran with the HUD hidden | `src/scenes/demo_scene.c` | **fixed** f4628e0 |
| D8 | Every `Mesh` mallocs full capacity (16,384 + 2,048 B) regardless of size | `src/render/mesh_build.c` (`mesh_add_vertex`, `mesh_add_triangle`) | open → S4 |
| D9 | Screen size (320/240) and guard-band constants duplicated across the renderer | `main.c:21-23`, `mesh.c:127-130,337`, `floor.c:21-24,121`, `shadow.c:12-15,90,166`, `particle.c:54-57,431`, `atmosphere.c:309-346`, `camera.c:164`, `scene.c:244`, `benchmark_scene.c:419` | open → S6 |
| D10 | Menu option strings live in `main.c`, their meanings in `demo_scene.c`, coupled by raw index with no bounds checks | `src/main.c:34-158`, `src/scenes/demo_scene.c:250-365` | open → S8 |
| D11 | `PhysicsWorld` is a demo static; `SceneObject.collider_handle` never used (demo keeps `obj_colliders[]`); scene texture manager unused | `src/scenes/demo_scene.c:84-89,162`, `src/scene/scene.c:17-21,96-98` | open → S1 (textures), S7 |
| D12 | Three input paths: action layer, raw joypad in `menu.c`, raw START/shortcut/abort reads | `src/ui/menu.c` (`menu_update`), `demo_scene.c` (Start handling in `demo_update`), `debug_menu.c` (D-Up/D-Down shortcuts), `benchmark_scene.c` (abort) | open → S9 |
| D13 | `ParticleBlendMode` declared and set but the renderer hardcodes additive (point spawn already works implicitly) | `src/render/particle.c:341-346` | open → S10 |
| D14 | Projected shadows transform every vertex twice per triangle with no culling (+16.6 ms for 16 casters); particle→emitter lookup is O(particles × emitters); the sky repaints the cleared framebuffer; collision queries scan all 64 slots | `shadow.c:114-191`, `particle.c:248-259`, `scene.c:63` + `atmosphere.c:307-347`, `collision.c:32,473,510,538` | fixed S3 (projected shadows −69 % of pass cost, particles −34 % at 128, sky replaces the clear, collision scans bounded by `CollisionWorld.high`) |
| D15 | Makefile had no `all:`, no `.d` includes, hand-listed `OBJS` | `Makefile` | **fixed** 642135c |
| D16 | Physics (Feature 9) only verified in ares | — | open → S11 |
| D17 | Committed generated `filesystem/` assets (a version-2 `.wav64` asserted `invalid version` at boot) | `.gitignore` | **fixed** d0d5bc0 |
| D18 | **Frame-overrun flicker on the Analogue 3D.** Frames over 16.7 ms (debug build + RDP validator + menu open) flicker in the lower screen area on the A3D (HDMI, 60 Hz 4K TV); ares shows only the FPS drop; the release ROM does not flicker. P1.3 measured the trigger: with the validator on, `menu_draw` peaks at 6–9 ms (text costs ~4× more under the validator). Mitigated by the validator being off at boot. **S0 Overload result: frame overruns alone do not flicker** (validator on or off, down to 26 FPS), so the flicker needs the menu on screen during an overrun: the menu drawing path (translucent background triangles, text) is the suspect | `src/main.c` frame loop, `src/ui/menu.c`, `src/ui/text.c` | open → S5 (menu path), re-check in S6 |
| D19 | **Loop pacing jitter.** Under triple buffering the loop is paced by framebuffer availability, not vsync: at a steady 60 FPS iterations alternate ~12.5 / ~21 ms (P1.5). `dt` inherits it (possible micro-stutter) | `src/main.c:189-192, 250` | open → S6 |
| D20 | The runtime physics ball never gets a shadow and is never selectable: `selectable_object_count` is snapshotted before billboards and the ball are added, and the shadow loop is bounded by it | `src/scenes/demo_scene.c` (`selectable_object_count` set in `demo_init`; shadow loop in `demo_draw`) | open → S7 |
| D21 | `texture_init()` resets `slot_count` to 6; if billboard slots were loaded first, `texture_upload` would assert | `src/render/texture.c:17-29` | fixed S1 |
| D22 | `assets/grass_tex.png` is unused but still packed into the ROM (2.9 KB) | `Makefile` wildcard, `assets/` | open → S12 |
| D23 | The demo re-applies point lights, control bindings, background colour and Environ item states every frame instead of on change | `src/scenes/demo_scene.c:1047-1135` | fixed S3 (point lights, torches, bindings, Environ disabled states on change; the background colour is still a per-frame 4-byte assignment by design) |
| D24 | Spheres vanish when seen from their −Z side, which in the demo is usually a view with the pillars or cube in front, so it looks like a Z-buffer fault. Cause is D3: every latitude-band group of the UV sphere starts at longitude 0 (+Z), so the group back-face test culls almost the whole sphere. Reported on the A3D 2026-09-23 | `src/render/mesh.c:243-265`, `src/render/mesh_defs.c:279-365` | fixed S2 (A3D 2026-09-23) |
| D25 | Render-loop speed depends on code placement. The VR4300 I-cache is 16 KB and direct-mapped: the first S2 triangle loop shared all 46 cache lines of `rdpq_triangle_rsp`, costing ~1,100 cycles per triangle (+38 % CPU in a same-ROM A/B), and unrelated edits moved benchmark CPU by ~6 % between builds. libdragon's `-ftrivial-auto-var-init=pattern` also memset the triangle scratch arrays on every triangle | `Makefile`, `src/engine/hot_text.ld`, `src/render/*.c` | fixed S2 (hot-text block, `tools/hot_text.py` in CI, `ENGINE_NOINIT`); S3: texture uploads run ~7 KB of libdragon code between face groups and cannot be pinned (the mesh phase would need ~17 KB), so `mesh_draw` skips repeat uploads (Textures bench −22 %) and the checker follows callees transitively |
| D26 | Data layout moves render timings by up to ~9 %: identical `mesh_draw` code on two copies of the same pillar data differed by 9 % in one ROM (Mesh A/B), most likely 8 KB direct-mapped D-cache aliasing between vertex data and stack scratch | `src/render/mesh.c`, mesh heap allocations | open → S4. S3 evidence: `BENCH_LAYOUT` showed the pillar's vertex/index data sharing D-cache sets with the render stack in the first S3 build but not in the final one, and their Objects runs differ by ≤ 3 %, so stack/vertex aliasing does not explain the 9 %. Next: a same-ROM A/B of two data copies with logged addresses, alongside the S4 `mesh_finalize` allocations |
| D27 | At boot the scene keeps `lighting_init()`'s ambient (0.15) while the Lighting tab shows Ambient 20 % (0.20): the demo applies menu values only on change and its lighting caches start at the menu defaults, so the menu and the scene disagree until a Lighting item changes | `src/scenes/demo_scene.c` (`last_*` lighting caches, `demo_init`) | open → S8 (settings module: the menu is the single source of truth) |

## 3. Phase overview

| Phase | Name | Status | Exit criterion |
|---|---|---|---|
| 0 | Environment re-establishment & baseline (Windows 11) | ✔ **complete** 2026-09-12 (d0d5bc0) | ROM builds via Docker on Windows, boots in ares and on the Analogue 3D, `debugf` visible on both channels, baseline recorded, `docs/SETUP.md` reproducible from a clean machine |
| 1 | Developer tooling & benchmarking foundation | ✔ **complete** 2026-09-23 (642135c..b6fba17); CI green on GitHub Actions from the first push | Overlay pages (stats, profiler, memory, frame time, RSP), benchmark scene + CSV + baseline table, debug/release variants, CI green, DEBUGGING / PROFILING / BENCHMARKS / HARDWARE docs |
| 2 | Engine hardening | **in progress** — §6 (stages S0–S13); S0–S3 verified 2026-09-23 | Defect register closed (except items explicitly deferred), 0 B heap growth over 10 resets, heap −90 KB, demo CPU −15 %, menu frames within budget, one input path, `demo_scene.c` no longer owns menu semantics, physics verified-hw (§6.3) |
| 3 | Graphics features independent of Tiny3D | planned | Vertex cache, Gouraud, sprite animation, CI4/TMEM residency, fonts, VI options, decals, skybox, sorted transparency — each with a BENCHMARKS row |
| 4 | Milestone 1: libdragon upgrade + Tiny3D + GLTF (F11–16) | planned | Blender → ROM pipeline; ≥ 64 pillars at 60 FPS on hardware; both render paths measured by the Phase 1 tools |
| 5 | Milestone 2: animation & characters (F17–21) | planned | Stick-controlled animated character, entity pattern, FFT grid track |
| 6 | Milestone 3: game framework | planned | Souls-like 1v1 arena, FFT battle loop, save/load, AI |

**Why this order.** Phase 0 isolates environment faults by building with the unchanged Makefile. Phase 1 comes before engine or graphics work because every later phase is judged by numbers (Tiny3D's value is "objects per frame at 60 FPS", hardening's value is "ms saved and KB freed") and because the RDP validator in debug builds turns "works in ares, hangs on hardware" from a mystery into a log line. Phase 2 removes debt before it is copied into a second render path. Phase 3 takes the features that are valuable on the layers that stay CPU-rendered under Tiny3D (floor, particles, billboards, shadows, HUD) and that raise the demo's visual bar now. Phase 4 adds P4.0 (submodule upgrade, Tiny3D vendored and built in the container and in CI, fresh baseline) before the v1 Features 11–16. Phases 5–6 carry Milestones 2–3 with the template applied lightly.

---

## 4. Phase 0 — Environment re-establishment & baseline

### P0.1 Windows 11 toolchain & first build — status: verified-hw

**Problem.** Development moved from macOS to a Windows 11 machine with nothing installed; the docs were macOS-only; the working tree was CRLF (Git for Windows `core.autocrlf=true`), which breaks `make`/`bash` inside the Linux build container; the SummerCart64 had no USB driver.

**Solution (as executed).** WSL2 (`wsl --install --no-distribution`, reboot) → Docker Desktop → Node 24 + `npm i -g libdragon` → ares via winget (Homebrew Mode) → sc64deployer to `C:\tools\sc64deployer` → FTDI CDM driver via `pnputil` (cart appears as `serial://COM3`) → `.gitattributes` (`* text=auto eol=lf`) plus `core.autocrlf=false`/`core.eol=lf` in the repo and the submodule, re-checkout → `libdragon init` → `libdragon make`. The stale committed `.wav64` files (D17) asserted at boot; `make clean` + rebuild regenerated them, and they are no longer tracked.

**Files.** `.gitattributes`, `.gitignore`, `.vscode/tasks.json` (per-OS commands), `docs/SETUP.md`, `docs/WORKFLOW.md`, `README.md`, `CLAUDE.md`.

**Test plan.** ares: scene, menu, `SMozN64 Dev Engine` in the log. Hardware: `sc64deployer upload` + reset, scene runs, `sc64deployer debug` shows the asset-load lines and the banner. ✔ both on 2026-09-12.

**Benchmark metric.** Build times, ROM/ELF sizes (table in §2).

**Definition of Done.** ✔ Clean-machine walkthrough of SETUP.md reproduces the build; both log channels work; generated assets rebuild from `assets/`; `git ls-files --eol` shows `w/lf`.

### P0.2 Baseline capture — status: verified-hw (completed through Phase 1)

*Outcome:* the numbers are in [BENCHMARKS.md](BENCHMARKS.md): demo CPU profile, memory, RDP load and the 26-step benchmark baseline (`docs/benchmarks/2026-09-23-baseline-debug-a3d.csv`), all on the Analogue 3D. The physics-ball hardware check moved to P2.10 (S11); canonical-view screenshots are folded into the Phase 2 exit (S13).

**Problem.** No recorded reference numbers; the RAM figure in v1 is an estimate; physics was never seen on hardware.

**Solution.** Record into `docs/BENCHMARKS.md` (created in P1.8; until then in §2): ROM/ELF/DFS sizes (done), build/upload times (done), HUD FPS + `T:`/`U:`/`OBJ`/`VIS` on ares and hardware in five canonical camera views with point lights and fog on/off, the physics ball on hardware, ares version, SC64 firmware. Re-shoot the five `docs/images/*.png` views.

**Test plan.** Hardware checklist §11.2 items 1–6 against the unchanged ROM.

**Definition of Done.** Dated, commit-tagged table; Feature 9 hardware result recorded; screenshots archived under `docs/images/verification/baseline/`.

### P0.3 Documentation refresh — status: done (d0d5bc0)

Cross-platform SETUP.md (Windows 11 primary), WORKFLOW.md without the aspirational Tiny3D snippets, CLAUDE.md with the real layout and gotchas, README prerequisites/quick start, v1 roadmap banner. Remaining stale docs (MENU_SYSTEM.md details, RENDERING.md source table, missing AUDIO/PARTICLES/HARDWARE docs) are scheduled in §12.

### P0.4 Repo hygiene — status: done (d0d5bc0)

Generated outputs and the ares `.pak` save untracked and ignored (D17); `.gitattributes` committed. The ROM rename to `engine.z64` / `engine-debug.z64` happened in P1.1 (642135c). Left for P2.11 (S12): the orphan `assets/grass_tex.png` (D22).

---

## 5. Phase 1 — Developer tooling & benchmarking foundation

Goal: answer "where does the frame go, and did this change make it better or worse?" on both ares and hardware, cheaply, every day.

> §5.1–§5.12 are the design sketches written before implementation; names, CSV rows and file layout differ in places from what was built. The reference for the implemented tools is the code and [DEBUGGING.md](DEBUGGING.md), [PROFILING.md](PROFILING.md) and [BENCHMARKS.md](BENCHMARKS.md); §5.13–§5.15 record what was delivered.

### 5.1 Module layout

```
src/debug/engine_debug.h        build switches: ENGINE_DEBUG / ENGINE_PROFILE / ENGINE_STATS; ENGINE_ASSERT, ENGINE_LOG
src/debug/profiler.c/h          named CPU scope timers (TICKS_READ), moving averages, peaks, CSV dump, RSP/RDP bridge
src/debug/stats.c/h             unified per-frame counters (replaces TextureStats)
src/debug/memstats.c/h          heap (sys_get_heap_stats / mallinfo), stack watermark, FB/Z sizes, RDRAM size
src/debug/frametime.c/h         256-frame ring: avg / min / max / p99 / 1 %-low, histogram
src/debug/overlay.c/h           on-screen pages: Stats, Profiler, Memory, FrameTime, RSP
src/debug/rdp_debug.c/h         rdpq_debug_start() gating, one-frame RDP log capture, TMEM dump
src/scenes/benchmark_scene.c/h  deterministic stress harness with CSV output
tests/host/                     host (gcc) unit tests for pure-C modules + a small libdragon.h shim
tools/bench_compare.py          baseline.csv vs new.csv with a regression threshold
tools/rdp_log_to_hex.py         rdpq_debug_log text -> hex for `rdpvalidate`
tools/rom_budget.py             parses the .map, enforces ROM/BSS caps
.github/workflows/build.yml     CI: both variants + host tests in the libdragon image
```

### 5.2 Build switches — `engine_debug.h`

```c
#ifndef ENGINE_DEBUG
#define ENGINE_DEBUG   1          // Makefile passes -DENGINE_DEBUG=0 for BUILD=release
#endif
#ifndef ENGINE_PROFILE
#define ENGINE_PROFILE ENGINE_DEBUG
#endif
#ifndef ENGINE_STATS
#define ENGINE_STATS   1          // counters are cheap; kept in release unless overridden
#endif
#if ENGINE_DEBUG
  #define ENGINE_ASSERT(cond, ...)  assertf(cond, __VA_ARGS__)
  #define ENGINE_LOG(...)           debugf(__VA_ARGS__)
#else
  #define ENGINE_ASSERT(cond, ...)  ((void)0)
  #define ENGINE_LOG(...)           ((void)0)
#endif
```

libdragon already compiles `debugf`/`assertf`/`debug_init_*` to no-ops under `-DNDEBUG`, and its `PROFILE_START/STOP` macros vanish with `-DLIBDRAGON_PROFILE=0`, so a release build carries no logging or profiling code.

### 5.3 Profiler — `profiler.h`

The engine owns its scope timers (two `TICKS_READ()` per scope) because libdragon's `profile.h` exposes per-slot data only through `profile_dump()`; the HUD, histogram and CSV need the values every frame. An optional `-DENGINE_PROFILE_LIBDRAGON=1` mirrors each scope into `PROFILE_START/STOP` so `profile_dump()` and emulator profilers see the same slots.

```c
typedef enum {
    PROF_FRAME,            // display_get() return -> rdpq_detach_show() return (+ audio)
    PROF_WAIT_DISPLAY,     // time blocked in display_get()
    PROF_INPUT, PROF_UPDATE, PROF_PHYSICS, PROF_PARTICLE_UPDATE,
    PROF_SKY, PROF_FLOOR, PROF_CULL, PROF_TRANSFORM, PROF_LIGHTING, PROF_SUBMIT,
    PROF_SHADOWS, PROF_PARTICLE_DRAW, PROF_HUD, PROF_OVERLAY, PROF_AUDIO,
    PROF_SLOT_COUNT
} ProfSlot;

typedef struct {
    uint32_t last_ticks[PROF_SLOT_COUNT];   // completed frame
    uint16_t calls[PROF_SLOT_COUNT];
    float    avg_us[PROF_SLOT_COUNT];       // 64-frame moving average
    float    peak_us[PROF_SLOT_COUNT];      // since profiler_reset_peaks()
    uint32_t frame_index;
} ProfilerFrame;

void  profiler_init(void);
void  profiler_frame_begin(void);
void  profiler_frame_end(void);             // rolls counters, frametime_record(), rspq_profile_next_frame()
const ProfilerFrame *profiler_get(void);
const char *profiler_slot_name(ProfSlot s);
void  profiler_reset_peaks(void);
void  profiler_dump_csv(void);              // debugf("PROF,<frame>,<slot0_us>,...\n")
static inline void profiler_record(ProfSlot s, uint32_t ticks);

#if ENGINE_PROFILE
  #define PROF_BEGIN(slot)  uint32_t __prof_t0_##slot = TICKS_READ()
  #define PROF_END(slot)    profiler_record(slot, TICKS_DISTANCE(__prof_t0_##slot, TICKS_READ()))
#else
  #define PROF_BEGIN(slot)  ((void)0)
  #define PROF_END(slot)    ((void)0)
#endif

// RSP/RDP bridge — valid only when the installed libdragon was built with RSPQ_PROFILE=1
typedef struct {
    bool  available;
    float rdp_busy_pct;
    float rsp_overlay_pct[RSPQ_PROFILE_SLOT_COUNT];
    const char *overlay_name[RSPQ_PROFILE_SLOT_COUNT];
} RspProfile;
bool profiler_rsp_get(RspProfile *out);     // rspq_profile_get_data() under #if RSPQ_PROFILE, else available=false
```

**RSP/RDP numbers need an instrumented libdragon.** `RSPQ_PROFILE` is a hard `#define RSPQ_PROFILE 0` in `libdragon/include/rspq_constants.h`, not overridable from project CFLAGS. P1.7 documents the procedure: flip it to 1 in the submodule (kept as `tools/patches/rspq_profile.patch`), `libdragon install`, and the engine detects it with `#if RSPQ_PROFILE`. Normal builds show "RSP profile not available" on the RSP page.

### 5.4 Unified stats — `stats.h`

```c
typedef struct {
    uint16_t objects_total, objects_drawn, objects_culled_frustum;
    uint16_t groups_drawn, groups_culled_backface;
    uint32_t tris_submitted, tris_rejected_near, tris_rejected_guard;
    uint32_t draw_calls, fill_rects, mode_changes;
    uint16_t tex_uploads, tex_uploads_skipped; uint32_t tex_upload_bytes;
    uint16_t particles_alive, particles_drawn, emitters_active, billboards_drawn, shadows_drawn;
    uint16_t colliders, collision_pairs, raycasts, physics_bodies, physics_steps;
    uint16_t sfx_playing;
} EngineStats;

extern EngineStats g_stats_cur;             // written during the frame
void stats_frame_begin(void);               // cur -> last, zero cur
const EngineStats *stats_get(void);         // last completed frame
void stats_dump_csv_header(void);
void stats_dump_csv_row(void);
#if ENGINE_STATS
  #define STATS_ADD(field, n) (g_stats_cur.field += (n))
#else
  #define STATS_ADD(field, n) ((void)0)
#endif
#define STATS_INC(field) STATS_ADD(field, 1)
```

Migration: `texture_stats_*` become thin wrappers for one phase, then go; `texture_upload()` counts only when a load is actually issued (and, after P3.3, `tex_uploads_skipped` on residency hits); `mesh.c` moves the upload after the group cull (D4); `demo_post_draw` reads `stats_get()`.

### 5.5 Memory stats — `memstats.h`

```c
typedef struct {
    int  rdram_total; bool expansion_pak;                       // get_memory_size(), is_memory_expanded()
    int  heap_total, heap_used, heap_free, heap_largest_free;   // sys_get_heap_stats() + mallinfo()
    int  fb_bytes, zbuf_bytes;                                  // 3 x 153,600 and 153,600 at 320x240x16
    int  stack_painted, stack_watermark;                        // paint-and-scan
} MemStats;
void memstats_init(void);       // paint the stack region below SP with a pattern
void memstats_update(void);     // every 60 frames: heap stats + scan for the high-water mark
const MemStats *memstats_get(void);
void memstats_dump(void);       // debugf("MEM,heap_used,heap_free,largest,stack_wm\n")
```

Leak protocol: the Memory page shows `heap_used`; "Reset Scene" ×10 must return to the same value. The tooling should demonstrate D1 (~12 KB per reset) before Phase 2 fixes it.

### 5.6 Frame-time history — `frametime.h`

```c
#define FRAMETIME_WINDOW  256
#define FRAMETIME_BUCKETS 24            // 1.5 ms buckets, 0-36 ms, last bucket = overflow
typedef struct { float fps, avg_ms, min_ms, max_ms, p99_ms, low1_fps; uint16_t over_budget; } FrameTimeStats;
void frametime_record(uint32_t frame_ticks);   // from profiler_frame_end()
void frametime_get(FrameTimeStats *out);       // low1_fps = 1000 / mean of the slowest 1 % of frames
const uint16_t *frametime_histogram(int *count, float *bucket_ms);
void frametime_reset(void);
```

Budget line = 16.67 ms (60 FPS) or 33.3 ms (30 FPS) from the target frame rate.

### 5.7 Overlay pages — `overlay.h`

```c
typedef enum { OVERLAY_OFF, OVERLAY_STATS, OVERLAY_PROFILER, OVERLAY_MEMORY,
               OVERLAY_FRAMETIME, OVERLAY_RSP, OVERLAY_PAGE_COUNT } OverlayPage;
void overlay_init(void);
void overlay_set_page(OverlayPage p);
OverlayPage overlay_get_page(void);
void overlay_draw(void);                   // after scene post_draw, before menu_draw
void overlay_request_csv_dump(void);       // next frame: profiler + stats + memstats CSV rows over debugf
```

- Selection (as built): a dedicated **Debug** menu tab (Overlay, Profiler, RDP Check, Dump CSV, Reset Peaks, Scene, Bench, RDP Log, Crash Test) plus fixed shortcuts D-Up (cycle pages) and D-Down (dump CSV) while those buttons are unbound. The shortcuts read the raw joypad for now; P2.8 (S9) moves them onto the action layer.
- Rendering: dark panel via `rdpq_set_mode_fill` + `rdpq_fill_rectangle` (rectangles only — hardware-safe), labels via `text_draw_fmt`, bars via fill rectangles at 10 px/ms with a budget marker, green/yellow/red at < 60 % / < 90 % / ≥ 90 % of budget.
- `PROF_OVERLAY` measures the overlay itself (expect < 0.3 ms with a page on, 0 when off).

### 5.8 Main-loop instrumentation (`src/main.c`; moves to `src/engine/engine.c` in P2.5 / S6)

```c
profiler_init(); memstats_init(); overlay_init();
#if ENGINE_DEBUG
rdpq_debug_start();                         // right after rdpq_init()
#endif
while (1) {
    PROF_BEGIN(PROF_WAIT_DISPLAY); surface_t *fb = display_get(); PROF_END(PROF_WAIT_DISPLAY);
    profiler_frame_begin(); stats_frame_begin();
    PROF_BEGIN(PROF_FRAME);
      /* dt as today */
      PROF_BEGIN(PROF_UPDATE); scene_manager_update(&mgr, dt); PROF_END(PROF_UPDATE);
      rdpq_attach(fb, &zbuf);
      scene_manager_draw(&mgr);             // sky/floor/objects/shadows/particles/HUD bracketed inside their modules
      overlay_draw();
      rdpq_detach_show();
      PROF_BEGIN(PROF_AUDIO); snd_update(); PROF_END(PROF_AUDIO);
    PROF_END(PROF_FRAME);
    memstats_update(); profiler_frame_end();
}
```

`mesh_draw()` brackets `PROF_CULL`, `PROF_TRANSFORM`, `PROF_LIGHTING`, `PROF_SUBMIT`; `demo_update` brackets `PROF_INPUT`, `PROF_PHYSICS`, `PROF_PARTICLE_UPDATE`; `sky_draw`, `floor_draw`, `shadow_*`, `particle_draw` and the HUD bracket their own slots. Nested scopes accumulate independently, so `PROF_FRAME` is wall time and the sub-slots explain it.

### 5.9 Build variants — Makefile (P1.1)

```make
BUILD     ?= debug                          # debug | release
BUILD_DIR  = build/$(BUILD)
SOURCE_DIR = src
include $(N64_INST)/include/n64.mk
ROM_NAME   = engine$(if $(filter release,$(BUILD)),,-debug)
N64_ROM_TITLE = "SMozN64 Engine"
CFLAGS += -I$(SOURCE_DIR)
ifeq ($(BUILD),release)
  N64_CFLAGS += -DNDEBUG -DLIBDRAGON_PROFILE=0 -DENGINE_DEBUG=0 -DENGINE_PROFILE=0
else
  N64_CFLAGS += -DENGINE_DEBUG=1 -DENGINE_PROFILE=1
endif
SRCS := $(wildcard $(SOURCE_DIR)/*.c $(SOURCE_DIR)/*/*.c)
OBJS := $(SRCS:$(SOURCE_DIR)/%.c=$(BUILD_DIR)/%.o)
all: $(ROM_NAME).z64
$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs
$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)
-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/*/*.d)
.PHONY: all clean
```

Separate `build/<variant>` directories prevent stale objects; `-include` of the `.d` files fixes D15; the ROM is renamed from `hello_cube` to `engine` (docs, tasks and the deployer command follow). The cost of `rdpq_debug_start()` is measured once and recorded, so BENCHMARKS rows always state the variant.

### 5.10 Benchmark scene (P1.8)

```c
typedef enum { BENCH_OBJECTS, BENCH_PARTICLES, BENCH_LIGHTS, BENCH_TEXTURES,
               BENCH_SHADOWS, BENCH_FILLRATE, BENCH_ALL } BenchKind;
typedef struct { BenchKind kind; int frames_per_step; bool loop; } BenchConfig;   // default 300 frames/step
Scene *benchmark_scene_get(void);
void   benchmark_scene_configure(const BenchConfig *cfg);
```

Deterministic: fixed orbital camera at 0.2 rad/s, seeded RNG, no menu. Steps:

| Bench | Ramp | What it finds |
|---|---|---|
| OBJECTS | 8, 16, 24, 32, 48, 64 pillars (32 tris each) on a grid | the CPU-path ceiling v1 estimated at 20–30 objects |
| PARTICLES | 32, 64, 96, 128 alive | particle update/draw cost |
| LIGHTS | 0, 1, 2, 4 point lights over OBJECTS(16) | lighting cost per light |
| TEXTURES | 1, 2, 4, 8 distinct textures over OBJECTS(16) | TMEM upload cost (motivates P3.3) |
| SHADOWS | off, blob ×16, projected ×16 | shadow cost |
| FILLRATE | 1, 2, 4, 8 full-screen textured layers | RDP-bound fill rate (Mpix/s) |

One CSV line per step over `debugf`: `BENCH,<kind>,<step>,<param>,<frames>,<avg_ms>,<p99_ms>,<low1_fps>,<tris>,<uploads>,<heap_used>,<rdp_busy_pct|-1>`, then `BENCH,END`. Selected by a Settings item **Scene** (Demo/Benchmark) or `-DENGINE_BOOT_SCENE=benchmark` for unattended runs. Capture: hardware `sc64deployer debug | Tee-Object docs/benchmarks/<date>-<commit>-hw.csv`; ares terminal for `-ares.csv`. `tools/bench_compare.py baseline.csv new.csv --threshold 0.05` prints a table and exits 1 on regression.

### 5.11 CI and host tests (P1.9)

`.github/workflows/build.yml`: job `rom` runs in `ghcr.io/dragonminded/libdragon:latest` (same tag as `.libdragon/config.json`), checks out with submodules, `make -C libdragon install` (cached by submodule SHA), `make BUILD=debug` and `make BUILD=release`, `tools/rom_budget.py`, uploads `*.z64`, `.sym`, `.map`. Job `host-tests` builds `tests/host` with the host gcc and runs it.

Host tests cover the pure-C modules: `test_vec3.c`, `test_collision.c` (sphere/sphere, sphere/AABB, ray/sphere, layer masks, nearest hit), `test_physics.c` (free fall matches ½gt² within 1 %, restitution, rest detection, max-steps clamp), `test_action.c`, `test_camera_math.c` (`mat4_*`, `camera_sphere_visible`), `test_frametime.c`. `tests/host/shim/libdragon.h` provides `color_t`/`RGBA32`, `joypad_*` stubs backed by a test-settable state, and `fm_*` → libm. `collision.c`, `physics.c` and `vec3.h` use no libdragon symbols; `action.c` uses only the joypad calls. Phase 2 moves `#include <libdragon.h>` out of `action.h`/`camera.h`/`lighting.h` into the `.c` files so the shim stays small.

### 5.12 Crash diagnostics and RDP validation (P1.10)

- The `.sym` file is already embedded in the ROM by n64.mk, so the on-console inspector shows a symbolized backtrace and also writes it to `debugf` (visible over `sc64deployer debug`). CI keeps `.sym`/`.map` as artifacts.
- `docs/DEBUGGING.md` covers: log channels, inspector pages, `assertf`, `debug_backtrace()`, `rdpq_debug_start()` messages and what the common ones mean here (fill-mode triangles, `TRIFMT_ZBUF_*` without Z attached, combiner/format mismatches, TMEM overflow), one-frame RDP capture (`rdp_debug_capture_next_frame()` around `rdpq_debug_log(true/false)`), `tools/rdp_log_to_hex.py` → `libdragon exec rdpvalidate` offline, and `rdpq_debug_get_tmem()` dumps.

### 5.13 Phase 1 features

| ID | Feature | Problem → Solution | Files | Deps | Test (ares / hw / host) | Metric & acceptance | Docs | Status |
|---|---|---|---|---|---|---|---|---|
| P1.1 | Build variants & Makefile restructure | D15; no debug/release split → §5.9 | `Makefile`, `src/debug/engine_debug.h`, `.vscode/tasks.json` (debug/release tasks), docs/CLAUDE.md (ROM name) | P0.1 | both ROMs boot on ares + hw; a header edit triggers a rebuild; a deliberate fill-mode triangle in a scratch scene is reported by the validator in debug only | release ROM ≤ debug ROM size; build times recorded | WORKFLOW.md, CLAUDE.md | ✔ verified-hw 2026-09-23 · 642135c, fad76ab |
| P1.2 | Unified stats | scattered counters, D4 → §5.4 | `src/debug/stats.c/h`, `texture.c`, `mesh.c`, `particle.c`, `collision.c`, `physics.c`, `shadow.c`, `billboard.c`, `demo_scene.c` | P1.1 | Stats page matches hand counts for the demo (≈345 tris steady / 455 in a burst; 8 uploads with all faces visible, fewer when culled) | uploads/frame in the demo drop from ≥ 8 to ≤ 6 after cull-before-upload; overhead ≤ 0.05 ms | PROFILING.md (glossary) | ✔ verified-hw 2026-09-23 · 907754d |
| P1.3 | CPU profiler + overlay bars | no per-phase timing → §5.3, §5.8 | `src/debug/profiler.c/h`, `main.c`, `mesh.c`, `demo_scene.c`, `floor.c`, `shadow.c`, `particle.c`, `atmosphere.c` | P1.1 | slot sum ≈ `PROF_FRAME` ± 5 % on ares and hw; CSV parses; peaks reset | profiler overhead ≤ 0.1 ms/frame (toggle `ENGINE_PROFILE`); first per-phase table for the demo | PROFILING.md | ✔ verified-hw 2026-09-23 · 1937119 |
| P1.4 | Memory stats | no heap/stack visibility → §5.5 | `src/debug/memstats.c/h` | P1.1 | hw: Expansion-Pak/RDRAM value recorded for the Analogue 3D; Reset ×10 shows D1 | heap_used baseline row; stack watermark < 50 % of the painted region | HARDWARE.md, BENCHMARKS.md | ✔ verified-hw 2026-09-23 · df93381 |
| P1.5 | Frame-time history / histogram / 1 % lows | only a 2 Hz smoothed FPS → §5.6 | `src/debug/frametime.c/h`, `tests/host/test_frametime.c` | P1.3 | host: p99 / 1 %-low math; ares: single-bucket histogram at a steady 60 | — | PROFILING.md | ✔ verified-hw 2026-09-23 · df93381 |
| P1.6 | Debug overlay pages | ad-hoc HUD → §5.7 | `src/debug/overlay.c/h`, `main.c` (menu items), `demo_scene.c` (menu enums) | P1.2–1.5 | each page renders with zero validator messages; Off costs 0 | `PROF_OVERLAY` measured and reported; ≤ 5 ms with a page on (target revised 2026-09-23 from 0.3 ms: text costs ~15–20 µs per glyph on the A3D, see BENCHMARKS.md) | DEBUGGING.md | ✔ verified-hw 2026-09-23 · f4628e0 |
| P1.7 | RSP/RDP profiling | Revised scope: libdragon's RSPQ_PROFILE cannot be built at the pinned commit: with it on, the core `rsp_rdpq` microcode (and the H.264 one) overflows the 4 KB IMEM by 96 bytes. Shipped instead: RDP load from the hardware counters (DP_CLOCK/BUSY/PIPE/TMEM), always available, on the RSP page and in `RDP` CSV rows. The opt-in patch + `tools/rspq_profile.ps1` are kept for a retry after the P4.0 libdragon upgrade. | `src/debug/profiler.c`, `src/debug/overlay.c`, `tools/patches/rspq_profile.patch`, `tools/rspq_profile.ps1` | P1.3 | hw: RSP page shows RDP busy/pipe/TMEM; counter rate measured (93.75 MHz on the A3D) | first RDP numbers: 5.4 ms busy (32 %) in the quiet demo; engine is CPU-bound | PROFILING.md | ✔ verified-hw 2026-09-23 (revised scope) · 7a477d0 |
| P1.8 | Benchmark scene + CSV + compare tool | no stress harness → §5.10 | `src/scenes/benchmark_scene.c/h`, `tools/bench_compare.py`, `docs/BENCHMARKS.md`, `docs/benchmarks/*.csv` | P1.2–1.6 | full run on ares + hw (~10 min); CSV parses; compare tool exit codes | 26-step baseline on the A3D (debug build; release prints no CSV); object ceiling ≈ 24–28 pillars at 60 FPS | BENCHMARKS.md | ✔ verified-hw 2026-09-23 · f1d5d28 |
| P1.9 | CI + host tests | no CI / tests → §5.11 | `.github/workflows/build.yml`, `tests/host/**`, `tools/rom_budget.py` | P1.1 | CI green on push; a deliberately broken collision test fails CI | CI wall time ≤ 10 min | WORKFLOW.md | ✔ verified 2026-09-23 · 6c855e4 — GitHub Actions green (first run on b6fba17) |
| P1.10 | Crash diagnostics & RDP validation workflow | undocumented → §5.12 | `src/debug/rdp_debug.c/h`, `tools/rdp_log_to_hex.py`, `docs/DEBUGGING.md`, `docs/HARDWARE.md` | P1.1 | hw: a forced `assertf` shows the inspector and the USB backtrace; a one-frame capture validates clean offline | — | DEBUGGING.md, HARDWARE.md | ✔ verified-hw 2026-09-23 · b6fba17 |

Definition of Done for every row: builds in both variants, CI green, BENCHMARKS row committed, docs updated, status `verified-hw`.

### 5.14 Phase 1 status (2026-09-23) — complete

All ten features are built, exercised on the Analogue 3D through the in-engine Debug tab, and documented (DEBUGGING.md, PROFILING.md, BENCHMARKS.md, HARDWARE.md).

| ID | Status | Notes |
|---|---|---|
| P1.1 | verified-hw | debug/release variants, auto sources, header deps; validator made opt-in after it exposed D2 and D18 |
| P1.2 | verified-hw | unified stats; D4 (upload before cull) fixed |
| P1.3 | verified-hw | 21 scopes; overhead ≈ 0.1 ms |
| P1.4 | verified-hw | A3D has 8 MB (Expansion Pak); D1 leak measured at ~20 KB per reset |
| P1.5 | verified-hw | host-tested; revealed loop pacing jitter (new D19) |
| P1.6 | verified-hw | 2.3–4.2 ms per page (target revised: text ≈ 15–20 µs per glyph) |
| P1.7 | verified-hw (revised) | RDP load from hardware counters (93.75 MHz on the A3D); libdragon RSPQ_PROFILE blocked at this commit (IMEM overflow), retry in P4.0 |
| P1.8 | verified-hw | 26-step baseline committed; object ceiling ≈ 24–28 pillars at 60 FPS |
| P1.9 | verified | 67 host checks at the time (123 by Phase 2 S3); GitHub Actions green from the first push (b6fba17) |
| P1.10 | verified-hw | crash inspector + USB backtrace; two-frame RDP capture validates clean offline (0 warnings, 188 triangles) |

Findings that feed Phase 2/3: the engine is **CPU-bound** (RDP busy 32–44 %); text drawing is the largest avoidable cost (HUD 1.1 ms, menu peaks 6–9 ms with the validator, overlay pages 2–4 ms); the floor (2.8 ms) and projected shadows (+16.6 ms for 16 casters) are the next biggest; state changes (SET_OTHER_MODES / SET_COMBINE_MODE) are a third of the RDP command stream.

### 5.15 Phase 1 stage log

How Phase 1 actually ran, stage by stage. Each stage was built in both variants, uploaded to the SummerCart64, tested by hand on the Analogue 3D (and in ares where relevant), then committed.

| Stage | Commit | What shipped | What the A3D test showed | Defects |
|---|---|---|---|---|
| 1 · Build variants | 642135c | `BUILD=debug/release`, wildcard sources, `.d` includes, `engine(-debug).z64` | Debug build with the RDP validator at boot crawled (30–40 FPS reported, <10 FPS felt) and flickered | D15 fixed; D2 found (~41,000 validator warnings) |
| 1b · D2 fix + Debug tab | fad76ab | flat materials use `TRIFMT_ZBUF`; Debug tab (Overlay, Profiler, RDP Check, Dump CSV, Reset Peaks); D-Up/D-Down shortcuts | 60 FPS restored; validator on → ~50 FPS + lower-screen flicker; toggling mid-frame gave bogus errors and one RSP crash in the mixer → toggle at a frame boundary | D2 fixed; D18 found |
| 2 · Unified stats | 907754d | `EngineStats`, STATS CSV | quiet view 254 tris (floor 200), 6 uploads with 17 groups culled | D4 fixed |
| 3 · CPU profiler | 1937119 | 21 scopes, PROF CSV, HUD CPU ms | CPU 8.0 ms quiet (floor 2.8, objects 2.0, audio 1.3, HUD 1.1); profiler overhead ≈ 0.1 ms; `menu` peaks 6–9 ms with the validator | D18 trigger measured |
| 4 · Memory + frame time | df93381 | memstats, frametime ring + histogram | A3D has 8 MB (Expansion Pak); Reset ×5 leaked 101.6 KB; loop times bimodal 12.5 / 21 ms at 60 FPS | D1 measured; D19 found |
| 5 · Overlay pages | f4628e0 | Stats / Profiler / Memory / Frame / RSP pages | first layout overflowed the TV; re-laid out from the measured font (6 px advance, 13 px line); text ≈ 15–20 µs per glyph, pages 2.3–4.2 ms | D7 fixed |
| 6 · RDP load | 7a477d0 | DP hardware counters on the RSP page, RDP CSV | counters tick at 93.75 MHz; RDP busy 5.4–7.2 ms (32–44 %): **engine is CPU-bound**; libdragon RSPQ_PROFILE fails to build (IMEM +96 B) | — |
| 7 · Benchmark scene | f1d5d28 | 26-step benchmark, bench_compare.py, baseline CSV | first runs: particles invisible (stack-allocated emitter def), faint fill layers, 32-bit timer wrap — fixed; object ceiling ≈ 24–28 pillars | — |
| 8 · Tests + CI | 6c855e4 | 67 host checks, ci_build.sh, rom_budget.py, GitHub workflow | tests caught `action_analog_x()` doc/behaviour mismatch | — |
| 9 · Crash + RDP capture | b6fba17 | Crash Test, RDP Log capture, rdp_log_to_hex.py, DEBUGGING/PROFILING/HARDWARE docs | inspector + symbolized USB backtrace on hardware; capture needed two frames and full-triangle logging, then validated with 0 warnings (1,740 words, 188 triangles) | — |

---

## 6. Phase 2 — Engine hardening — status: in progress (S0–S3 verified 2026-09-23)

Close the defect register and remove the structural debt that would otherwise be copied into the Tiny3D path, measuring every change with the Phase 1 tools. Order: **measure → fix leaks and correctness → performance → engine core → structural refactors → completeness → hygiene → exit.** Each stage is one commit and must pass the stage gate in §6.4.

Scope decisions (2026-09-23): a text/menu performance stage is added (P2.12, the root of D18); D18 and D19 are investigated and fixed in the engine core (P2.5), with a deliberate-overload benchmark to reproduce D18; both structural refactors stay in (P2.7 settings module, P2.8 input unification).

**Start reference:** `docs/benchmarks/2026-09-23-baseline-debug-a3d.csv` plus the demo numbers in BENCHMARKS.md (quiet view CPU 8.0 ms, heap 913 KB, D1 ≈ 20 KB per reset).

### 6.1 Stages

| Stage | ID | Status | Work | Key files | Acceptance (measured on the A3D unless noted) |
|---|---|---|---|---|---|
| S0 | P2.0 **Measurement prep** | ✔ verified-hw 2026-09-23 | New benchmark kind `OVERLOAD` (CPU burn stepping 12 → 25 ms plus a text-heavy page) to reproduce D18 **without** the validator. New Debug item **Reset Soak** (10 scene resets in a row, MEM rows before/after, prints heap delta). Record demo-view dumps (quiet / menu / particles) as the Phase 2 start rows. | `benchmark_scene.c`, `debug_menu.c`, `main.c`, BENCHMARKS.md | Soak reproduces D1 (~20 KB/reset); OVERLOAD answers "does a late frame flicker without the validator?" |
| S1 | P2.1 **Resource lifecycle** (D1, D21, part of D11) | ✔ verified-hw 2026-09-23 | `texture_init()` frees before loading and preserves `slot_count`; `demo_cleanup` calls `texture_cleanup()`; the demo loads billboard textures through `Scene.texture_paths` (first real use of the scene texture manager); `billboard_data_count` reset in init; account for the ~8 KB the sprites don't explain. | `texture.c`, `demo_scene.c`, `scene.c` | Reset Soak ×10 → heap delta **0 B** |
| S2 | P2.2 **Renderer correctness** (D3, D5, D24, D25) | ✔ verified-hw 2026-09-23 | Per-triangle back-face cull by screen-space winding (exact, no `sqrtf`), replacing the object-centre group test; group lighting normal = average of the group's vertex normals, precomputed when the mesh is built; alpha compare set **or cleared** on every material change. Counter `tris_culled_backface`. | `mesh.c/h`, `stats.h` | Sphere and pillars shade correctly (A3D photo vs ares); RDP Log capture validates with 0 warnings; OBJECTS bench CPU within +5 % of the start reference |
| S3 | P2.3 **CPU hot paths** (D6, D14, D23) | ✔ verified-hw 2026-09-23 | Camera dirty only on change (follow mode marks dirty when its target moves); projected shadows transform each vertex once and frustum-test the caster; `Particle` stores its emitter index (removes the O(particles × emitters) scan); collision loops bounded by the highest active slot; skip the colour clear when the sky covers the screen; demo applies menu settings only when they change. | `demo_scene.c`, `shadow.c`, `particle.c`, `collision.c`, `scene.c` | SHADOWS projected CPU −40 %; PARTICLES 128 CPU −20 %; demo `update` −20 %; no other step regresses |
| S4 | P2.4 **Mesh right-sizing** (D8) | planned | `mesh_finalize()` reallocs vertices and indices to their exact size; every builder calls it (cube, mesh_defs, billboard, benchmark boxes). | `mesh.c/h`, `cube.c`, `mesh_defs.c`, `billboard.c`, `benchmark_scene.c` | heap_used after demo init −90 KB |
| S5 | P2.12 **Text & menu performance** (new; D18 root) | planned | `text_draw` sets the font style only when the colour changes; the demo HUD becomes one cached paragraph rebuilt at 4 Hz (the overlay's `text_build` pattern); the menu caches its layout and rebuilds only when the cursor, tab or a value changes. | `text.c/h`, `demo_scene.c` (HUD), `menu.c` | `hud` ≤ 0.4 ms (from 1.1); `menu` avg ≤ 1.5 ms, peak ≤ 3 ms with the validator off; menu frames stay under 16.7 ms with the validator on |
| S6 | P2.5 **Engine core + frame pacing** (D9, D18, D19) | planned | `src/engine/engine_config.h` (screen size, FB count, guard bands, max dt) replaces every hardcoded 320/240 and guard band; `src/engine/engine.c/h` owns init and the frame loop (`main.c` shrinks to setup); `dt` from `display_get_delta_time()`; 30/60 FPS through `display_set_fps_limit()` instead of the busy-wait; Z-buffer from `display_get_zbuf()`. Use OVERLOAD to settle D18 (does a late frame flicker without the validator; does the FPS limit or buffer count change it) and document the answer in HARDWARE.md. | new `src/engine/`, `main.c`, `mesh.c`, `floor.c`, `shadow.c`, `particle.c`, `atmosphere.c`, `camera.c`, `scene.c`, `benchmark_scene.c` | Frame page loop-time histogram single-moded at 60 FPS (D19 gone); 30 FPS mode holds 30 with `limiter` ≈ 0 ms of spinning; D18 root cause written up and fixed or mitigated |
| S7 | P2.6 **Scene owns physics & colliders** (D11, D20) | planned | `Scene.physics`; `SceneObject.collider_handle` actually used (`scene_object_set_collider()`, `scene_sync_colliders()`) in place of `obj_colliders[]`; shadow casting and selection driven by per-object flags rather than an index snapshot, so the ball gets a shadow. | `scene.c/h`, `demo_scene.c` | The ball casts a shadow on the A3D; host `test_scene.c` (add/remove/collider sync); Demo↔Benchmark fade still works |
| S8 | P2.7 **Settings module** (D10) | planned | `src/ui/settings.c/h` holds tab/item enums, option strings **and** their value tables side by side; `_Static_assert` on every table size; `menu_add_item` counts come from `ARRAY_LEN`; `demo_scene.c` loses the TAB_/ITEM_ macros and lookup tables (~300 lines). | `settings.c/h`, `main.c`, `demo_scene.c`, `menu.c` | Every menu item still applies and Cancel reverts (A3D walk-through); host test that every value table matches its string table |
| S9 | P2.8 **Input unification** (D12) | planned | `ACTION_CTX_MENU` with fixed menu actions; menu navigation, START, the debug shortcuts and benchmark abort read actions instead of the raw joypad; `action_chord_pressed()`; `_Static_assert(MENU_MAX_ITEMS >= ACTION_COUNT)`. | `action.c/h`, `menu.c`, `demo_scene.c`, `debug_menu.c`, `benchmark_scene.c` | Host `test_action.c` covers contexts and chords; menu, D-Up/D-Down and Start behave as before on the A3D |
| S10 | P2.9 **Particle completeness** (D13) | planned | Honour `blend_mode` (additive, or alpha via `RDPQ_BLENDER_MULTIPLY`), batched per mode; document that point spawn already works; add a smoke effect to the demo. | `particle.c/h`, `demo_scene.c` | Smoke renders translucent on the A3D; validator clean; PARTICLES bench unchanged |
| S11 | P2.10 **Physics on hardware** (D16) | planned | Ball test at 60 and 30 FPS on the A3D with the new `dt`; host physics tests at dt = 1/30 as well as 1/60. | `tests/host/test_physics.c` | Ball comes to rest within 5 s, no tunnelling, same behaviour at both rates |
| S12 | P2.11 **Hygiene + docs** (D22) | planned | Drop or use `grass_tex.png`; ignore `tests/host/build/`; new AUDIO.md, PARTICLES.md, ENGINE.md; fix MENU_SYSTEM.md (16 options, `menu_item_set_disabled`, single-tab header, `menu_add_item` signature), SCENE_SYSTEM, INPUT, PHYSICS, RENDERING, ARCHITECTURE (Billboard section); CLAUDE/README. | docs, `.gitignore`, `assets/` | Link check clean; CI green |
| S13 | **Phase 2 exit** | planned | Full benchmark (All) and demo-view dumps on the A3D compared with the start reference; BENCHMARKS "post-hardening" rows; canonical-view screenshots; statuses here and a Phase 2 stage log (same format as §5.15). | BENCHMARKS.md, ROADMAP_v2.md | §6.3 exit criteria |

### 6.2 Why this order

S0 first so every later claim can be measured, and so D18 is reproduced without the validator before anything touches frame pacing. S1–S4 are small, local fixes (leaks, correctness, hot paths, memory) whose effect shows cleanly in the benchmark. S5 removes the biggest avoidable CPU cost before S6 changes the loop, so the pacing work isn't confounded by text overruns. S6 comes before S7–S9 because those refactors all touch `main.c` / scene setup, which S6 restructures. S10–S12 finish completeness, hardware verification and docs; S13 closes the phase against the start reference.

### 6.3 Exit criteria

- Defect register: D1, D3, D5, D6, D8–D14, D16, D19–D23 fixed; D18 root-caused and fixed or mitigated (documented in HARDWARE.md).
- Reset Soak ×10 heap delta 0 B; heap after demo init ≥ 90 KB lower than 913 KB.
- Demo quiet-view CPU ≥ 15 % lower than 8.0 ms; menu-open frames never exceed 16.7 ms with the validator off.
- No benchmark step regresses by more than 5 %; SHADOWS projected ≥ 40 % faster; the OBJECTS ceiling is reported (the +25 % goal is aspirational here — the vertex cache in P3.1 is the big win).
- `demo_scene.c` holds no menu tables; one input path; CI green; docs updated.

### 6.4 Hardening test plan

**Stage gate** (every stage, before its commit):
1. `libdragon exec bash tools/ci_build.sh` passes: both ROMs, host tests, ROM/RAM budgets, hot-text layout (`tools/hot_text.py`, D25).
2. ares smoke test: boots, menu works, the change is visible where applicable.
3. A3D checklist (§11.2) for the areas touched; the user runs it on the console and reports.
4. Benchmark compare: run the affected bench kinds (or All) and `python tools/bench_compare.py <previous>.csv <new>.csv`. No regression above 5 % unless the stage intends it; the compare output goes into the commit message. Timings move with code and data layout (D25, D26): when a render change lands near the limit, compare old and new code inside one ROM (the S2 Mesh A/B, commit 44203e6) before drawing conclusions.
5. The stage-specific proof from the matrix below, recorded as a BENCHMARKS row.

**Test matrix:**

| Stage | Automated (host / CI) | Engine tool on the A3D | Visual check on the A3D |
|---|---|---|---|
| S0 | — | OVERLOAD bench; Reset Soak | flicker with the validator **off**? |
| S1 | — | Reset Soak delta 0; Memory page | textures intact after resets |
| S2 | `test_mesh.c`: winding of every built-in mesh, group planarity, sphere visible from 8 sides; `hot_text.py` | Stats `tris_culled_backface`; Objects bench + same-ROM Mesh A/B | sphere visible from every side; shading vs ares |
| S3 | collision with sparse slots; particle update | SHADOWS / PARTICLES / OBJECTS benches; Profiler page | shadows unchanged |
| S4 | mesh builder keeps data after `mesh_finalize()` | Memory page heap after init | scene unchanged |
| S5 | — | Profiler `hud` and `menu` slots; menu open with RDP Check on | text identical; no flicker |
| S6 | frame-time histogram test for single-mode pacing | Frame page histogram; 30 FPS `limiter`; OVERLOAD | no tearing/flicker at 30 and 60 FPS |
| S7 | `test_scene.c` | Stats colliders / pairs | ball shadow |
| S8 | settings table-size test | — | every menu item + Cancel |
| S9 | `test_action.c` contexts and chords | — | menu nav, D-Up/D-Down, Start |
| S10 | — | validator on | smoke translucency |
| S11 | physics at 1/30 and 1/60 | — | ball at 30 and 60 FPS |
| S13 | full CI | benchmark All + demo dumps | full demo walk-through |

**Regression guard:** each stage commits its benchmark CSV under `docs/benchmarks/` (`<date>-p2-s<N>-debug-a3d.csv`), and that file becomes the comparison point for the next stage (a moving baseline). The 2026-09-23 baseline stays the reference for the S13 exit comparison.

---

## 7. Phase 3 — Graphics features independent of Tiny3D

### 7.1 Candidate list with honest feasibility

Ratings: **High** = known technique, fits TMEM/RAM/RDP with today's data; **Medium** = feasible with a measurable cost or constraint; **Experimental** = needs a spike before committing. "CPU path" = the current `mesh_draw()` layers, which stay CPU-rendered under Tiny3D for floor, particles, billboards, shadows, HUD.

| Feature | Feasibility | Needs T3D? | When | Cost / constraint |
|---|---|---|---|---|
| Post-transform vertex cache (each mesh vertex transformed once, not per triangle corner) | High | No | P3.1 | demo does ≈1,365 `mat4_mul_vec3`/frame for ≈300–400 unique vertices → expect −40–50 % `PROF_TRANSFORM`; prerequisite for Gouraud |
| Per-vertex Gouraud lighting (`TRIFMT_ZBUF_SHADE(_TEX)`, `RDPQ_COMBINER_SHADE`/`TEX_SHADE`) | High | No | P3.2 | `MeshVertex` already stores a normal; +1 normal transform + Blinn-Phong per unique vertex (≈1 ms for 500 verts with 4 lights); smooth normals needed in `mesh_defs` |
| Sprite animation (Feature 8) | High | No | F8 | sheets: 64×32 RGBA16 = 4 KB fills TMEM → use CI4 (128×32 = 2 KB + 32 B TLUT) or `rdpq_tex_upload_sub` per frame |
| TMEM residency manager + CI4 textures | High | No | P3.3 | 32×32 CI4 = 512 B + palette: all six cube faces, both billboards and a sprite sheet fit in 4 KB at once → zero per-frame uploads in the demo; `mksprite --format CI4`, `rdpq_tex_multi_begin/end` |
| Texture atlas for billboards/particles | High | No | in P3.3 | same TMEM math; UV sub-rects |
| TTF fonts via `mkfont` (`rdpq_font_load`) | High | No | P3.6 | `text.c` already uses `rdpq_text`; +20–60 KB ROM per font size |
| VI options: `FILTERS_RESAMPLE_ANTIALIAS(_DEDITHER)`, `GAMMA_CORRECT(_DITHER)`, `rdpq_mode_dithering` | High | No | P3.5 | menu items + a measured table; AA/dedither cost RDRAM bandwidth, visible in `PROF_WAIT_DISPLAY` / RDP busy |
| Decals & Z modes (`ZMODE_DECAL`, `rdpq_mode_zoverride`) | High | No | P3.4 | replaces manual Z-bias tricks for shadows; trivial RDP cost |
| Skybox: yaw-scrolled 2-D panorama via `rdpq_tex_blit` | High | No | P3.7 | replaces 60 fill rectangles; a 256×64 CI4 panorama is 8 KB in ROM; textured 3-D dome is Medium (more tris, fog interaction) |
| Sorted transparency pass (back-to-front translucent objects + alpha particles) | High | No | P3.8 | insertion sort over ≤ 32 objects per frame; Z-write off for translucents |
| Dynamic / animated point lights + radius culling | High | No | P3.13 | animation is free; per-vertex lighting makes it look right |
| UV scroll / water | High | No | P3.9 | per-frame UV offset; a second layer needs a second tile resident (fine with CI4) |
| 2-cycle combiner effects: detail/multi-texture, fake specular map, sphere-map environment (view-space normal → UV on the CPU) | Medium | No | P3.9 (after P3.2) | two tiles resident; 2-cycle halves fill rate on those triangles — measure with FILLRATE |
| Mipmapping (`mksprite --mipmap`, `rdpq_mode_mipmap`, `tex_mipmaps`) | Medium | No | P3.10 | 32×32 RGBA16 chain = 2.7 KB; needs 2-cycle and correct LOD (W is already computed); fixes floor shimmer |
| LOD switching (distance-based mesh swap) | High | No | P3.12 | `MeshLOD { const Mesh *lod[3]; float dist[3]; }` on the object |
| Full-screen fade / colour grade (blended full-screen rectangle) | High | No | P3.11 | ≈0.5–1 ms RDP; transitions already draw such a rectangle |
| Cheap bloom / motion blur via framebuffer-as-texture (chunked `rdpq_tex_blit`, downsample, additive) | Experimental | No | P3.11 spike | ≈40 TMEM-sized blits per full-screen pass → 2–4 ms RDP; previous-frame reads need the triple-buffer index; likely 30 FPS only |
| 640×480 interlaced / 512×240; anamorphic widescreen via `vi_set_xscale` | Medium (widescreen: High) | No | option | 640×480×16-bit ×3 = 1.8 MB FB + 614 KB Z → needs the Expansion Pak (record the Analogue 3D's RDRAM size in P1.4) and halves the CPU raster budget; widescreen via VI scaling is free |
| Render-to-texture shadow maps projected in 2-cycle | Medium / Experimental | No (works on both) | after T3D | second pass of casters into a 32×32 surface; affordable once T&L is on the RSP; no stencil → no shadow volumes |
| Occlusion / portal culling | Medium | No | after T3D | only pays off with large authored scenes (Fast64 custom properties) |
| Terrain heightmap | Medium (CPU) / High (T3D) | prefer T3D | after T3D | CPU path limited to ≈16×16 visible chunks |
| Instancing | T3D-only | Yes | after T3D | matrix stack + shared vertex buffers |

### 7.2 Committed Phase 3 features

Order: P3.1 → P3.2 → P3.3 → F8 → P3.8 → P3.4 → P3.6 → P3.5 → P3.7 → P3.12 → P3.13 → P3.9 → P3.10 → P3.11.

| ID | Feature | Files | Deps | Test (ares / hw / host) | Metric & acceptance | Docs |
|---|---|---|---|---|---|---|
| P3.1 | **Post-transform vertex cache.** `mesh_draw` transforms `mesh->vertices[]` once into a static scratch array `MeshVertexOut { float sx, sy, sz, inv_w; uint8_t r, g, b, a; float s, t; uint8_t flags; }` (sized `MESH_MAX_VERTICES`), then assembles triangles from indices; near-plane/guard-band rejection becomes per-vertex flags | `mesh.c/h`, `shadow.c` (reuses the cache), `billboard.c` | P2.2, P2.5 | pixel-identical output (screenshot pair) | `PROF_TRANSFORM` −40 % or better on OBJECTS(32); tris/frame unchanged | RENDERING.md, MESH_SYSTEM.md |
| P3.2 | **Gouraud shading.** `Material.shading = SHADING_FLAT | SHADING_SMOOTH`; smooth transforms normals per vertex and calls a specular-free `lighting_calculate_fast()` per vertex, shade RGB (A = fog) lands in the cache, `TRIFMT_ZBUF_SHADE(_TEX)`; flat path unchanged; `mesh_defs` sphere and pillar sides get averaged normals; floor gets per-vertex fog | `mesh.c/h`, `lighting.c/h`, `mesh_defs.c`, `floor.c` | P3.1 | sphere shows smooth shading; validator clean; host `test_lighting.c` | `PROF_LIGHTING` ≤ 1.5 ms on OBJECTS(32) with 4 lights; demo holds 60 | RENDERING.md, ARCHITECTURE.md |
| F8 | **Sprite animation.** `src/render/anim_sprite.c/h`: `AnimDef { sheet slot, frame_w/h, frame_count, fps, loop }`, `AnimState { def, time, frame }`, `anim_update(AnimState*, dt)`, `anim_uv(const AnimState*, float uv[4][2])`; billboard materials gain an `AnimState*`; particles an optional `AnimDef*` (frame by life fraction) | `anim_sprite.c/h`, `billboard.c/h`, `particle.c/h`, `Makefile` (CI4 sheets), `assets/sheets/` | P3.3 (or `rdpq_tex_upload_sub`) | fire billboard animates at the authored rate; host `test_anim.c` (timing, loop/one-shot) | uploads/frame unchanged vs a static billboard | PARTICLES.md, ARCHITECTURE.md (Billboards) |
| P3.3 | **TMEM residency manager + CI4.** `texture_upload` tracks `{slot, tmem_addr, tile}` and skips redundant loads; `texture_pack_begin/end()` places several small textures with `rdpq_tex_multi_begin/end`; `mksprite --format CI4` for 32×32 assets; `scene_draw` sorts objects by texture slot | `texture.c/h`, `mesh.c`, `scene.c`, `Makefile`, `assets/*.png` | P2.2 | no visual change; Stats page `tex_uploads` ≈ 0 steady-state in the demo | TEXTURES bench at 8 distinct textures: ≤ 8 → target 0 uploads/frame; `PROF_SUBMIT` −10 % | TEXTURES.md |
| P3.4 | **Decals & Z modes.** Shadows use `ZMODE_DECAL` / `rdpq_mode_zoverride`; `Material.decal` flag | `shadow.c`, `mesh.c`, `floor.c` | P2.2 | hw: no Z-fighting on shadows at grazing angles | — | RENDERING.md |
| P3.5 | **VI / display options.** Settings items AA (Off/Resample/AA/AA+Dedither), Gamma (Off/On/Dither), Dither (`rdpq_mode_dithering` presets); needs a `display_close()`/`display_init()` re-init path in the engine | `engine.c`, `settings.c`, `main.c` | P2.5, P2.7 | hw: each mode stable for 60 s; screenshots | table of `PROF_WAIT_DISPLAY` / RDP busy per mode | HARDWARE.md, RENDERING.md |
| P3.6 | **TTF fonts.** `mkfont` rule `filesystem/fonts/%.font64: assets/fonts/%.ttf`; `FONT_UI` id; builtin fallback | `text.c/h`, `Makefile`, `assets/fonts/` | P1.1 | HUD readable on ares + hw; ROM delta recorded | text draw ≤ +0.2 ms | ARCHITECTURE.md (Text) |
| P3.7 | **Skybox panorama.** `sky_draw()` blits a yaw-scrolled CI4 strip; gradient mode kept as fallback | `atmosphere.c/h`, `assets/sky/`, `Makefile` | P3.3 | hw: seamless wrap; fog band still matches `bg_color` | `PROF_SKY` ≤ previous 60-rectangle cost | ARCHITECTURE.md (Atmosphere) |
| P3.8 | **Sorted transparency.** `SceneObject.translucent`; `scene_draw` = opaque pass → translucent pass back-to-front (Z-write off) → particles by blend mode | `scene.c/h`, `particle.c`, `mesh.c` | P2.6, P2.9 | overlapping translucent quads correct from all angles | sort ≤ 0.1 ms at 32 objects | RENDERING.md |
| P3.9 | **UV scroll + 2-cycle effects.** `Material.uv_scroll`, `Material.detail_slot`; water tile, detail texture, sphere-map demo object | `mesh.c/h`, `mesh_defs.c` | P3.2, P3.3 | hw: validator clean in 2-cycle; FILLRATE with 2-cycle layers | 2-cycle cost table | RENDERING.md |
| P3.10 | **Mipmapping** for the floor and large textures | `Makefile` (`--mipmap BOX`), `texture.c`, `mesh.c` (`tex_mipmaps`) | P3.3 | hw: no shimmer at distance | RDP busy delta recorded | TEXTURES.md |
| P3.11 | **Post-processing.** Fade/colour-grade rectangle (`engine_post_fx_set(color, alpha)`); bloom/motion-blur as a documented spike with a go/no-go number | `engine.c`, RENDERING.md (spike results) | P2.5, P1.7 | hw: fade clean | fade ≤ 1 ms RDP; spike: go if a pass costs ≤ 3 ms | RENDERING.md |
| P3.12 | **LOD switching** | `scene.c`, `demo_scene.c`, `mesh_defs.c` (low-poly variants) | P3.1 | pop distance tunable | OBJECTS(64) with LOD holds 60 where no-LOD does not | MESH_SYSTEM.md |
| P3.13 | **Animated point lights + radius culling** (torch flicker, moving lights) | `lighting.c/h`, `demo_scene.c` | P3.2 | hw: flicker visible | `PROF_LIGHTING` unchanged ± 5 % | ARCHITECTURE.md |

---

## 8. Phase 4 — Milestone 1: libdragon upgrade, Tiny3D, GLTF pipeline

The v1 rationale stands: the CPU transform path tops out around 20–30 objects; [Tiny3D](https://github.com/HailToDodongo/tiny3d) moves transform and lighting to the RSP, loads GLTF exported from Blender with Fast64, and brings skeletal animation. The Phase 1 tools exist precisely so that this transition is measured rather than assumed. The **parallel-path** architecture from v1 is kept: Tiny3D objects get their own `T3DObjectData` and draw callback through `SceneObject.on_draw`; the CPU pipeline (`mesh_draw()`, floor, particles, billboards, shadows, HUD) stays untouched; bridge functions sync `LightConfig` and `FogConfig` to Tiny3D.

### P4.0 libdragon upgrade + Tiny3D integration (Docker / Windows / CI) — status: planned

**Problem.** The submodule is ~460 commits behind `origin/preview`; Tiny3D requires `preview`; Tiny3D must be built inside the container and reproducibly in CI; `.libdragon/config.json` (`:latest`) and `.devcontainer` (`:preview`) disagree.

**Solution.**
1. `git -C libdragon fetch && git -C libdragon checkout <preview SHA that Tiny3D targets>`; `libdragon install`; fix API drift in project code (expected: `rdpq_font`/`rdpq_text` parameters, `display_init`/`vi.h`, joypad, `mksprite`/`audioconv64` flags, sprite accessors, `rspq_profile` struct); `libdragon make clean` (asset formats change); full benchmark run → "post-upgrade" rows = the fresh CPU baseline.
2. One-day bake-off: libdragon's own `model64`/OpenGL RSP path vs Tiny3D on the same crate model — RSP overlay time (P1.7) and CPU time — decision recorded in `docs/T3D_INTEGRATION.md`.
3. Vendor Tiny3D as `external/tiny3d` (submodule); build with `libdragon exec bash -c "cd external/tiny3d && ./build.sh"`; Makefile `include external/tiny3d/t3d.mk`; GLTF rule `filesystem/models/%.t3dm: assets/models/%.glb` using `$(T3D_GLTF_TO_3D)`; CI caches the Tiny3D build by submodule SHA.
4. Align `.libdragon/config.json` and `.devcontainer` on `ghcr.io/dragonminded/libdragon:preview`.

**Test.** Both ROM variants build in CI; the demo is unchanged on hardware; `rspq_profile` shows the Tiny3D overlay once Feature 11 lands.
**Metric.** The upgrade must not regress the demo's `PROF_FRAME` by more than 5 % (investigate before proceeding).
**Docs.** SETUP.md (Tiny3D section), WORKFLOW.md, T3D_INTEGRATION.md (new), CLAUDE.md.

### Features 11–16 (v1 text applies, with these deltas)

| Feature | v1 scope | Delta in v2 |
|---|---|---|
| F11 Tiny3D bootstrap | RSP proof of life: one quad drawn by Tiny3D beside the CPU scene | `PROF_T3D_SUBMIT` slot; RSP page shows the Tiny3D overlay; metric = RSP + CPU time for a 1-quad frame |
| F12 GLTF model loading | first Blender model on screen; `docs/BLENDER_SETUP.md` | metric = DFS load time and heap delta per model (memstats) |
| F13 Textured materials & fog bridge | Fast64 textures; `FogConfig` → Tiny3D fog; `docs/FAST64_MATERIALS.md` | tested in all 7 atmosphere presets; validator clean |
| F14 Dual-path scene | pillar/platform/pyramid as Blender models; `t3d_demo_scene`; `docs/ASSET_PIPELINE.md` | `BENCH_OBJECTS_T3D` variant; acceptance ≥ 64 pillars at 60 FPS on hardware vs the CPU-path ceiling from Phase 1 |
| F15 Developer tooling | model viewer scene, Tiny3D debug overlay, Blender template; `docs/T3D_INTEGRATION.md` | the viewer reuses the overlay pages; Tiny3D stats feed `stats.c` |
| F16 Pipeline polish | LOD workflow, collision from model bounds; `docs/LOD_WORKFLOW.md` | collision-from-bounds gets host tests; v1's triangle budget table stays |

N64 triangle budgets (design-time LOD, from v1): environment prop 20–50, architectural 30–80, humanoid character 150–300, boss/hero 300–500, vehicle/large prop 100–200.

---

## 9. Phase 5 — Milestone 2: animation & character system

v1 Features 17–21 as written, each with a test plan (ares + hardware checklist) and one metric:

| Feature | Metric |
|---|---|
| F17 Skeletal animation (rigged character, idle + walk, `AnimController`) | CPU skeleton update ≤ 1 ms per character; bones vs RSP time recorded |
| F18 Animation blending & state queries (crossfade, speed, double-buffered skeletons) | blend cost per character recorded; no RSP DMA tearing on hardware |
| F19 Character controller (stick movement, facing lerp, physics grounding) | input → animation state latency ≤ 1 frame |
| F20 Animation state machine (named states, transition rules, keyframe events) | host tests for transitions and events |
| F21 Entity/actor pattern (Transform / Renderable / Collidable / Animated / Controller / Combat / GameData capability pointers; 1 player + 1 enemy attack/dodge loop) | per-entity update cost recorded; 60 FPS with 2 animated characters + particles + shadows |
| Grid-based movement (FFT track: tile grid, snapping, A*, tile-to-tile lerp) | A* on a 32×32 grid ≤ 1 ms (host test) |

## 10. Phase 6 — Milestone 3: game framework

v1 text applies: game state machine (Title → World Map → Battle Setup → Battle → Victory/Defeat → Save), souls-like combat test scene (1v1 arena, light/heavy attacks, dodge with i-frames and stamina, hit reactions, health/stamina HUD), turn-based battle system (initiative queue, Move/Attack/Ability/Item/Wait, tile targeting, damage model), save/load (SRAM or Controller Pak; set `N64_ROM_SAVETYPE` so `sc64deployer upload` configures the save), AI foundation (decision trees, A*, threat assessment). Metrics: save/load round-trip on ares and the SummerCart64; the combat loop at 60 FPS with two animated characters, particles and shadows; battle turn resolution ≤ 2 ms.

---

## 11. Testing & verification strategy

### 11.1 Definitions

- **built** — compiles in both variants; CI green.
- **verified-host** — host unit tests pass (pure-C modules).
- **verified-ares** — runs in ares (Homebrew Mode) with `rdpq_debug_start()` on and zero validator errors or warnings during ≥ 2 minutes of interaction covering the feature.
- **verified-hw** — the same ROM on the Analogue 3D via SummerCart64 passes the checklist below. Hardware is the source of truth; ares never substitutes for it on anything touching the RDP, VI, audio, timing or memory.

### 11.2 Hardware verification checklist

1. Boots from `sc64deployer upload` + reset; no inspector screen for ≥ 5 minutes idle and ≥ 2 minutes of interaction.
2. `sc64deployer debug` log: no `assert`, no validator messages (debug build), the expected startup lines present.
3. FPS overlay at target (60, or 30 in 30-FPS mode) in the demo's five canonical views.
4. Reset Scene ×10 → Memory page heap delta 0 B.
5. Every menu item the feature touches: Apply and Cancel both behave.
6. Visual parity vs ares screenshots (allowing VI filter differences); the pair archived under `docs/images/verification/<feature-id>/`.
7. Benchmark run captured to `docs/benchmarks/<date>-<commit>-hw.csv` (debug and release).
8. Feature-specific checks (audio underruns, save round-trip, 30-FPS timing, …).
9. SC64 firmware and Analogue 3D firmware versions recorded in the BENCHMARKS row.

### 11.3 `docs/BENCHMARKS.md` format

Header: how to run (menu item / boot define), how to capture (PowerShell `Tee-Object`, ares terminal), definitions (avg, p99, 1 % low = 1000 / mean of the slowest 1 % of a 256-frame window).

Columns: `Date | Commit | Build | Platform (ares x / A3D fw + SC64 fw) | Bench | Step | Frames | Avg ms | P99 ms | 1% low FPS | Tris/frame | Tex uploads/frame | RDP busy % (or n/a) | Heap used KB | Notes`.

Sections: Environment (build times, ROM size, ELF text/data/bss), Baseline (pre-tooling), per-phase rows (post-P1, post-P2, post-P3.x, post-upgrade, post-T3D), per-feature before/after pairs.

### 11.4 Catching regressions

- CI builds both variants, runs host tests, checks ROM/BSS budgets, and validates the schema of any committed `docs/benchmarks/*.csv`.
- Hardware cannot run in CI, so `tools/bench_compare.py <baseline>.csv <new>.csv --threshold 0.05` must pass before a feature's status becomes `verified-hw`; its output goes into the commit message.
- Any regression > 5 % average frame time, or any benchmark step that drops below 60 FPS where the baseline held 60, blocks the feature until it is explained in its BENCHMARKS notes.

---

## 12. Documentation plan

| Phase | Create | Update |
|---|---|---|
| 0 | `ROADMAP_v2.md`, `.gitattributes` | `SETUP.md` (cross-platform), `WORKFLOW.md`, `CLAUDE.md`, `README.md`, `.vscode/tasks.json`, `.gitignore`, `ROADMAP.md` (superseded banner) ✔ |
| 1 | `DEBUGGING.md` (log channels, inspector/backtrace, assertf, RDP validator, one-frame capture + rdpvalidate, TMEM dump), `PROFILING.md` (slots, stats glossary, memory page, frame-time definitions, instrumented-libdragon procedure), `BENCHMARKS.md`, `HARDWARE.md` (Analogue 3D specifics: FPGA, measured RDRAM, SC64 upload/debug/save types, USB driver; consolidated RDP rules; ares-vs-hardware differences) | WORKFLOW.md (variants, CI, benchmark capture), README.md, CLAUDE.md |
| 2 | `AUDIO.md` (mixer init 22050 Hz / 4 DMA buffers, channel policy, `snd_*` API, sound bank, placeholder generator), `PARTICLES.md` (pool, emitters, defs, renderer, blend modes, CPU fog, animation hooks), `ENGINE.md` (engine core, config header, frame loop, settings module) | `MENU_SYSTEM.md` (MAX_OPTIONS 16, `menu_item_set_disabled`, single-tab header, `menu_add_item` signature, settings module), `SCENE_SYSTEM.md` (physics/collider ownership, transitions), `INPUT.md` (menu context, chords), `PHYSICS.md` (hardware verification), `RENDERING.md` (source table, culling), `ARCHITECTURE.md` (Billboard section, dependency graph, memory budget from memstats) |
| 3 | — | `RENDERING.md` (vertex cache, Gouraud, transparency, 2-cycle, post-fx spike), `TEXTURES.md` (CI4, residency, mipmaps), `MESH_SYSTEM.md` (shading flag, LOD), `ARCHITECTURE.md` (fonts, skybox), `HARDWARE.md` (VI modes table) |
| 4 | `T3D_INTEGRATION.md`, `BLENDER_SETUP.md`, `FAST64_MATERIALS.md`, `ASSET_PIPELINE.md`, `LOD_WORKFLOW.md` | `SETUP.md` (Tiny3D in the container), `WORKFLOW.md`, `BENCHMARKS.md` |
| 5–6 | `ANIMATION.md`, `ENTITY_SYSTEM.md`, `GAME_FRAMEWORK.md` | — |

---

## 13. Reference

### Hardware budget (corrected)

| Resource | Total | Used / measured | Notes |
|---|---|---|---|
| RDRAM | 4 MB (8 MB with Expansion Pak; the Analogue 3D's value is recorded in P1.4) | framebuffers 3 × 153,600 B + Z 153,600 B = 614,400 B; ELF data + bss 116,760 B; heap measured in P1.4 | design for 4 MB regardless |
| TMEM | 4 KB | 2 KB per 32×32 RGBA16; the demo re-uploads up to 8 textures per frame | CI4 (P3.3): 512 B each, all resident |
| CPU per frame | 16.67 ms @ 60 / 33.3 ms @ 30 | per-phase table from P1.3 replaces v1's "~3–5 ms" estimate | |
| RSP per frame | 16.67 ms | the audio mixer already runs on the RSP (v1's "future" wording was wrong); T&L after Phase 4 | measured via P1.7 |
| RDP per frame | 16.67 ms | unknown until P1.7 | full-screen passes ≈ 0.5–1 ms each |
| ROM | 64 MB (SC64) | 344,064 B (resolves v1's 337 vs 327 KB) | |

### External tools

| Tool | Purpose | When |
|---|---|---|
| Docker Desktop + [libdragon CLI](https://github.com/anacierdem/libdragon-docker) | build container (`ghcr.io/dragonminded/libdragon`) | now |
| [ares](https://ares-emu.net/) (Homebrew Mode) | emulator with ISViewer output, tracer, memory viewer | now |
| [sc64deployer](https://github.com/Polprzewodnikowy/SummerCart64) + FTDI VCP driver | upload, USB debug log, save types | now |
| [Tiny3D](https://github.com/HailToDodongo/tiny3d) | RSP rendering, GLTF, skeletal animation | Phase 4 |
| [Fast64](https://github.com/Fast-64/fast64) + [Blender](https://www.blender.org/) | N64-oriented modelling, materials, export | Phase 4 |
| libdragon `model64`/`mkmodel` | alternative RSP model path (bake-off in P4.0) | Phase 4 |
| [MilkyTracker](https://milkytracker.org/), [Audacity](https://www.audacityteam.org/) | XM music, WAV editing | audio content |

### Documentation index

Existing: ARCHITECTURE, CAMERA, COLLISION, INPUT, MENU_SYSTEM, MESH_SYSTEM, PHYSICS, RENDERING, ROADMAP (v1), ROADMAP_v2, SCENE_SYSTEM, SETUP, TEXTURES, WORKFLOW. Planned (phase): DEBUGGING, PROFILING, BENCHMARKS, HARDWARE (1); AUDIO, PARTICLES, ENGINE (2); T3D_INTEGRATION, BLENDER_SETUP, FAST64_MATERIALS, ASSET_PIPELINE, LOD_WORKFLOW (4); ANIMATION, ENTITY_SYSTEM, GAME_FRAMEWORK (5–6).

---

## Appendix A — Feature template

```
### <ID> <Name> — status: planned | built | verified-host | verified-ares | verified-hw
**Problem** — what is wrong or missing today, with file:line where applicable.
**Solution** — the design, detailed enough to implement without re-deciding.
**Files** — new/modified paths.
**Dependencies** — feature IDs that must land first; libdragon APIs used.
**Test plan**
  - ares: what to run, what to look for, expected log lines.
  - hardware: applicable items from §11.2 plus feature-specific checks.
  - host: unit tests to add under tests/host (pure-C modules only).
**Benchmark metric & acceptance** — the BENCHMARKS.md row(s) this feature produces, the metric, the threshold.
**Docs** — docs to create or update.
**Definition of Done** — builds in both variants; CI green; benchmark row committed; docs updated; status reached.
```

## Appendix B — Benchmark CSV schema

> Planning sketch. The rows the engine actually prints are documented in [PROFILING.md](PROFILING.md) (PROF, STATS, MEM, FRAME, RDP, RSP, SOAK, SWEEP, RDPLOG) and [BENCHMARKS.md](BENCHMARKS.md) (BENCH, BENCH_PROF, BENCH_LAYOUT).

```
PROF,<frame_index>,<slot_0_us>,...,<slot_N_us>           one row per profiler dump
STATS,<objects_drawn>,<tris_submitted>,<tex_uploads>,... one row per stats dump (header row emitted first)
MEM,<heap_used>,<heap_free>,<largest_free>,<stack_watermark>
BENCH,<kind>,<step>,<param>,<frames>,<avg_ms>,<p99_ms>,<low1_fps>,<tris>,<uploads>,<heap_used>,<rdp_busy_pct|-1>
BENCH,END
```

All rows go through `debugf()`, so the same capture works over `sc64deployer debug` (hardware) and the ares terminal (emulator).
