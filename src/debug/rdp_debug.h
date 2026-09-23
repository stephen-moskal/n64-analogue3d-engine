#ifndef RDP_DEBUG_H
#define RDP_DEBUG_H

/*
 * RDP command capture and crash test (ROADMAP_v2 P1.10). Debug builds only.
 *
 * One-frame capture: Debug tab > RDP Log > Capture! logs every RDP command of
 * the next frame (address, 64-bit word, disassembly) to the debug log between
 * "RDPLOG_BEGIN" and "RDPLOG_END" lines. If the validator is off it is started
 * for that frame and stopped afterwards. Convert the capture for offline
 * validation with tools/rdp_log_to_hex.py, then run rdpvalidate in the
 * container (see docs/DEBUGGING.md).
 *
 * Crash test: Debug tab > Crash Test > Assert! triggers an assertf() so the
 * inspector screen and the USB backtrace can be checked on hardware.
 */

void rdp_debug_request_capture(void);
void rdp_debug_frame_begin(void);   // before display_get()/rdpq_attach()
void rdp_debug_frame_end(void);     // after rdpq_detach_show()
void rdp_debug_crash_test(void);

#endif
