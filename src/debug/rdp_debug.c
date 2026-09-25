#include "rdp_debug.h"
#include <libdragon.h>
#include "engine_debug.h"
#include "debug_menu.h"

/*
 * The trace starts printing when the RSP reaches the rdpq_debug_log(true)
 * marker, which lands somewhere inside the frame being drawn, so a one-frame
 * window only catches that frame's tail. The capture therefore spans two
 * frames; tools/rdp_log_to_hex.py keeps the second (complete) frame, which
 * starts at its SET_COLOR_IMAGE.
 */
#define CAPTURE_FRAMES 2

// libdragon internal (src/rdpq/rdpq_debug_internal.h): by default the log
// coalesces runs of triangles into one summary line without command words.
// SHOWTRIS prints every triangle so the capture can be validated offline.
// rdpq_debug_start() resets the flags, so set it after starting.
extern int __rdpq_debug_log_flags;
#define RDPQ_LOG_FLAG_SHOWTRIS 0x00000001
// libdragon internal (rdpq_debug_internal.h): fetches the RDP commands queued
// since the last fetch and validates and prints them
extern void (*rdpq_trace)(void);

static bool capture_requested = false;
#if ENGINE_DEBUG
static int  frames_left = 0;
static bool started_validator = false;
#endif

void rdp_debug_request_capture(void) {
    capture_requested = true;
}

void rdp_debug_frame_begin(void) {
#if ENGINE_DEBUG
    if (!capture_requested || frames_left > 0) return;
    capture_requested = false;

    // The trace engine (validator) must run for rdpq_debug_log to print.
    // Start it at a clean frame boundary, as the Debug tab does.
    rspq_wait();
    started_validator = !debug_rdp_check_enabled();
    if (started_validator) rdpq_debug_start();

    __rdpq_debug_log_flags |= RDPQ_LOG_FLAG_SHOWTRIS;
    debugf("RDPLOG_BEGIN\n");
    rdpq_debug_log(true);
    frames_left = CAPTURE_FRAMES;
#endif
}

void rdp_debug_frame_end(void) {
#if ENGINE_DEBUG
    if (frames_left == 0) return;
    if (--frames_left > 0) return;
    rdpq_debug_log(false);
    // The validator prints from the RSP interrupt with interrupts off, a
    // buffer at a time. rspq_wait() gives up after 200 ms and checks the time
    // before the syncpoint, so a long print inside it reports an RSP crash
    // although the RSP is idle: in ares a frame of ~1,000 lines did (~0.2 ms a
    // line; over USB it is slower). So wait for the RSP without a time limit,
    // print what is left here, outside the interrupt, then sync
    rspq_syncpoint_t queued = rspq_syncpoint_new();
    rspq_flush();
    while (!rspq_syncpoint_check(queued)) {}
    if (rdpq_trace) rdpq_trace();
    rspq_wait();                 // every captured command is printed
    __rdpq_debug_log_flags &= ~RDPQ_LOG_FLAG_SHOWTRIS;
    if (started_validator) rdpq_debug_stop();
    started_validator = false;
    debugf("RDPLOG_END\n");
#endif
}

void rdp_debug_crash_test(void) {
#if ENGINE_DEBUG
    assertf(0, "Crash test from the Debug tab (this is intentional)");
#endif
}
