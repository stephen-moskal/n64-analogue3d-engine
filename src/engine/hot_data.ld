/* ---- Engine hot data (src/engine/hot_data.ld, ROADMAP_v2 D34) ----------
 * The VR4300 data cache is 8 KB, direct-mapped, 16-byte lines: addresses equal
 * modulo 8 KB ("the same colour") share a line. The stack sits at the top of
 * RDRAM, so the colours its render frames use are fixed (about 0x1700-0x1DFF,
 * tools/hot_data.py computes them); static data follows the code, so without
 * this file its colours move whenever code changes size, and a variable a
 * drawing loop touches can land on the loop's own stack lines (D34: libdragon's
 * rspq_cur_pointer on mesh_draw's frame cost the triangle loop 14 %).
 *
 * src/engine/engine_ld.awk inserts each part below into libdragon's n64.ld
 * (build/<variant>/engine.ld, with hot_text.ld). Every variable has its own
 * input section (-fdata-sections), so groups are placed by name. The "_pad"
 * parts go between output sections, where "." is an absolute address, and
 * advance it to the group's colour; the other parts go first inside the
 * output section that would otherwise take those sections (a section goes to
 * the first rule that matches it in script order). Colours:
 *   0x0000  common, first in .sbss: libdragon's command queue and rdpq state,
 *           the engine's stats and profiler counters (every drawing loop;
 *           several are $gp-relative, so this group must stay in small data)
 *   0x0200  triangle formats and the fog config (per triangle), first in .rodata
 *   0x0700  floor grid                     (own NOLOAD section)
 *   0x0F20  projected shadows: state, then screen positions; indexed by
 *           vertex, only the first vertex_count entries are hot (the first
 *           128 vertices reach 0x171F)      (own NOLOAD section)
 *   0x0300  particle pool, first in .bss (5.5 KB, to 0x18FF)
 * The floor and shadow colours follow the end of small data (~0x0700) and of
 * each other, so their pads stay small; a group that grows past the next
 * colour costs a larger pad but keeps its colour.
 * Everything between __bss_start and __bss_end, pads included, is zeroed at
 * boot. Each pad costs up to 8 KB of RAM (the one before .rodata also ROM);
 * about 10–15 KB in total. tools/hot_data.py (CI) checks the colours, the contents,
 * and that nothing pinned shares a line with the stack of a loop using it.
 * Renaming a variable listed here means updating this file and PINNED there.
 */

/* ==== rodata_pad ==== */
    . = . + ((0x0200 - (. & 0x1FFF)) & 0x1FFF);   /* engine hot data (hot_data.ld) */

/* ==== rodata ==== */
        __engine_hot_rodata_start = .;
        *(.rodata.TRIFMT_ZBUF) *(.rodata.TRIFMT_ZBUF_TEX)
        *(.rodata.TRIFMT_ZBUF_SHADE) *(.rodata.TRIFMT_ZBUF_SHADE_TEX)
        *render/atmosphere.o(.data.g_fog)
        __engine_hot_rodata_end = .;

/* ==== sbss_pad ==== */
    . = . + ((0x0000 - (. & 0x1FFF)) & 0x1FFF);   /* engine hot data (hot_data.ld) */

/* ==== sbss ==== */
        __engine_hot_bss_start = .;
        *(.sbss.rspq_cur_pointer) *(.sbss.rspq_cur_sentinel) *(.sbss.rspq_block) *(.sbss.rspq_ctx)
        *(.sbss.rdpq_tracking) *(.sbss.rdpq_config) *(.sbss.__rdpq_inited) *(.bss.rdpq_block_state)
        *debug/profiler.o(.sbss.g_prof_on .bss.g_prof_calls .bss.g_prof_ticks)
        *debug/stats.o(.bss.g_stats_cur)
        *render/particle.o(.sbss.particle_initialized .sbss.particle_pool_allocated)
        __engine_hot_bss_common_end = .;

/* ==== bss_pre ==== */
    /* engine hot data (hot_data.ld): floor grid */
    . = . + ((0x0700 - (. & 0x1FFF)) & 0x1FFF);
    .engine_hot_floor (NOLOAD) : {
        __engine_hot_bss_floor = .;
        *render/floor.o(.bss.grid_valid .bss.grid_depth .bss.grid)
    }
    /* projected shadows */
    . = . + ((0x0F20 - (. & 0x1FFF)) & 0x1FFF);
    .engine_hot_shadow (NOLOAD) : {
        __engine_hot_bss_shadow = .;
        *render/shadow.o(.bss.shadow_state .bss.shadow_scr)
    }
    /* particles: first in .bss */
    . = . + ((0x0300 - (. & 0x1FFF)) & 0x1FFF);

/* ==== bss ==== */
        __engine_hot_bss_particle = .;
        *render/particle.o(.bss.particle_pool)
        __engine_hot_bss_end = .;
