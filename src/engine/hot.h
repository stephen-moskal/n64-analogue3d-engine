#ifndef ENGINE_HOT_H
#define ENGINE_HOT_H

/*
 * Code placement for the per-triangle render path (ROADMAP_v2 D25).
 *
 * The VR4300 instruction cache is 16 KB, direct-mapped, 32-byte lines: two
 * routines whose addresses are equal modulo 16 KB share cache lines and evict
 * each other. When a triangle loop collides with libdragon's
 * rdpq_triangle_rsp, every triangle pays ~1,100 extra cycles (measured on the
 * A3D: +38 % CPU in the Objects benchmark), and which functions collide
 * changes whenever any code before them changes size.
 *
 * ENGINE_HOT puts a function in the hot-text block that src/engine/hot_text.ld
 * links contiguously at the start of .text, next to the libdragon triangle
 * path. Mark the functions a per-triangle loop runs through (the loop itself
 * and what it calls every triangle or group). tools/hot_text.py checks after
 * each build that no two functions of one render phase share a cache line.
 *
 * ENGINE_NOINIT opts a local scratch array out of libdragon's
 * -ftrivial-auto-var-init=pattern, which otherwise memsets it on every loop
 * iteration. Only for arrays fully written before they are read.
 *
 * Per-object loops. The loop that calls mesh_draw or shadow_draw_* for each
 * object runs between every two calls, so unpinned it can land on the
 * phase's lines too (D35: the benchmark's object loop took 36 of the mesh
 * phase's lines, +22 us per object). ENGINE_HOT_LOOP pins such a loop at the
 * end of the block, with mesh_draw; ENGINE_HOT_HEAD at the start, for loops
 * around the shadow and floor code (the block is longer than the cache, so
 * its tail wraps onto its head: tail code shares lines only with head code).
 * Both keep the function out of line and unrenamed (noipa), so it stays in
 * its section and tools/hot_text.py finds it by name.
 */

/*
 * Mesh geometry colours (S9.1, D26). The D-cache is 8 KB, direct-mapped,
 * 16-byte lines: data whose addresses are equal modulo 8 KB (the same
 * "colour") share lines. The render path's static data is pinned
 * (hot_data.ld) and the render stack's colours are fixed, but mesh vertices
 * and indices, read for every triangle, came from the heap wherever it
 * happened to be: S9's static data growth moved the pillar's onto the mesh
 * phase's stack lines and the pinned rdpq state (+13 % per triangle).
 * mesh_finalize() places each geometry block at a chosen colour instead:
 * packed inside [LO, HI), the window no drawing phase that reads meshes uses
 * (above the pinned render data, below the projected shadows' scratch); a
 * block too big for it starts at LO and may run to MAX, the render stack.
 * tools/hot_data.py checks the window against the pinned groups and the
 * computed stack ranges.
 */
#define ENGINE_DCACHE_BYTES         8192
#define ENGINE_GEOMETRY_COLOUR_LO   0x0520
#define ENGINE_GEOMETRY_COLOUR_HI   0x0F20
#define ENGINE_GEOMETRY_COLOUR_MAX  0x1700

#if defined(N64)
  #define ENGINE_HOT       __attribute__((section(".text.engine_hot")))
  #define ENGINE_HOT_LOOP  __attribute__((section(".text.engine_hot"), noipa))
  #define ENGINE_HOT_HEAD  __attribute__((section(".text.engine_hot_head"), noipa))
  #define ENGINE_NOINIT    __attribute__((uninitialized))
#else
  // Host unit tests (and Mach-O, which rejects ELF section names)
  #define ENGINE_HOT
  #define ENGINE_HOT_LOOP
  #define ENGINE_HOT_HEAD
  #define ENGINE_NOINIT
#endif

#endif
