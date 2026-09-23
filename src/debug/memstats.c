#include "memstats.h"
#include <libdragon.h>
#include <string.h>

// libdragon reserves 64 KiB for the stack at the top of RDRAM (src/system.c
// STACK_SIZE, private); the boot code starts $sp at the top of RDRAM - 0x10.
#define STACK_SIZE_BYTES  0x10000
#define STACK_PAINT       0x5AC0FFEEu
#define STACK_MARGIN      2048          // don't paint the live frames near $sp

static MemStats ms;
static int      heap_baseline = -1;
static uint32_t frame_counter = 0;
static bool     header_sent = false;

static uint32_t *stack_floor(void) {
    return (uint32_t *)(0x80000000u + (uint32_t)__boot_memsize - STACK_SIZE_BYTES);
}

static uint32_t *stack_top(void) {
    return (uint32_t *)(0x80000000u + (uint32_t)__boot_memsize - 0x10);
}

static void paint_stack(void) {
    uint32_t *sp = (uint32_t *)__builtin_frame_address(0);
    uint32_t *end = (uint32_t *)((uint8_t *)sp - STACK_MARGIN);
    // Interrupt handlers push onto this stack; keep them out while painting.
    disable_interrupts();
    for (uint32_t *p = stack_floor(); p < end; p++) *p = STACK_PAINT;
    enable_interrupts();
}

static int scan_stack(void) {
    uint32_t *p = stack_floor();
    uint32_t *top = stack_top();
    while (p < top && *p == STACK_PAINT) p++;
    return (int)((uint8_t *)top - (uint8_t *)p);
}

void memstats_init(int fb_count, int width, int height) {
    memset(&ms, 0, sizeof(ms));
    ms.rdram_total   = get_memory_size();
    ms.expansion_pak = is_memory_expanded();
    ms.fb_bytes      = fb_count * width * height * 2;   // 16-bit color
    ms.zbuf_bytes    = width * height * 2;
    ms.stack_size    = STACK_SIZE_BYTES;
    paint_stack();
    heap_baseline = -1;
}

static void sample(void) {
    heap_stats_t hs;
    sys_get_heap_stats(&hs);
    ms.heap_total = hs.total;
    ms.heap_used  = hs.used;
    if (hs.used > ms.heap_peak) ms.heap_peak = hs.used;
    if (heap_baseline < 0) heap_baseline = hs.used;
    ms.heap_delta = hs.used - heap_baseline;

    int st = scan_stack();
    if (st > ms.stack_used_peak) ms.stack_used_peak = st;
    ms.samples++;
}

void memstats_update(void) {
    if ((frame_counter++ % 30) == 0) sample();
}

void memstats_reset_baseline(void) {
    sample();
    heap_baseline = ms.heap_used;
    ms.heap_delta = 0;
}

const MemStats *memstats_get(void) { return &ms; }

void memstats_dump_csv(uint32_t frame_index) {
    sample();
    if (!header_sent) {
        debugf("MEM_HDR,frame,rdram,expansion_pak,heap_total,heap_used,heap_peak,heap_delta,"
               "fb_bytes,zbuf_bytes,stack_size,stack_used_peak\n");
        header_sent = true;
    }
    debugf("MEM,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
           (unsigned long)frame_index, ms.rdram_total, ms.expansion_pak ? 1 : 0,
           ms.heap_total, ms.heap_used, ms.heap_peak, ms.heap_delta,
           ms.fb_bytes, ms.zbuf_bytes, ms.stack_size, ms.stack_used_peak);
}
