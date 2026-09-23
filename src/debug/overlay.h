#ifndef OVERLAY_H
#define OVERLAY_H

/*
 * Debug overlay pages (ROADMAP_v2 P1.6).
 *
 * Draws the page selected in the Debug tab (or cycled with D-Up) on top of
 * the scene: Stats, Profiler, Memory, Frame, RSP. Panels are translucent
 * rectangles in standard (1-cycle) mode — hardware-safe — and text uses the
 * debug mono font. The overlay times itself in PROF_OVERLAY; text costs about
 * 0.2 ms per line on the Analogue 3D, so pages are kept short.
 *
 * Call overlay_draw() after the scene, while the framebuffer is attached.
 */

void overlay_draw(float budget_ms);

#endif
