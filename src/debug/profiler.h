#ifndef PROFILER_H
#define PROFILER_H

/*
 * CPU scope profiler (ROADMAP_v2 P1.3).
 *
 * The engine owns its timers because libdragon's profile.h only reports via
 * profile_dump(). Scopes use the CPU tick counter (TICKS_READ, 46.875 MHz):
 *
 *     PROF_BEGIN(PROF_FLOOR);
 *     floor_draw(...);
 *     PROF_END(PROF_FLOOR);
 *
 * A slot may be entered many times per frame (e.g. once per mesh); times and
 * call counts accumulate. Nested slots are timed independently, so a parent's
 * time includes its children. Scopes compile out when ENGINE_PROFILE=0
 * (release builds) and are skipped at runtime when the Debug tab's
 * Profiler item is Off.
 *
 * FRAME, WAIT_DISPLAY and LIMITER are always measured (three reads per frame)
 * so CPU work = FRAME - WAIT_DISPLAY - LIMITER can be compared with the
 * profiler on and off.
 */

#include <stdint.h>
#include <stdbool.h>
#include "engine_debug.h"

typedef enum {
    PROF_FRAME,             // wall time, loop top to loop top
    PROF_WAIT_DISPLAY,      // blocked in display_get() waiting for a free framebuffer
    PROF_LIMITER,           // busy-wait of the 30 FPS limiter
    PROF_UPDATE,            //   scene_manager_update()
    PROF_INPUT,             //     action/input polling
    PROF_PHYSICS,           //     physics_world_update()
    PROF_PARTICLE_UPDATE,   //     particle_update()
    PROF_SCENE_SYS,         //     camera_update + collision_test_all
    PROF_DRAW,              //   scene_manager_draw() (CPU side of rendering)
    PROF_SKY,               //     sky_draw()
    PROF_FLOOR,             //     floor_draw()
    PROF_SHADOWS,           //     shadow pass
    PROF_OBJECTS,           //     per-object draw callbacks
    PROF_MESH_CULL,         //       mesh_draw frustum test
    PROF_MESH_LIGHT,        //       mesh_draw lighting_calculate
    PROF_MESH_TRIS,         //       mesh_draw transform + rdpq_triangle loop
    PROF_PARTICLE_DRAW,     //     particle_draw()
    PROF_HUD,               //     demo HUD text
    PROF_MENU,              //     menu_draw()
    PROF_OVERLAY,           //   debug overlay
    PROF_AUDIO,             //   snd_update()
    PROF_SLOT_COUNT
} ProfSlot;

typedef struct {
    uint32_t last_us[PROF_SLOT_COUNT];   // completed frame
    uint16_t calls[PROF_SLOT_COUNT];     // completed frame
    float    avg_us[PROF_SLOT_COUNT];    // exponential moving average (~32 frames)
    uint32_t peak_us[PROF_SLOT_COUNT];   // since profiler_reset_peaks()
    uint32_t frame_index;
} ProfilerFrame;

void  profiler_init(void);
void  profiler_set_enabled(bool enabled);
void  profiler_frame_begin(void);
void  profiler_frame_end(uint32_t frame_ticks);   // publishes the frame
const ProfilerFrame *profiler_get(void);
const char *profiler_slot_name(ProfSlot s);
int   profiler_slot_depth(ProfSlot s);            // indentation level for display
void  profiler_reset_peaks(void);
float profiler_cpu_ms(void);                      // avg (FRAME - WAIT - LIMITER) in ms
void  profiler_dump_csv(void);                    // PROF_HDR / PROF_AVG / PROF_PEAK rows

// Internal: current-frame accumulators used by the macros
extern bool     g_prof_on;
extern uint32_t g_prof_ticks[PROF_SLOT_COUNT];
extern uint16_t g_prof_calls[PROF_SLOT_COUNT];

static inline void profiler_record(ProfSlot s, uint32_t ticks) {
    g_prof_ticks[s] += ticks;
    g_prof_calls[s]++;
}

#if ENGINE_PROFILE
  #define PROF_BEGIN(slot) \
      uint32_t __prof_t0_##slot = g_prof_on ? TICKS_READ() : 0
  #define PROF_END(slot) \
      do { if (g_prof_on) profiler_record(slot, TICKS_DISTANCE(__prof_t0_##slot, TICKS_READ())); } while (0)
#else
  #define PROF_BEGIN(slot) ((void)0)
  #define PROF_END(slot)   ((void)0)
#endif

#endif
