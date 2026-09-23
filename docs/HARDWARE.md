# Hardware Notes: Analogue 3D + SummerCart64

What we have measured or learned about the target hardware, and the RDP rules the engine must follow. The Analogue 3D is an FPGA re-implementation of the N64 and our source of truth: **a feature is done only when it runs there**. ares is the day-to-day emulator and is more lenient.

## Setup in use (2026-09)

| Item | Value |
|---|---|
| Console | Analogue 3D, HDMI to a 60 Hz 4K TV |
| Flash cart | SummerCart64, firmware v2.20.2, USB to the PC (FTDI FT232H, Windows: FTDI CDM 2.12.36.20 VCP driver, `serial://COM3`) |
| Deploy | `sc64deployer upload engine-debug.z64` (0.2 s), then reset the console; the cart boots the ROM directly ("Bootloader → ROM") |
| Log | `sc64deployer debug` (USB, libdragon `debug_init_usblog()`) |
| Emulator | ares v148 with Homebrew Mode (ISViewer log) |

## Measured on the Analogue 3D

| Property | Value | How |
|---|---|---|
| RDRAM | **8 MB: reports an Expansion Pak** (`is_memory_expanded()` = true) | Memory overlay page / MEM row (P1.4) |
| Heap available to malloc | 7.86 MB | same |
| RDP counter rate (`DP_CLOCK`) | **93.75 MHz**, 1.5× the 62.5 MHz RCP clock | RDP counters vs loop time (P1.7) |
| Stack peak (demo) | ~3 KB of the 64 KB stack (4.5 KB with the validator) | stack painting (P1.4) |
| Late frames | flicker in the lower screen area when CPU work exceeds 16.7 ms (debug build + validator + menu); ares shows only an FPS drop | defect D18 |

Design for 4 MB anyway (a real N64 without Expansion Pak); the extra 4 MB is headroom for debug builds and tooling.

## Analogue 3D vs ares

| Aspect | ares | Analogue 3D |
|---|---|---|
| RDP rule violations (fill-mode triangles, format/combiner mismatch) | tolerated | hang, RSP timeout or garbage |
| Late frames | FPS drops | FPS drops and visible flicker (D18) |
| Timing | close, not exact | real |
| Log | ISViewer (Homebrew Mode) | USB via sc64deployer |
| Crash inspector / backtrace | yes | yes (also over USB) |
| RDP cycle counters | may be unimplemented | work, at 93.75 MHz |

## RDP rules (consolidated)

1. **Fill mode is only for rectangles.** Triangles must use standard (1-cycle) or 2-cycle mode:
   ```c
   // triangles
   rdpq_set_mode_standard();
   rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
   rdpq_set_prim_color(color);
   rdpq_triangle(&TRIFMT_FILL, v1, v2, v3);

   // rectangles (fast, no blending)
   rdpq_set_mode_fill(color);
   rdpq_fill_rectangle(x0, y0, x1, y1);

   // translucent rectangles: standard mode + blender (used by the debug overlay)
   rdpq_set_mode_standard();
   rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
   rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
   rdpq_set_prim_color(RGBA32(0, 0, 0, 0xB8));
   rdpq_fill_rectangle(x0, y0, x1, y1);
   ```
2. **Triangle format must match the combiner.** Flat combiners use `TRIFMT_ZBUF` / `TRIFMT_FILL`; `TRIFMT_ZBUF_TEX` only with a texture combiner; `TRIFMT_ZBUF_SHADE(_TEX)` when shade is used (the engine's fog path).
3. **Z formats need a Z-buffer attached:** `rdpq_attach(fb, &zbuf)` sends `SET_Z_IMAGE` before `SET_COLOR_IMAGE`.
4. **Mode state persists.** Reset what you set (e.g. alpha compare, defect D5) or set the full mode at the start of each batch.
5. **TMEM is 4 KB.** A 32×32 RGBA16 texture uses 2 KB; 64×64 RGBA16 does not fit (use CI4/CI8 or split).
6. **DMA buffers** must be uncached and 8-byte aligned.
7. **RSP timeout in `display_get`** usually means an RDP pipeline misconfiguration: turn on RDP Check.

## CPU caches and code placement

The VR4300 has a 16 KB instruction cache (32-byte lines) and an 8 KB data cache (16-byte lines), both **direct-mapped**: two addresses that are equal modulo the cache size share one line and evict each other.

- **Measured (Phase 2 S2):** a triangle loop that shared all 46 cache lines of libdragon's `rdpq_triangle_rsp` cost ~1,100 extra cycles per triangle, +38 % CPU for the same work (D25). Every function moves when code linked before it changes size, so unrelated edits moved benchmark CPU by ~6 % between builds.
- **Hot text:** the per-triangle render path is linked contiguously at the start of `.text`. The Makefile builds `build/<variant>/engine.ld` from libdragon's `n64.ld` with `src/engine/hot_text.ld` inserted after the boot code; engine functions opt in with `ENGINE_HOT` (`src/engine/hot.h`), and libdragon's triangle path (`rdpq_triangle`, `rdpq_triangle_rsp`, the `floorf`/`floor` it calls, the per-group rdpq helpers) is placed by section name.
- **Check:** `tools/hot_text.py <elf>` (part of `ci_build.sh`) verifies that no two functions of a render phase (mesh, floor, shadow, particle) need different lines at the same cache index, and lists phase callees left outside the block. New per-triangle code: mark it `ENGINE_HOT`; a new drawing loop also gets a phase entry in the tool.
- **libdragon compiles with `-ftrivial-auto-var-init=pattern`:** every uninitialized local array is filled with 0xFE whenever it comes into scope, which in a triangle loop means a `memset` per triangle. Scratch arrays that are fully written before they are read take `ENGINE_NOINIT`.
- **Data cache:** identical code on two copies of the same mesh data differed by up to 9 % (D26, open).

## RSP / microcode limits

- RSP IMEM and DMEM are 4 KB each. At the pinned libdragon commit, enabling libdragon's own RSP profiler (`RSPQ_PROFILE=1`) makes the core `rsp_rdpq` microcode 96 bytes too large, so it cannot be used (PROFILING.md).
- The audio mixer runs on the RSP (`rspq_highpri_sync` in `mixer.c`): anything that leaves the RSP halted shows up as an RSP crash in the mixer.
