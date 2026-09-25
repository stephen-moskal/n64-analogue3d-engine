#include "input.h"
#include <libdragon.h>
#include <string.h>
#include "../debug/profiler.h"

// The joybus runs over the SI; libdragon keeps its register pointer private.
// Idle = no DMA or IO in flight and no interrupt waiting to be serviced: the
// joybus queue is empty and every message's callback has run.
#define SI_STATUS         ((volatile uint32_t *)0xA4800018)
#define SI_STATUS_BUSY    0x0003        // DMA busy | IO busy
#define SI_STATUS_PENDING 0x1000        // interrupt raised

// Which read a frame waits for. The engine's vblank handler runs before
// libdragon's joypad handler, which queues the vblank's read: if the SI is
// idle at that moment, the read is the first joybus message, and it is in
// after two SI interrupts (the PIF-RAM write, then the reply). A message
// queued after it (libdragon identifies the ports once a second) is not
// waited for. If another message is ahead of it (rare), the wait falls back
// to "SI idle".

// What the interrupts (vblank sampler, SI hook) share with input_poll
typedef struct {
    joypad_inputs_t in;         // the newest sample
    joypad_style_t  style;
    uint16_t last;              // its buttons
    uint16_t pressed, released; // edges since the frame's snapshot
} PortSample;

static PortSample samples[PAD_PORTS];           // interrupts off to touch
static volatile bool     ready;                 // joypad_init done: the vblank hook may poll
static volatile uint32_t si_irqs;               // SI interrupts so far
static volatile uint32_t vb_index, vb_ticks;    // the latest vblank (engine count), its time
static volatile uint32_t vb_irqs;               // si_irqs at that vblank, before its read was queued
static volatile bool     vb_behind;             // another joybus message was ahead of that read
static volatile uint32_t read_ticks;            // when that read came in (valid if read_in)
static volatile bool     read_in;
static volatile bool     read_taken;            // input_poll sampled the latest vblank's read

static PadState    pads[PAD_PORTS];             // this frame's snapshot
static InputSync   sync_mode = INPUT_SYNC_FRESH;
static InputTiming timing;

static bool  rumble_on = true;                  // master switch
static bool  rumble_hold[PAD_PORTS];            // on until turned off
static float rumble_left[PAD_PORTS];            // pulse seconds left
static bool  rumble_sent[PAD_PORTS];            // what the motor was last told

static bool si_idle(void) {
    return (*SI_STATUS & (SI_STATUS_BUSY | SI_STATUS_PENDING)) == 0;
}

// Runs before libdragon's SI handler (callbacks are prepended), in the same
// interrupt: once the main thread sees the count, the read's callback has run
static void on_si_interrupt(void) {
    uint32_t n = ++si_irqs;
    if (!vb_behind && n - vb_irqs == 2) {
        read_ticks = TICKS_READ();
        read_in = true;
    }
}

// The latest vblank's read is in (vb_* read once by the caller)
static bool read_complete(uint32_t irqs0, bool behind) {
    return behind ? si_idle() : (uint32_t)(si_irqs - irqs0) >= 2;
}

static uint16_t buttons_of(joypad_buttons_t b) {
    return (uint16_t)(
        (b.a << BTN_A) | (b.b << BTN_B) | (b.z << BTN_Z) | (b.start << BTN_START) |
        (b.d_up << BTN_D_UP) | (b.d_down << BTN_D_DOWN) | (b.d_left << BTN_D_LEFT) | (b.d_right << BTN_D_RIGHT) |
        (b.l << BTN_L) | (b.r << BTN_R) |
        (b.c_up << BTN_C_UP) | (b.c_down << BTN_C_DOWN) | (b.c_left << BTN_C_LEFT) | (b.c_right << BTN_C_RIGHT) |
        (b.x << BTN_X) | (b.y << BTN_Y));
}

// libdragon's newest completed read into samples[], edges accumulated.
// Interrupts must be off: the vblank handler and input_poll both call it.
static void sample_locked(void) {
    joypad_poll();
    for (int port = 0; port < PAD_PORTS; port++) {
        PortSample *s = &samples[port];
        s->in = joypad_get_inputs((joypad_port_t)port);
        s->style = joypad_get_style((joypad_port_t)port);
        uint16_t now = buttons_of(s->in.btn);
        s->pressed  |= now & (uint16_t)~s->last;
        s->released |= s->last & (uint16_t)~now;
        s->last = now;
    }
}

