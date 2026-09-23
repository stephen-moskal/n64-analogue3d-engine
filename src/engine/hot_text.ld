        /* ---- Engine hot text (src/engine/hot_text.ld, ROADMAP_v2 D25) ------
         * Inserted into libdragon's n64.ld after the boot code by the Makefile.
         * The per-triangle render path is linked contiguously so that code that
         * runs interleaved never shares a line of the 16 KB direct-mapped
         * I-cache. Sections are selected by object file or function name:
         * renaming or splitting a render .c file means updating this list.
         *
         * Order matters because the block is over 16 KB: its tail wraps onto
         * its head. The head holds the floor, shadow and particle loops; the
         * tail holds mesh_draw, which never runs together with them.
         * tools/hot_text.py verifies every render phase after each build.
         *
         * Texture uploads (texture_upload -> libdragon's sprite upload, ~7 KB)
         * are deliberately NOT here: with them the mesh phase needs ~17 KB and
         * would collide with rdpq_triangle_rsp. mesh_draw minimises uploads
         * instead (a texture already in TMEM is not uploaded again). */
        . = ALIGN(32);
        __engine_hot_start = .;
        *render/floor.o(.text.engine_hot)
        *render/shadow.o(.text.engine_hot)
        *render/particle_draw.o(.text.engine_hot)
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
        *render/camera.o(.text.engine_hot)
        *render/lighting.o(.text.engine_hot)
        *render/atmosphere.o(.text.engine_hot)
        *render/mesh.o(.text.engine_hot)
        /* anything else marked ENGINE_HOT, last */
        *(.text.engine_hot)
        __engine_hot_end = .;
        /* ---- end of engine hot text --------------------------------------- */
