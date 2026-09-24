// Layout-stability test (ROADMAP_v2 D34, D35): `make LAYOUT_PAD=<bytes>` puts
// that many unused bytes right after the hot text and after each pinned data
// group (src/engine/hot_text.ld, hot_data.ld), so every function and every
// static variable that is not pinned moves by that much, as it would when
// unrelated code or data grows. What the render path touches is pinned, so
// tools/hot_text.py, tools/hot_data.py and the benchmark must report the same
// as without the pad. Normal builds compile nothing here.
//
// Until S7.1 the pad sat at the end of .text, where the pinned groups' own
// pads absorbed it: nothing moved but the heap, by a whole 8 KB (D35).

#if defined(ENGINE_LAYOUT_PAD) && ENGINE_LAYOUT_PAD > 0

#define PAD_STR2(x) #x
#define PAD_STR(x)  PAD_STR2(x)

__asm__(".pushsection .text.engine_layout_pad, \"ax\", @progbits\n"
        ".globl engine_layout_pad\n"
        "engine_layout_pad:\n"
        ".fill (" PAD_STR(ENGINE_LAYOUT_PAD) ") / 4, 4, 0\n"
        ".popsection\n"
        ".pushsection .rodata.engine_layout_pad, \"a\", @progbits\n"
        ".space " PAD_STR(ENGINE_LAYOUT_PAD) "\n"
        ".popsection\n"
        ".pushsection .sbss.engine_layout_pad, \"aw\", @nobits\n"
        ".space " PAD_STR(ENGINE_LAYOUT_PAD) "\n"
        ".popsection\n"
        ".pushsection .bss.engine_layout_pad, \"aw\", @nobits\n"
        ".space " PAD_STR(ENGINE_LAYOUT_PAD) "\n"
        ".popsection\n");

#endif