void input_init(void) {
    joypad_init();                          // reads every port once before it returns
    register_SI_handler(on_si_interrupt);
    action_init();
    memset(samples, 0, sizeof(samples));
    memset(pads, 0, sizeof(pads));
    input_reset_timing();
    ready = true;
}

// Interrupt context, before libdragon's joypad handler queues this vblank's
// read. Notes where the read will sit in the joybus queue. Then, unless the
// frame loop already took it, samples the previous read (complete long ago):
// a tap the loop would skip (below 60 FPS) is in the edges. At 60 FPS the
// loop takes every read and the read is not delayed by the sampling.
void input_on_vblank(uint32_t vblank) {
    if (!ready) return;
    vb_index = vblank;
    vb_ticks = TICKS_READ();
    vb_irqs = si_irqs;
    vb_behind = !si_idle();
    read_in = false;
    if (!read_taken) sample_locked();
    read_taken = false;
}

static PadStyle style_of(joypad_style_t s) {
    switch (s) {
    case JOYPAD_STYLE_N64:   return PAD_N64;
    case JOYPAD_STYLE_GCN:   return PAD_GCN;
    case JOYPAD_STYLE_MOUSE: return PAD_MOUSE;
    default:                 return PAD_NONE;
    }
}

static PadAccessory accessory_of(joypad_accessory_type_t t) {
    switch (t) {
    case JOYPAD_ACCESSORY_TYPE_NONE:           return PAD_ACC_NONE;
    case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return PAD_ACC_CONTROLLER_PAK;
    case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:     return PAD_ACC_RUMBLE_PAK;
    case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK:   return PAD_ACC_TRANSFER_PAK;
    case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR:     return PAD_ACC_BIO_SENSOR;
    case JOYPAD_ACCESSORY_TYPE_SNAP_STATION:   return PAD_ACC_SNAP_STATION;
    default:                                   return PAD_ACC_UNKNOWN;
    }
}

// The accessory and the motor change rarely (a Rumble Pak plugged in):
// refreshed for one port per frame, and for a controller just connected
static int refresh_port;

static void build_pad(int port, const PortSample *s) {
    PadState *pad = &pads[port];
    PadStyle before = pad->style;
    pad->style = style_of(s->style);
    pad->connected = pad->style != PAD_NONE && before == PAD_NONE;
    pad->disconnected = pad->style == PAD_NONE && before != PAD_NONE;
    pad->held = s->last;
    pad->pressed = s->pressed;
    pad->released = s->released;
    pad->stick_x = s->in.stick_x;
    pad->stick_y = s->in.stick_y;
    pad->cstick_x = s->in.cstick_x;
    pad->cstick_y = s->in.cstick_y;
    pad->trigger_l = s->in.analog_l;
    pad->trigger_r = s->in.analog_r;
    if (pad->style == PAD_NONE) {
        pad->accessory = PAD_ACC_NONE;
        pad->rumble = false;
    } else if (pad->connected || port == refresh_port) {
        pad->accessory = pad->style == PAD_N64 ? accessory_of(joypad_get_accessory_type((joypad_port_t)port))
                                               : PAD_ACC_NONE;
        pad->rumble = joypad_get_rumble_supported((joypad_port_t)port);
    }
    if (pad->disconnected) {                // a new controller starts with its motor off
        rumble_hold[port] = false;
        rumble_left[port] = 0.0f;
        rumble_sent[port] = false;
    }
}

