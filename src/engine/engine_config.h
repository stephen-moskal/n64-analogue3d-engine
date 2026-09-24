#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

// Engine-wide build constants (ROADMAP_v2 D9). One place for the screen
// format and the limits the renderer derives from it; change them here, never
// as literals in render code. Compile-time, so hot loops fold them.

// Frame buffer: 320x240, 16-bit, triple-buffered (display_init in engine.c)
#define ENGINE_SCREEN_W     320
#define ENGINE_SCREEN_H     240
#define ENGINE_FB_COUNT     3

// Largest frame time handed to update code (s): a long stall (loading, a
// breakpoint) must not make physics take one huge step
#define ENGINE_MAX_DT       0.1f

// Guard band: vertices projected further than this outside the screen would
// overflow the RDP's fixed-point edge coefficients, so a triangle with such a
// vertex is dropped (or clipped where the renderer clips). Screen px.
#define ENGINE_GUARD_MARGIN 1024.0f
#define ENGINE_GUARD_X_MIN  (-ENGINE_GUARD_MARGIN)
#define ENGINE_GUARD_X_MAX  ((float)ENGINE_SCREEN_W + ENGINE_GUARD_MARGIN)
#define ENGINE_GUARD_Y_MIN  (-ENGINE_GUARD_MARGIN)
#define ENGINE_GUARD_Y_MAX  ((float)ENGINE_SCREEN_H + ENGINE_GUARD_MARGIN)

#endif
