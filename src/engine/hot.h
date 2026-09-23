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
 */

#if defined(N64)
  #define ENGINE_HOT     __attribute__((section(".text.engine_hot")))
  #define ENGINE_NOINIT  __attribute__((uninitialized))
#else
  // Host unit tests (and Mach-O, which rejects ELF section names)
  #define ENGINE_HOT
  #define ENGINE_NOINIT
#endif

#endif
