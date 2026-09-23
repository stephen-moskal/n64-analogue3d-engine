#ifndef MEMSTATS_H
#define MEMSTATS_H

/*
 * Memory statistics (ROADMAP_v2 P1.4).
 *
 * - RDRAM size and Expansion Pak detection.
 * - Heap usage via sys_get_heap_stats() (malloc'd bytes, incl. libdragon's
 *   top-of-heap allocations). `heap_delta` is relative to the first sample
 *   after boot, which makes leaks visible (e.g. Reset Scene x10).
 * - Stack high-water mark: the 64 KiB stack at the top of RDRAM is painted
 *   with a pattern at init; the scan finds the deepest word ever touched.
 * - Framebuffer and Z-buffer sizes (fixed by the display config).
 *
 * Sampling is cheap but not free, so memstats_update() refreshes every 30 frames.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int  rdram_total;        // bytes (4 MB or 8 MB)
    bool expansion_pak;
    int  heap_total;         // bytes available to malloc
    int  heap_used;          // bytes in use
    int  heap_peak;          // highest heap_used seen
    int  heap_delta;         // heap_used - first sample (leak indicator)
    int  fb_bytes;           // all framebuffers
    int  zbuf_bytes;
    int  stack_size;         // bytes reserved for the stack
    int  stack_used_peak;    // deepest stack use seen (bytes)
    uint32_t samples;
} MemStats;

void memstats_init(int fb_count, int width, int height);   // call early in main()
void memstats_update(void);                               // once per frame
void memstats_reset_baseline(void);                       // heap_delta := 0
const MemStats *memstats_get(void);
void memstats_dump_csv(uint32_t frame_index);              // MEM_HDR / MEM rows

#endif
