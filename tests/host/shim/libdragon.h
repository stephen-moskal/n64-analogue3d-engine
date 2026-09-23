#ifndef HOST_SHIM_LIBDRAGON_H
#define HOST_SHIM_LIBDRAGON_H

/*
 * Minimal libdragon stand-in for host unit tests (tests/host).
 *
 * Only what the pure-logic modules under test touch: color types, the joypad
 * API used by the action layer (backed by test-settable state), and logging
 * macros. Rendering, display and RSP/RDP APIs are deliberately absent — a
 * module that needs them is not a host-testable module.
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

// --- Joypad ---
typedef enum { JOYPAD_PORT_1 = 0, JOYPAD_PORT_2, JOYPAD_PORT_3, JOYPAD_PORT_4 } joypad_port_t;

typedef struct {
    unsigned a : 1, b : 1, z : 1, start : 1;
    unsigned d_up : 1, d_down : 1, d_left : 1, d_right : 1;
    unsigned l : 1, r : 1;
    unsigned c_up : 1, c_down : 1, c_left : 1, c_right : 1;
} joypad_buttons_t;

typedef struct {
    joypad_buttons_t btn;
    int8_t stick_x, stick_y;
} joypad_inputs_t;

void joypad_init(void);
void joypad_poll(void);
joypad_buttons_t joypad_get_buttons_pressed(joypad_port_t port);
joypad_buttons_t joypad_get_buttons_held(joypad_port_t port);
joypad_buttons_t joypad_get_buttons_released(joypad_port_t port);
joypad_inputs_t  joypad_get_inputs(joypad_port_t port);

// Test control: what the next joypad_poll() reports
void shim_joypad_set(joypad_buttons_t pressed, joypad_buttons_t held,
                     joypad_buttons_t released, int stick_x, int stick_y);

#endif
