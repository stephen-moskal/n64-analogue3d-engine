#include "engine.h"
#include "../input/input.h"
#include "../ui/text.h"
#include "../audio/audio.h"
#include "../render/atmosphere.h"
#include "../debug/engine_debug.h"
#include "../debug/debug_menu.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "../debug/memstats.h"
#include "../debug/frametime.h"
#include "../debug/overlay.h"
#include "../debug/rdp_debug.h"
#include "../debug/testbed.h"
#include <string.h>

// libdragon display APIs still marked preview at 39d0d6096, used here only:
// display_set_fps_limit, display_get_delta_time, display_get_zbuf,
// vi_install_vblank_handler, vi_get_scanline and vi_read. With LIBDRAGON_PREVIEW=1 each use warns
// (deprecated); the warnings are silenced for this file alone, so a new
// preview use elsewhere still shows in the build output.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define PACE_TIMEOUT_MS 100      // low-latency pacing never waits longer for a frame to show

static int          fps_limit = 0;     // 0 = display rate
static EnginePacing pacing = ENGINE_PACING_THROUGHPUT;
static surface_t   *zbuf;

void engine_set_fps_limit(int fps) {
    if (fps == fps_limit) return;
    fps_limit = fps;
    display_set_fps_limit((float)fps);
}

int engine_fps_limit(void) { return fps_limit; }

float engine_frame_budget_ms(void) {
    return (fps_limit == 30) ? 33.33f : 16.67f;
}

void engine_set_pacing(EnginePacing p) { pacing = p; }
EnginePacing engine_pacing(void) { return pacing; }

surface_t *engine_zbuf(void) { return zbuf; }

static const surface_t *frame_fb;
const surface_t *engine_framebuffer(void) { return frame_fb; }

// Presented frames: at every vblank, check whether the VI scans out another
// framebuffer from now on; if so, the previous one was on screen for
// vblanks_shown vblanks (frametime.h). Runs after the display module's own
// vblank handler, which picks the framebuffer with vi_show(); libdragon
// writes the VI registers only after every handler has run, so the origin is
// read with vi_read(), the value about to take effect (the hardware register
// still holds the old one: until S9 each flip was seen a vblank late). The
// current VI half-line is when the flip happens: past the start of the
// active picture, the flip tears the frame (D18: the RDP validator holds
// interrupts off while it validates, and the vblank interrupt runs late).
static volatile uint32_t vblank_count;
static volatile uint32_t shown_origin;   // framebuffer on screen (physical address)
static uint32_t vi_origin_last;
static int      vblanks_shown;

// Input lag: the vblank of the controller read each framebuffer's frame used
// (input_timing), matched when that framebuffer reaches the screen
static volatile uint32_t lag_fb[ENGINE_FB_COUNT];       // physical address, 0 = unused slot
static volatile uint32_t lag_vblank[ENGINE_FB_COUNT];
static volatile bool     lag_pending[ENGINE_FB_COUNT];

static void lag_frame_begin(const surface_t *fb, uint32_t sample_vblank) {
    uint32_t phys = PhysicalAddr(fb->buffer);
    disable_interrupts();
    int slot = -1;
    for (int i = 0; i < ENGINE_FB_COUNT && slot < 0; i++) if (lag_fb[i] == phys) slot = i;
    for (int i = 0; i < ENGINE_FB_COUNT && slot < 0; i++) if (lag_fb[i] == 0) slot = i;
    if (slot >= 0) {
        lag_fb[slot] = phys;
        lag_vblank[slot] = sample_vblank;
        lag_pending[slot] = true;
    }
    enable_interrupts();
}

static void on_vblank(void *arg) {
    (void)arg;
    uint32_t vb = ++vblank_count;
    uint32_t origin = vi_read(VI_ORIGIN);
    vblanks_shown++;
    if (origin != vi_origin_last) {
        int line = vi_get_scanline(NULL);
        int active_start = (int)((*VI_V_VIDEO >> 16) & 0x3FF);
        if (vi_origin_last) frametime_record_present(vblanks_shown, line > active_start ? line : 0);
        vi_origin_last = origin;
        vblanks_shown = 0;
        shown_origin = origin;
        for (int i = 0; i < ENGINE_FB_COUNT; i++) {
            if (lag_pending[i] && lag_fb[i] == origin) {
                frametime_record_lag((int)(vb - lag_vblank[i]));
                lag_pending[i] = false;
            }
        }
    }
    // Before libdragon's joypad handler queues this vblank's read: note
    // where it will sit, sample the previous one (taps between frames)
    input_on_vblank(vb);
}

