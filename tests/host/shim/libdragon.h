#ifndef HOST_SHIM_LIBDRAGON_H
#define HOST_SHIM_LIBDRAGON_H

/*
 * Minimal libdragon stand-in for host unit tests (tests/host).
 *
 * Only what the pure-logic modules under test touch: color types and logging
 * macros. Rendering, display, joypad and RSP/RDP APIs are deliberately absent:
 * a module that needs them is not a host-testable module (the action layer
 * takes PadState snapshots from the input core instead of the joypad).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

// --- Colors ---
typedef struct { uint8_t r, g, b, a; } color_t;
#define RGBA32(rx, gx, bx, ax) ((color_t){ (rx), (gx), (bx), (ax) })

// --- Logging / asserts ---
#define debugf(...)               ((void)0)
#define assertf(cond, ...)        assert(cond)

// --- Timer (seeds and profiling; the tests need no real time) ---
#define TICKS_READ()              0u

#endif
