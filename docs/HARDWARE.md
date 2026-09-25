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
| Tearing | only with the RDP validator on: libdragon validates inside interrupt handlers with interrupts off, the vblank interrupt runs up to ~400 half-lines late and the framebuffer flip lands mid-picture (lower-screen tear, D18). Overruns alone never tear (S0 Overload; 0 late vblanks in every run without the validator) | libdragon `VI WARNING` lines, the engine's torn-frame counter (D18, S6.4) |

| After a reset | for **~7.5 s** the same work costs ~35 % more CPU, ~47 % more RDP time and ~25 % more RSP (audio mixing) time, then drops to normal within one second. Every reset, demo or benchmark boot; ares shows nothing | `BOOT` rows, boot-to-benchmark holding a constant scene (D32, S6.2) |

Design for 4 MB anyway (a real N64 without Expansion Pak); the extra 4 MB is headroom for debug builds and tooling. Take no timing measurement in the first 10 s after a reset; boot-to-benchmark ROMs wait 15 s before their first step.

## Controllers and the VI (S9)

- **Controller reads.** libdragon's joypad module reads all four ports at every vblank. Its vblank handler queues a joybus message: an SI DMA writes the command block to PIF RAM (first SI interrupt), a second DMA reads the reply once the PIF has run the commands (second interrupt), then the callback copies it. The reply arrives **0.69 ms** after the vblank on the Analogue 3D and 1.76 ms in ares. The joybus queue holds 8 messages; libdragon also queues a port identification once a second, and every Rumble Pak on/off is a message too.
- **Knowing the read is in.** The SI status register (`0xA4800018`: DMA busy, IO busy, interrupt pending) is idle only when the queue is empty. If it is idle just before the vblank's read is queued, that read is the first message and is in after two SI interrupts. The engine's vblank handler therefore runs before the joypad module's (vblank handlers run in install order; at most 4, three used).
- **VI register writes.** `vi_write()` / `vi_show()` only update libdragon's copy of the registers; the vblank interrupt writes the hardware after every vblank handler has run. A handler that reads `*VI_ORIGIN` still sees the previous framebuffer; `vi_read(VI_ORIGIN)` returns the one about to be scanned (D36).
- **Input lag** (Bench = Latency): 3 vblanks from read to screen when the frame acts on the newest completed read and renders ahead, 2 when it waits for the vblank's read, 1 with low-latency pacing while a frame fits the budget ([ENGINE.md](ENGINE.md#input-and-latency-s9)).

## Analogue 3D vs ares

