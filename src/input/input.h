#ifndef INPUT_H
#define INPUT_H

/*
 * Input core (ROADMAP_v2 S9): owns libdragon's joypad module and feeds the
 * action layer (action.h).
 *
 * libdragon starts a read of all four ports at every vblank; it completes
 * over the SI a millisecond or two later (ares: 1.75 ms). The engine calls
 * input_poll() once per frame, right after display_get() and the audio mix:
 * with INPUT_SYNC_FRESH it first waits (at most INPUT_FRESH_TIMEOUT_US) for
 * the read that started at the latest vblank, so the frame acts on input a
 * vblank newer than the last completed read. The engine's vblank handler
 * also samples every read (input_on_vblank), so a press and release between
 * two frames (a tap below 60 FPS) still reaches the game.
 *
 * Game code reads actions, or a port's PadState for full control. It must
 * not call joypad_poll() or joypad_get_*() itself: the vblank interrupt
 * polls the joypad module too. docs/INPUT.md.
 */

#include <stdbool.h>
#include <stdint.h>
#include "pad.h"
#include "action.h"

typedef enum {
    INPUT_SYNC_FRESH,       // wait for the read started at the latest vblank (default)
    INPUT_SYNC_LATEST,      // never wait: the newest completed read, up to a vblank older
} InputSync;

#define INPUT_FRESH_TIMEOUT_US 3000

// Engine hooks: init once (joypad_init, the SI hook, action_init); the
// vblank handler (interrupt; it must run before libdragon's joypad handler,
// i.e. be installed before input_init); once per frame before the game
// update; once per frame after it (rumble reaches the pads).
void input_init(void);
void input_on_vblank(uint32_t vblank);
void input_poll(float dt);
void input_end_frame(float dt);

void      input_set_sync(InputSync sync);
InputSync input_sync(void);

// This frame's snapshot of a port (style PAD_NONE: nothing plugged in)
const PadState *input_pad(int port);
// The first port whose button went down this frame (a "press Start to join"
// screen), -1 if none
int input_port_pressed(PadButton btn);

// Rumble: a Rumble Pak or a GameCube controller's motor. Changes reach the
// pad at the end of the frame (input_end_frame), while the SI is idle.
void input_rumble(int port, bool on);                 // on until turned off
void input_rumble_pulse(int port, float seconds);     // on for a while (a longer pulse wins)
void input_rumble_player(int player, float seconds); // a pulse on the player's port
void input_rumble_stop_all(void);
void input_rumble_enable(bool on);                    // master switch (an option); off stops every motor
bool input_rumble_enabled(void);

// The frame's controller read (the Input overlay page, latency tracking,
// the benchmark's BENCH_INPUT rows)
typedef struct {
    // This frame
    uint32_t vblank;        // vblank whose read this frame's input comes from (the engine's count)
    bool     fresh;         // that is the latest vblank's read
    float    read_us;       // that read came in this long after its vblank; -1 unknown
    float    wait_us;       // input_poll waited this long for it
    // Moving averages (~32 frames), for a live display
    float    read_avg_us;
    float    wait_avg_us;
    // Since input_reset_timing()
    uint32_t frames;        // input_poll calls
    uint32_t fresh_frames;  // ... that got the latest vblank's read
    uint32_t reads_timed;   // reads whose arrival time is known
    float    read_sum_us;   // their arrival times, summed (mean = sum / reads_timed)
    float    wait_sum_us;   // every wait, summed (mean = sum / frames)
    float    wait_max_us;
    uint32_t timeouts;      // waits that gave up (INPUT_FRESH_TIMEOUT_US)
} InputTiming;

const InputTiming *input_timing(void);
void input_reset_timing(void);

#endif
