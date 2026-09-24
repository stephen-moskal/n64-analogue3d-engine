#ifndef FRAMETIME_H
#define FRAMETIME_H

/*
 * Frame-time history (ROADMAP_v2 P1.5).
 *
 * Keeps the last 256 frames of wall time and CPU work time (microseconds).
 * Statistics are computed on demand (overlay, CSV dump), not every frame.
 *   low1_fps = 1000 / mean of the slowest 1 % of frames (ms)
 *   over_budget = frames whose CPU work exceeded the budget by > 10 %
 *
 * Note: with triple buffering the loop is paced by framebuffer availability,
 * not by vsync, so per-iteration wall time jitters (about 12.5-21 ms on the
 * A3D at a steady 60 FPS). CPU time is the overrun signal; wall time gives fps.
 *
 * What the player sees is measured separately (S6.2): the engine's vblank
 * handler reports how many vblanks each presented frame stayed on screen
 * (frametime_record_present). At a steady 60 FPS every frame shows for 1
 * vblank; a frame shown longer than the target interval (1 at 60, 2 at 30)
 * is "late": a visible hitch. A flip that happens after the VI has started
 * scanning the picture is "torn": the lines below it come from the new frame,
 * the lines above from the old one (D18: the RDP validator delays the vblank
 * interrupt this way).
 * Pure C (no libdragon) so it can be unit-tested on the host.
 */

#include <stdint.h>

#define FRAMETIME_WINDOW   256
#define FRAMETIME_BUCKETS  24      // 1.5 ms each, 0-36 ms; last bucket = overflow
#define FRAMETIME_BUCKET_US 1500
#define FRAMETIME_PRESENT_BUCKETS 4   // shown for 1, 2, 3, 4+ vblanks

typedef struct {
    int      count;                // frames in the window
    float    fps;                  // 1000 / avg_ms
    float    avg_ms, min_ms, max_ms, p99_ms;
    float    low1_fps;
    float    cpu_avg_ms, cpu_max_ms;
    int      over_budget;          // frames whose CPU work > budget + 10 %
    uint16_t histogram[FRAMETIME_BUCKETS];

    // Presented frames (the last FRAMETIME_WINDOW)
    int      presents;
    uint16_t present_hist[FRAMETIME_PRESENT_BUCKETS];   // shown for 1, 2, 3, 4+ vblanks
    int      late;                 // shown longer than the target interval
    float    present_avg_vblanks;
    int      torn;                 // flips after the active picture had started
    int      torn_worst_halfline;  // latest such flip (VI half-line), 0 if none
} FrameTimeStats;

void frametime_record(uint32_t frame_us, uint32_t cpu_us);
// A frame was replaced on screen after being shown for this many vblanks.
// torn_halfline: the VI half-line the flip happened at when that was inside
// the active picture (a tear), else 0. Safe to call from the vblank interrupt.
void frametime_record_present(int vblanks, int torn_halfline);
void frametime_get(FrameTimeStats *out, float budget_ms);
void frametime_reset(void);
void frametime_dump_csv(uint32_t frame_index, float budget_ms);

#endif
