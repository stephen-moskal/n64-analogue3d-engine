        /* ---- Engine hot text (src/engine/hot_text.ld, ROADMAP_v2 D25) ------
         * Inserted into libdragon's n64.ld after the boot code by the Makefile.
         * The per-triangle render path is linked contiguously so that code that
         * runs interleaved never shares a line of the 16 KB direct-mapped
         * I-cache. Sections are selected by object file or function name:
         * renaming or splitting a render .c file means updating this list.
         *
         * Order matters because the block is over 16 KB: its last ~8 KB wrap
         * onto its first. The tail is lighting, fog, then mesh_draw and the
         * per-object loops around it; the head is the shadow code first (it
         * uses neither lighting nor fog), then particles and the floor, which
         * never run together with mesh_draw. What every phase shares
         * (triangle, mode and buffer-switch paths, camera math) sits in the
         * middle, which nothing wraps onto.
         * tools/hot_text.py verifies every render phase after each build.
         *
         * Texture uploads (texture_upload -> libdragon's sprite upload, ~7 KB)
         * are deliberately NOT here: with them the mesh phase needs ~17 KB and
         * would collide with rdpq_triangle_rsp. mesh_draw minimises uploads
         * instead (a texture already in TMEM is not uploaded again). */
        . = ALIGN(32);
        __engine_hot_start = .;
        /* per-object loops around the shadow code (ENGINE_HOT_HEAD, D35),
         * then the shadow code itself: the tail's lighting and fog wrap onto
         * these, and the shadow phase uses neither */
        *(.text.engine_hot_head)
        *render/shadow.o(.text.engine_hot)
        *render/particle_draw.o(.text.engine_hot)
        *render/floor.o(.text.engine_hot)
        /* libdragon triangle path: every rdpq_triangle() runs through these */
        *(.text.rdpq_triangle_rsp)
        *(.text.rdpq_triangle)
        *libm.a:libm_a-sf_floor.o(.text .text.*)
        *libm.a:libm_a-s_floor.o(.text .text.*)
        /* libdragon per group / per draw: prim colour, fog colour, mode changes, autosync */
        *(.text.__rdpq_write8_syncchange)
        *(.text.__rdpq_write8_syncchangeuse)
        *(.text.__rdpq_fixup_write8_syncchange)
        *(.text.__rdpq_write8)
        *(.text.__rdpq_autosync_change)
        *(.text.__rdpq_fixup_mode)
        *(.text.__rdpq_fixup_mode3)
        *(.text.__rdpq_fixup_mode4)
        *(.text.__rdpq_reset_render_mode)
        *(.text.rdpq_set_mode_standard)
        /* libdragon command-buffer switch: the buffers are 2 KB, so every
         * drawing loop runs it every ~20-30 triangles, and each switch clears
         * the next buffer with memset. Unpinned, it landed on one phase or
         * another with each build: 40 lines of the shadow code in S6.3, of
         * the particle code in S7 (+3-4 %, D35) */
        *(.text.rspq_next_buffer)
        *(.text.__rspq_deferred_poll)
        *(.text.rspq_flush_internal)
        *libc.a:libc_a-memset.o(.text .text.*)
        *render/camera.o(.text.engine_hot)
        *render/lighting.o(.text.engine_hot)
        *render/atmosphere.o(.text.engine_hot)
        *render/mesh.o(.text.engine_hot)
        /* anything else marked ENGINE_HOT, last: the per-object loops around
         * mesh_draw (ENGINE_HOT_LOOP) wrap onto the head's shadow code */
        *(.text.engine_hot)
        __engine_hot_end = .;
        /* make LAYOUT_PAD=N: N bytes here move all other code (layout_pad.c) */
        KEEP(*(.text.engine_layout_pad))
        /* ---- end of engine hot text --------------------------------------- */
