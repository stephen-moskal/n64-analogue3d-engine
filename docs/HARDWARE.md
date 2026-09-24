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
| Late frames | flicker in the lower screen area when frames overrun with the Start menu on screen (debug build + validator + menu); overruns without the menu do not flicker (Overload benchmark, Phase 2 S0); ares shows only an FPS drop | defect D18 |

| After a reset | for **~7.5 s** the same work costs ~35 % more CPU, ~47 % more RDP time and ~25 % more RSP (audio mixing) time, then drops to normal within one second. Every reset, demo or benchmark boot; ares shows nothing | `BOOT` rows, boot-to-benchmark holding a constant scene (D32, S6.2) |

Design for 4 MB anyway (a real N64 without Expansion Pak); the extra 4 MB is headroom for debug builds and tooling. Take no timing measurement in the first 10 s after a reset; boot-to-benchmark ROMs wait 15 s before their first step.

## Analogue 3D vs ares

| Aspect | ares | Analogue 3D |
|---|---|---|
| RDP rule violations (fill-mode triangles, format/combiner mismatch) | tolerated | hang, RSP timeout or garbage |
| Late frames | FPS drops | FPS drops; flicker only with the menu on screen (D18) |
| Timing | close, not exact | real |
| Log | ISViewer (Homebrew Mode) | USB via sc64deployer |
| Crash inspector / backtrace | yes | yes (also over USB) |
| RDP cycle counters | may be unimplemented | work, at 93.75 MHz |
| First ~7.5 s after reset | normal speed | CPU, RSP and RDP ~35–47 % slower (D32) |

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
- **Hot text:** the per-triangle render path is linked contiguously at the start of `.text`. The Makefile builds `build/<variant>/engine.ld` from libdragon's `n64.ld` with `src/engine/hot_text.ld` inserted after the boot code; engine functions opt in with `ENGINE_HOT` (`src/engine/hot.h`, a no-op in host tests), and libdragon's triangle path (`rdpq_triangle`, `rdpq_triangle_rsp`, the `floorf`/`floor` it calls) and the per-group rdpq helpers are placed by section name.
- **Layout rules:** `hot_text.ld` selects sections by object file (`*render/mesh.o`) or function name, so renaming or splitting a render `.c` file means updating it (the particle renderer's move to `particle_draw.c` did). The block is larger than the 16 KB I-cache, so its tail wraps onto its head: the head holds the floor, shadow and particle loops, the tail `mesh_draw`, which never runs at the same time. Anything else marked `ENGINE_HOT` goes last.
- **Texture uploads stay out:** `texture_upload()` and libdragon's sprite upload (~7 KB) would push the mesh phase past 16 KB and into `rdpq_triangle_rsp`'s lines, so they are left outside the block and `mesh_draw()` skips uploading a slot that is already in TMEM ([TEXTURES.md](TEXTURES.md)).
- **Check:** `tools/hot_text.py <elf>` (part of `ci_build.sh`) follows each render phase (mesh, floor, shadow, particle) from its root functions through their callees inside the block, fails when two functions of a phase need different lines at the same cache index or when a phase function lies outside the block, and lists callees outside it (`COLD_OK` allows `texture_upload` and rare-path functions such as asserts; calls through function pointers are declared in `INDIRECT`). New per-triangle code: mark it `ENGINE_HOT` and place its object file in `hot_text.ld` if the order matters; a new drawing loop also gets a phase entry in `PHASES`. Step-by-step: [EXTENDING.md](EXTENDING.md).
- **libdragon compiles with `-ftrivial-auto-var-init=pattern`:** every uninitialized local array is filled with 0xFE whenever it comes into scope, which in a triangle loop means a `memset` per triangle. Scratch arrays that are fully written before they are read take `ENGINE_NOINIT`.
- **Pointer aliasing and `restrict` (checked 2026-09-24):** the hot helpers already compile without reloads. `mat4_mul_vec3` loads its vector and matrix once and stores its four results at the end, because strict aliasing (on at `-O2`) keeps `vec4_t`, `mat4_t` and `vec3_t` apart. The inlined float-array helpers (`model_point`, `model_normal`, `shadow_project`, `light_material`, `mesh_normal_matrix`) write locals the compiler can see are separate. Adding `restrict` to all of them left every instruction of `mesh_draw`, `floor_draw`, the shadow and particle loops and `lighting_calculate` unchanged, so the engine doesn't use it. `vec3.h` never writes through its pointers, so it can't gain either. Where it would matter: a new out-of-line loop that writes one float array while reading another (the P3.1 vertex-cache transform, for example). Use `restrict` there, or copy the matrix into locals before the loop, and check the result with `tools/disasm.sh` (count the `lwc1` loads).
- **Reading FP code:** every `mul.s` is followed by a `nop`: that is `-mfix4300`, the VR4300 multiply-errata workaround every file is built with. A vertex transform (`mat4_mul_vec3`) is 19 loads, 12 multiplies (each with its `nop`), 12 adds and 4 stores.
- **Data cache (D26):** mesh data that shares D-cache sets with the render stack costs 4–8.5 % CPU on object-heavy frames. The stack sits just below the top of RDRAM, so its hot frames (`mesh_draw` 688 B, `rdpq_triangle`) land on sets ≈0x1880–0x1CF0; a mesh's geometry block or its `Mesh` struct (face groups, read per group) on those sets is slower. Bench = Layout measures it with copies at four cache colours, and `BENCH_LAYOUT` rows log every address. The planned fix is the vertex cache (P3.1), whose triangle loop reads one static buffer at a known colour instead of mesh data.
- **libdragon's static state slides too (D34):** every rdpq command reads and writes libdragon's queue pointer (`rspq_cur_pointer`, `rspq_cur_sentinel`), a static variable placed after all code. 448 bytes of unrelated code moved it from set 0x1750 to 0x1920, onto the render stack's sets, and cost `mesh_tris` 14 % (about 117 cycles per triangle). This is why builds that differ only in unrelated code can differ by ~10 % on mesh-heavy benchmark steps. Planned fix (S6.3): pin hot static data to a fixed D-cache colour in the link script, as `hot_text.ld` does for code.

## RSP / microcode limits

- RSP IMEM and DMEM are 4 KB each. With libdragon's own RSP profiler (`RSPQ_PROFILE=1`) the audio mixer's microcode overflows DMEM by 8 bytes at the current pin (at the original pin `rsp_rdpq` overflowed IMEM by 96), so it cannot be used (PROFILING.md).
- **D28, RSP crash in `rspq_highpri_sync`** ("wait loop timed out", status 0x3403): a libdragon race between the audio mixer's high-priority work and a low-priority buffer switch, fixed upstream in `7c57c409d` and included since the S4b upgrade (`39d0d6096`).
- **VR4300 multiply errata:** the `:preview` toolchain builds with `-mfix4300` automatically (GCC specs), so back-to-back multiplies are scheduled around the hardware bug.
- The audio mixer runs on the RSP (`rspq_highpri_sync` in `mixer.c`): anything that leaves the RSP halted shows up as an RSP crash in the mixer.
- Each mixed audio buffer (50 per second at this libdragon version) is a high-priority RSP job the CPU waits for, so where in the frame the mixing runs matters. With music, mixing right after `display_get()` costs 0.48 ms per frame; right after `rdpq_detach_show()` it costs 2.07 ms, because the RSP is still busy with the frame just queued (D29, [AUDIO.md](AUDIO.md)).