| Aspect | ares | Analogue 3D |
|---|---|---|
| RDP rule violations (fill-mode triangles, format/combiner mismatch) | tolerated | hang, RSP timeout or garbage |
| Late frames | FPS drops | FPS drops; frames repeat in whole vblanks |
| RDP validator on | late vblank interrupts, no visible tear | late vblank interrupts and a visible lower-screen tear (D18) |
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
- **Layout rules:** `hot_text.ld` selects sections by object file (`*render/mesh.o`) or function name, so renaming or splitting a render `.c` file means updating it (the particle renderer's move to `particle_draw.c` did). The block (24 KB since S7.1) is larger than the 16 KB I-cache, so its last ~8 KB wrap onto its first. The tail is lighting, fog, `mesh_draw` and the per-object loops around it (`ENGINE_HOT_LOOP`, and anything else marked `ENGINE_HOT`, last); the head is the shadow per-object loops (`ENGINE_HOT_HEAD`) and shadow code first, since the shadow phase uses neither lighting nor fog, then the particle and floor loops, which never run at the same time as `mesh_draw`. Code every phase shares (the triangle, mode and command-buffer-switch paths, the camera math) sits in the middle, where nothing wraps.
- **Texture uploads stay out:** `texture_upload()` and libdragon's sprite upload (~7 KB) would push the mesh phase past 16 KB and into `rdpq_triangle_rsp`'s lines, so they are left outside the block and `mesh_draw()` skips uploading a slot that is already in TMEM ([TEXTURES.md](TEXTURES.md)).
- **Check:** `tools/hot_text.py <elf>` (part of `ci_build.sh`) follows each render phase (mesh, floor, shadow, particle) from its root functions through their callees inside the block, fails when two functions of a phase need different lines at the same cache index or when a phase function lies outside the block, and lists callees outside it (`COLD_OK` allows `texture_upload` and rare-path functions such as asserts; calls through function pointers are declared in `INDIRECT`). A phase's roots include the per-object loops that call its drawing function (they run between every two calls), and every phase includes libdragon's command-buffer switch.
- **Per-object loops and the buffer switch (D35, S7.1):** code that runs once per object or once per few triangles moves the benchmark as much as the triangle loop, if it lands on the loop's lines. S7's layout put the benchmark's unpinned object loop on 36 of the mesh phase's lines (+22 µs per object), so the per-object loops of the benchmark, the scene (`scene_draw_objects`) and the demo are pinned, with `mat4_from_srt` (which skips `sinf`/`cosf` for an unrotated axis, so unrotated objects make no libm calls). libdragon's command buffers are 2 KB, so `rspq_next_buffer` runs every ~20–30 triangles and clears the next buffer with `memset`: unpinned, its ~40 lines fell on the shadow code in S6.3 and on the particle code in S7 (+3–4 % there). It is pinned in the middle of the block with `__rspq_deferred_poll`, `rspq_flush_internal` and libc's `memset`. New per-triangle code: mark it `ENGINE_HOT` and place its object file in `hot_text.ld` if the order matters; a new drawing loop also gets a phase entry in `PHASES`. Step-by-step: [EXTENDING.md](EXTENDING.md).
- **libdragon compiles with `-ftrivial-auto-var-init=pattern`:** every uninitialized local array is filled with 0xFE whenever it comes into scope, which in a triangle loop means a `memset` per triangle. Scratch arrays that are fully written before they are read take `ENGINE_NOINIT`.
- **Pointer aliasing and `restrict` (checked 2026-09-24):** the hot helpers already compile without reloads. `mat4_mul_vec3` loads its vector and matrix once and stores its four results at the end, because strict aliasing (on at `-O2`) keeps `vec4_t`, `mat4_t` and `vec3_t` apart. The inlined float-array helpers (`model_point`, `model_normal`, `shadow_project`, `light_material`, `mesh_normal_matrix`) write locals the compiler can see are separate. Adding `restrict` to all of them left every instruction of `mesh_draw`, `floor_draw`, the shadow and particle loops and `lighting_calculate` unchanged, so the engine doesn't use it. `vec3.h` never writes through its pointers, so it can't gain either. Where it would matter: a new out-of-line loop that writes one float array while reading another (the P3.1 vertex-cache transform, for example). Use `restrict` there, or copy the matrix into locals before the loop, and check the result with `tools/disasm.sh` (count the `lwc1` loads).
- **Reading FP code:** every `mul.s` is followed by a `nop`: that is `-mfix4300`, the VR4300 multiply-errata workaround every file is built with. A vertex transform (`mat4_mul_vec3`) is 19 loads, 12 multiplies (each with its `nop`), 12 adds and 4 stores.
- **Data cache (D26):** mesh data that shares D-cache sets with the render stack costs 4–8.5 % CPU on object-heavy frames. The stack sits just below the top of RDRAM, so its hot frames (`mesh_draw` 688 B, `rdpq_triangle`) land on sets ≈0x1880–0x1CF0; a mesh's geometry block or its `Mesh` struct (face groups, read per group) on those sets is slower. Bench = Layout measures it with copies at four cache colours, and `BENCH_LAYOUT` rows log every address. Since S7.1 `mesh_draw` copies what its triangle loop needs from the `Mesh` (index range, index and vertex pointers, material) into locals, so only the vertex and index data are still read per triangle. In one S7.1 build the pillar's vertices (heap colour 0x0490) overlapped the pinned camera copy's matrix and frustum, costing 3–4 µs per object (~2 % on mesh steps); in S9, 4 KB of new static data moved the heap and put them on the mesh stack's lines (+8–11 % on every mesh step). **Since S9.1 `mesh_finalize()` places every geometry block at a fixed colour** in a window clear of the stack and the pinned data (0x0520–0x0F1F, [MESH_SYSTEM.md](MESH_SYSTEM.md#geometry-placement-s91-d26)), checked by `hot_data.py`. What still moves is data placed by its owner: `Mesh` structs (face groups and materials, read per group) and per-object data (1–2.5 % on mesh steps between a plain and a `LAYOUT_PAD=448` build). The vertex cache (P3.1) will read one static buffer at a known colour instead of mesh data.
- **Static data slides with code (D34, fixed in S6.3):** static data follows the code, so its colours move whenever code changes size. Every rdpq command reads and writes libdragon's queue pointer (`rspq_cur_pointer`, `rspq_cur_sentinel`); 448 bytes of unrelated code moved it from set 0x1750 to 0x1920, onto the render stack's sets, and cost `mesh_tris` 14 % (about 117 cycles per triangle), every mesh-heavy benchmark step 7–12 %.
- **Hot data (S6.3):** `src/engine/hot_data.ld` pins every piece of static data a drawing loop touches to a fixed colour, away from the stack. Every variable has its own input section (`-fdata-sections`), so the groups are chosen by name, in libdragon too; `src/engine/engine_ld.awk` inserts each group's pad and rules into `n64.ld` (with `hot_text.ld`).

  | Colour | Group | Where |
  |---|---|---|
  | 0x0000 | libdragon's command queue and rdpq state, the engine's profiler and stats counters, the particle flags ($gp-relative: must stay in small data) | first in `.sbss` |
  | 0x0200 | the four `TRIFMT_*` triangle formats, the fog config, then (0x0290–0x051B) the camera and lighting every scene draws with: `scene_draw()` copies them there each frame (`scene_view_camera()` / `scene_view_light()`, D35) | first in `.rodata` |
  | 0x0700 | floor grid (`grid`, `grid_depth`, `grid_valid`) | own NOLOAD section |
  | 0x0F20 | projected-shadow scratch (`shadow_state`, `shadow_scr`: only a caster's first vertex_count entries are hot) | own NOLOAD section |
  | 0x0300 | particle pool (5.5 KB, to 0x18FF) | first in `.bss` |
  | 0x0520–0x0F1F | mesh geometry blocks (heap, placed by `mesh_finalize()`, S9.1) | `ENGINE_GEOMETRY_COLOUR_LO/HI` in `hot.h` |

  The per-element stack ranges (debug build, counting the buffer switch's frames) start at 0x1720 (mesh), 0x1880 (shadows), 0x19A0 (particles) and 0x19E0 (floor), and end at 0x1D90. The particle loop copies the camera's matrix to its stack at entry, because the camera copy shares colours with the particle pool. The pads cost 10–20 KB of RAM (19.6 KB at S7.1; `rom_budget.py` counts them): each advances to its group's colour, so a group that ends past it costs up to 8 KB more.
- **Checks:** `tools/hot_data.py <elf>` (CI) lists the static data each render phase touches (it decodes the phase's `$gp`-relative and `lui`+offset accesses), computes the phase's stack range from the frame sizes along its call chain (`CHAINS`, calibrated with `ENTRY_BYTES` from a `BENCH_LAYOUT` row), and fails if a group is off its colour, misses a symbol, shares a line with the stack of a phase that uses it, or evicts another pinned symbol of the same phase. It also notes unpinned data on the stack's lines: that is how it found the floor grid (58 lines), the shadow scratch (26–55) and the particle pool (47, the rest of D33) on the stack in every earlier build. Data a phase reaches only through pointers (the camera and lighting copies) is declared in `REACHED`, since the disassembly scan cannot attribute it.
- **Layout-stability test:** `make LAYOUT_PAD=<bytes>` puts that many unused bytes right after the hot text and after each pinned data group, so every function and static variable that is not pinned moves (colours shift by 448 bytes with `LAYOUT_PAD=448`), while `hot_text.py` and `hot_data.py` must report exactly the same. Until S7.1 the pad sat at the end of `.text`, where the groups' own pads absorbed it: it moved nothing but the heap, by a whole 8 KB, which is why S7's data growth (D35) got past it. On the A3D, compare Bench = All with and without the pad (BENCHMARKS.md, S7.1).
- **Heap-placement test (D37, Phase 3 S1):** `make HEAP_PAD=<bytes>` allocates that many bytes first thing in `engine_init()`, so every later heap block (framebuffers, libdragon's command buffers, textures, mesh geometry at its fixed colour) sits that much further into RDRAM. The Z-buffer stays at the top of RDRAM. The size is a variable in `.data`, read at run time, so builds that differ only in the pad have the same code and the same symbol addresses (`HEAP_PAD=0` against `HEAP_PAD=0x100040`: every address and section size equal, `.text` byte-identical). A step that moves between the two is moved by where the heap sits, not by code or static data. Result (S1): a 1 MB move changed no step by more than 0.5 % in any per-primitive slot, the noise of the same build booted twice, so D37 is not RDRAM placement. `BENCH_META` records `heap_pad` and `layout_pad`, and the second `BENCH_LAYOUT` row logs where the buffers landed.

## RSP / microcode limits

- RSP IMEM and DMEM are 4 KB each. With libdragon's own RSP profiler (`RSPQ_PROFILE=1`) the audio mixer's microcode overflows DMEM by 8 bytes at the current pin (at the original pin `rsp_rdpq` overflowed IMEM by 96), so it cannot be used (PROFILING.md).
- **D28, RSP crash in `rspq_highpri_sync`** ("wait loop timed out", status 0x3403): a libdragon race between the audio mixer's high-priority work and a low-priority buffer switch, fixed upstream in `7c57c409d` and included since the S4b upgrade (`39d0d6096`).
- **VR4300 multiply errata:** the `:preview` toolchain builds with `-mfix4300` automatically (GCC specs), so back-to-back multiplies are scheduled around the hardware bug.
- The audio mixer runs on the RSP (`rspq_highpri_sync` in `mixer.c`): anything that leaves the RSP halted shows up as an RSP crash in the mixer.
- Each mixed audio buffer (50 per second at this libdragon version) is a high-priority RSP job. Until Phase 3 S2 the CPU waited for each one (`mixer_poll`, now `SND_MIX_SYNC`), so where in the frame the mixing ran mattered. With music, mixing right after `display_get()` costs 0.48 ms per frame; right after `rdpq_detach_show()` it costs 2.07 ms, because the RSP is still busy with the frame just queued (D29, [AUDIO.md](AUDIO.md)).
- The RSP starts a high-priority job only between two commands (`RSPQ_Loop` checks for it), never inside a wait. rdpq makes it wait for an idle RDP in `rdpq_fence()`, which every `rdpq_clear_z()` uses (the RSP clears the Z-buffer by DMA once the RDP is done with it), and when it switches RDP command buffers while the RDP still has one queued or is finishing a `SYNC_FULL` (`RDPQ_Send`). Near and over the frame budget the audio mix waited 2–10 ms per frame (D38).
- **The SYNC_FULL hold (D38, found in Phase 3 S2):** after every `SYNC_FULL` (each frame's `rdpq_detach_show()`), libdragon's RSP code sets `RDPQ_SYNCFULL_ONGOING` and sends no further RDP command until the RDP has finished that frame, to avoid an RDP hardware bug (`RDPQCmd_SyncFull` in `rsp_rdpq.S`). The CPU writes commands into a ring of two 2 KB buffers, so it can run only ~20–30 triangles ahead of the RSP. Whenever the RDP still had the last frame's work queued, the next frame's first RSP-bound call waited for it: the audio mix (6–9.5 ms per frame at 48–64 pillars), or, with the mix no longer waited for, the triangle submission. The frame queue ([ENGINE.md](ENGINE.md)) records the frame and hands it over whole, so the CPU no longer waits: 48 pillars 40.6 → 60 FPS, 64 pillars 30.6 → 40 FPS.