void input_poll(float dt) {
    // Wait for the latest vblank's read
    uint32_t t0 = TICKS_READ();
    disable_interrupts();
    uint32_t irqs0 = vb_irqs;
    bool behind = vb_behind;
    enable_interrupts();
    bool fresh = read_complete(irqs0, behind);
    if (!fresh && sync_mode == INPUT_SYNC_FRESH) {
        const uint32_t limit = TICKS_FROM_US(INPUT_FRESH_TIMEOUT_US);
        while (!(fresh = read_complete(irqs0, behind))) {
            uint32_t t = TICKS_READ();
            if ((uint32_t)TICKS_DISTANCE(t0, t) >= limit) {
                timing.timeouts++;
                break;
            }
            while ((uint32_t)TICKS_DISTANCE(t, TICKS_READ()) < TICKS_FROM_US(5)) { }   // not back to back
        }
    }
    uint32_t t1 = TICKS_READ();
    profiler_record(PROF_WAIT_INPUT, TICKS_DISTANCE(t0, t1));

    PROF_BEGIN(PROF_INPUT);
    PortSample snap[PAD_PORTS];
    disable_interrupts();
    // Again under the lock: a vblank between the wait and here started a new read
    fresh = read_complete(vb_irqs, vb_behind);
    uint32_t vb = vb_index, vbt = vb_ticks, rt = read_ticks;
    bool in = read_in;
    sample_locked();
    read_taken = fresh;
    memcpy(snap, samples, sizeof(snap));
    for (int p = 0; p < PAD_PORTS; p++) samples[p].pressed = samples[p].released = 0;
    enable_interrupts();

    for (int p = 0; p < PAD_PORTS; p++) build_pad(p, &snap[p]);
    refresh_port = (refresh_port + 1) % PAD_PORTS;

    // Which vblank's read the frame uses, and when that read came in
    timing.fresh = fresh;
    timing.vblank = fresh ? vb : vb - 1;
    timing.read_us = (fresh && in) ? (float)TICKS_TO_US(TICKS_DISTANCE(vbt, rt)) : -1.0f;
    timing.wait_us = (float)TICKS_TO_US(TICKS_DISTANCE(t0, t1));
    if (timing.read_us >= 0.0f) {
        timing.read_avg_us += (timing.read_us - timing.read_avg_us) * (1.0f / 32.0f);
        timing.read_sum_us += timing.read_us;
        timing.reads_timed++;
    }
    timing.wait_avg_us += (timing.wait_us - timing.wait_avg_us) * (1.0f / 32.0f);
    timing.wait_sum_us += timing.wait_us;
    if (timing.wait_us > timing.wait_max_us) timing.wait_max_us = timing.wait_us;
    timing.frames++;
    if (fresh) timing.fresh_frames++;

    action_update(pads, dt);
    PROF_END(PROF_INPUT);
}

void input_end_frame(float dt) {
    for (int p = 0; p < PAD_PORTS; p++) {
        if (rumble_left[p] > 0.0f) {
            rumble_left[p] -= dt;
            if (rumble_left[p] < 0.0f) rumble_left[p] = 0.0f;
        }
        bool on = rumble_on && pads[p].rumble && (rumble_hold[p] || rumble_left[p] > 0.0f);
        if (on != rumble_sent[p]) {
            joypad_set_rumble_active((joypad_port_t)p, on);
            rumble_sent[p] = on;
        }
    }
}

void input_set_sync(InputSync sync) { sync_mode = sync; }
InputSync input_sync(void) { return sync_mode; }

const PadState *input_pad(int port) {
    static const PadState none;
    return (port >= 0 && port < PAD_PORTS) ? &pads[port] : &none;
}

int input_port_pressed(PadButton btn) {
    if (btn < 0 || btn >= BTN_COUNT) return -1;
    for (int p = 0; p < PAD_PORTS; p++)
        if (pads[p].pressed & PAD_BIT(btn)) return p;
    return -1;
}

// ============================================================
// Rumble
// ============================================================

void input_rumble(int port, bool on) {
    if (port >= 0 && port < PAD_PORTS) rumble_hold[port] = on;
}

void input_rumble_pulse(int port, float seconds) {
    if (port < 0 || port >= PAD_PORTS) return;
    if (seconds > rumble_left[port]) rumble_left[port] = seconds;
}

void input_rumble_player(int player, float seconds) {
    input_rumble_pulse(action_port(player), seconds);
}

void input_rumble_stop_all(void) {
    memset(rumble_hold, 0, sizeof(rumble_hold));
    memset(rumble_left, 0, sizeof(rumble_left));
}

void input_rumble_enable(bool on) { rumble_on = on; }
bool input_rumble_enabled(void) { return rumble_on; }

// ============================================================
// Timing
// ============================================================

const InputTiming *input_timing(void) { return &timing; }

void input_reset_timing(void) {
    InputTiming t = { .vblank = timing.vblank, .fresh = timing.fresh, .read_us = timing.read_us,
                      .wait_us = timing.wait_us, .read_avg_us = timing.read_avg_us,
                      .wait_avg_us = timing.wait_avg_us };
    timing = t;
}