void engine_init(void) {
    // Debug output: ISViewer (emulators) and USB (SummerCart64)
    debug_init_isviewer();
    debug_init_usblog();

    display_init((resolution_t){ .width = ENGINE_SCREEN_W, .height = ENGINE_SCREEN_H, .interlaced = INTERLACE_OFF },
                 DEPTH_16_BPP, ENGINE_FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Memory stats: RDRAM size, heap, stack high-water mark (paints the stack now)
    memstats_init(ENGINE_FB_COUNT, ENGINE_SCREEN_W, ENGINE_SCREEN_H);

    // RDP command queue. The RDP validator (rdpq_debug_start) is toggled from
    // the Debug tab in debug builds; it is off at boot because its CPU cost
    // can push frames past the budget (defect D18).
    rdpq_init();

    dfs_init(DFS_DEFAULT_LOCATION);   // before anything loads from rom:/

    // Vblank handlers run in install order: the display's (display_init),
    // the engine's, then the joypad's (joypad_init, in input_init)
    vi_install_vblank_handler(on_vblank, NULL);
    input_init();
    text_init();
    snd_init();
    atmosphere_init();

    zbuf = display_get_zbuf();
}

#ifndef NDEBUG
// D32: the first seconds after boot ran slow on the A3D. For the first
// BOOT_LOG_SECONDS, print one BOOT row per second with that second's
// averages (debug builds; needs the profiler on, as it is at boot).
#define BOOT_LOG_SECONDS 20
static void boot_log(uint32_t now) {
    static uint32_t sec_start;
    static int      sec = -1, frames;
    static uint64_t acc[6];      // frame, wait_display, update, draw, audio, rdp busy (us)
    if (sec >= BOOT_LOG_SECONDS) return;
    if (sec < 0) {
        sec_start = now;
        sec = 0;
        debugf("BOOT_HDR,sec,frames,frame_ms,wait_ms,update_ms,draw_ms,audio_ms,rdp_ms\n");
    }
    const ProfilerFrame *pf = profiler_get();
    const RdpCounters *rdp = profiler_rdp_get();
    acc[0] += pf->last_us[PROF_FRAME];
    acc[1] += pf->last_us[PROF_WAIT_DISPLAY];
    acc[2] += pf->last_us[PROF_UPDATE];
    acc[3] += pf->last_us[PROF_DRAW];
    acc[4] += pf->last_us[PROF_AUDIO];
    acc[5] += rdp->available ? (uint64_t)(rdp->busy_ms * 1000.0f) : 0;
    frames++;
    if (TICKS_DISTANCE(sec_start, now) >= (int32_t)TICKS_PER_SECOND) {
        float n = (float)frames * 1000.0f;
        debugf("BOOT,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", sec, frames,
               acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n, acc[4] / n, acc[5] / n);
        memset(acc, 0, sizeof(acc));
        frames = 0;
        sec++;
        sec_start = now;
    }
}
#endif

// Fill the audio buffers if this is the point of the frame the sound module
// is set to poll at (SndPollPoint; the audio benchmark compares them)
static inline void audio_poll(SndPollPoint point, float dt) {
    if (snd_get_poll_point() != point) return;
    PROF_BEGIN(PROF_AUDIO);
    snd_update(dt);
    PROF_END(PROF_AUDIO);
}

// Low-latency pacing: do not render ahead. Wait until the frame submitted
// last is on screen, so the next one starts right after a vblank and shows
// at the following one (while it fits the budget).
static void pace_wait(uint32_t last_fb) {
    if (pacing != ENGINE_PACING_LOW_LATENCY || !last_fb) return;
    uint32_t t0 = TICKS_READ();
    while (shown_origin != last_fb &&
           TICKS_DISTANCE(t0, TICKS_READ()) < (int32_t)TICKS_FROM_MS(PACE_TIMEOUT_MS)) { }
    profiler_record(PROF_PACE, TICKS_DISTANCE(t0, TICKS_READ()));
}

void engine_run(const EngineApp *app) {
    debugf("SMozN64 Dev Engine [%s build, %s %s]\n", ENGINE_BUILD_NAME, __DATE__, __TIME__);

    // Variable timestep: logic runs once per rendered frame. The loop's own
    // wall time (frame_ticks) feeds the profiler and the frame-time window;
    // the logic's dt comes from the display (engine.h).
    uint32_t last_ticks = TICKS_READ();
    uint32_t frame_index = 0;
    uint32_t last_fb = 0;            // physical address of the frame submitted last
    profiler_init();

    while (1) {
        uint32_t now = TICKS_READ();
        uint32_t frame_ticks = (uint32_t)TICKS_DISTANCE(last_ticks, now);
        float dt = display_get_delta_time();
        last_ticks = now;

        // Publish last frame's counters and timings, start this frame
        if (frame_index > 0) {
            profiler_frame_end(frame_ticks);
            const ProfilerFrame *pf = profiler_get();
            uint32_t f_us = pf->last_us[PROF_FRAME];
            uint32_t idle = pf->last_us[PROF_WAIT_DISPLAY] + pf->last_us[PROF_PACE] +
                            pf->last_us[PROF_WAIT_INPUT];
            frametime_record(f_us, f_us > idle ? f_us - idle : 0);
#ifndef NDEBUG
            boot_log(now);
#endif
        }
        memstats_update();
        profiler_frame_begin();
        profiler_set_enabled(debug_profiler_enabled());
        stats_frame_begin();
        frame_index++;

        if (dt > ENGINE_MAX_DT) dt = ENGINE_MAX_DT;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        pace_wait(last_fb);

        audio_poll(SND_POLL_BEFORE_DISPLAY, dt);

        // Time blocked waiting for a free framebuffer is always measured
        uint32_t t_wait = TICKS_READ();
        surface_t *fb = display_get();
        profiler_record(PROF_WAIT_DISPLAY, TICKS_DISTANCE(t_wait, TICKS_READ()));
        frame_fb = fb;
        audio_poll(SND_POLL_AFTER_DISPLAY, dt);

        // Input as of the vblank display_get() woke on (input.h), then the
        // game: everything the frame shows reacts to it
        input_poll(dt, profiler_cpu_ms(), engine_frame_budget_ms());
        lag_frame_begin(fb, input_timing()->vblank);

        PROF_BEGIN(PROF_UPDATE);
        scene_manager_update(app->scenes, dt);
        PROF_END(PROF_UPDATE);

        // Debug tooling (Debug tab, testbed, CSV dumps), then the game's logic
        debug_menu_update();
        testbed_update();
        if (debug_consume_dump_request()) {
            stats_dump_csv(frame_index);
            profiler_dump_csv();
            profiler_rsp_dump_csv(frame_index);
            frametime_dump_csv(frame_index, engine_frame_budget_ms());
            memstats_dump_csv(frame_index);
        }
        if (app->on_frame) app->on_frame(dt);
        if (debug_consume_reset_peaks_request()) {
            profiler_reset_peaks();
            frametime_reset();
            memstats_reset_baseline();
            input_reset_timing();
        }
        input_end_frame(dt);             // rumble changes go out while the SI is idle

        // Render
        rdp_debug_frame_begin();   // one-frame RDP capture, if requested
        rdpq_attach(fb, zbuf);
        PROF_BEGIN(PROF_DRAW);
        scene_manager_draw(app->scenes);
        PROF_END(PROF_DRAW);

        // Debug overlay page (hidden while the menu is open)
        if (!app->menu || !app->menu->is_open) {
            overlay_draw(engine_frame_budget_ms());
        }
        const char *tb = testbed_status();
        if (tb) {
            TextBoxConfig tbc = { .x = 12, .y = 30, .font_id = FONT_DEBUG_MONO,
                                  .color = RGBA32(0xFF, 0x80, 0x40, 0xFF) };
            text_draw(&tbc, tb);
        }
        rdpq_detach_show();
        last_fb = PhysicalAddr(fb->buffer);
        rdp_debug_frame_end();
        audio_poll(SND_POLL_AFTER_PRESENT, dt);
        // No frame limiter here: display_set_fps_limit makes display_get()
        // wait (counted in wait_display).
    }
}
