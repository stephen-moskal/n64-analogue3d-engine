        /* ---- Engine hot text (src/engine/hot_text.ld, ROADMAP_v2 D25) ------
         * Inserted into libdragon's n64.ld after the boot code by the Makefile.
         * The per-triangle render path is linked contiguously so that code that
         * runs interleaved never shares a line of the 16 KB direct-mapped
         * I-cache. Order matters once the block is over 16 KB: its tail wraps
         * onto its head, so the head holds phases that never run together with
         * the tail (floor/shadow vs mesh). tools/hot_text.py verifies each
         * render phase after every build. */
        . = ALIGN(32);
        __engine_hot_start = .;
        *render/floor.o(.text.engine_hot)
        *render/shadow.o(.text.engine_hot)
        /* libdragon triangle path: every rdpq_triangle() runs through these */
        *(.text.rdpq_triangle_rsp)
        *(.text.rdpq_triangle)
        *libm.a:libm_a-sf_floor.o(.text .text.*)
        *libm.a:libm_a-s_floor.o(.text .text.*)
        /* per group / per draw: prim colour, mode changes */
        *(.text.__rdpq_write8_syncchange)
        *(.text.__rdpq_fixup_write8_syncchange)
        *(.text.__rdpq_fixup_mode)
        *(.text.__rdpq_fixup_mode3)
        *(.text.__rdpq_fixup_mode4)
        *(.text.rdpq_set_mode_standard)
        *render/camera.o(.text.engine_hot)
        *render/lighting.o(.text.engine_hot)
        *render/atmosphere.o(.text.engine_hot)
        *render/particle.o(.text.engine_hot)
        *render/mesh.o(.text.engine_hot)
        /* anything else marked ENGINE_HOT, last (may wrap onto floor/shadow) */
        *(.text.engine_hot)
        __engine_hot_end = .;
        /* ---- end of engine hot text --------------------------------------- */
