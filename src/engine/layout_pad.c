// Layout-stability test (ROADMAP_v2 D34): `make LAYOUT_PAD=<bytes>` adds that
// many bytes of unused code at the end of .text, so every read-only, data and
// bss address after it moves, as it would when code grows. With the hot data
// pinned (src/engine/hot_data.ld), tools/hot_data.py and the benchmark must
// report the same as without the pad. Normal builds compile nothing here.

#if defined(ENGINE_LAYOUT_PAD) && ENGINE_LAYOUT_PAD > 0

#define PAD_STR2(x) #x
#define PAD_STR(x)  PAD_STR2(x)

// "keep.text.*" sections are kept by n64.ld (not garbage-collected) and linked
// after all other code
__asm__(".pushsection keep.text.layout_pad, \"ax\", @progbits\n"
        ".globl engine_layout_pad\n"
        "engine_layout_pad:\n"
        ".fill (" PAD_STR(ENGINE_LAYOUT_PAD) ") / 4, 4, 0\n"
        ".popsection\n");

#endif
