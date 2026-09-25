#ifndef ENGINE_H
#define ENGINE_H

// Engine core (ROADMAP_v2 S6): brings the hardware and the engine subsystems
// up, then runs the frame loop. The game (main.c) builds its menus and scenes
// in between and hands them over in an EngineApp. See docs/ENGINE.md.
//
//   engine_init();                       // display, rdpq, DFS, input, text, audio, Z-buffer
//   ... build the Start menu, scene manager, first scene ...
//   engine_run(&(EngineApp){ .scenes = &mgr, .menu = &menu, .on_frame = app_frame });

#include <libdragon.h>
#include "engine_config.h"
#include "../scene/scene.h"
#include "../ui/menu.h"

typedef struct {
    SceneManager *scenes;          // updated and drawn every frame
    const Menu   *menu;            // the Start menu: the debug overlay hides while it is open (may be NULL)
    // Called every frame after the scene update and the debug tooling, before
    // rendering: game-level logic such as scene switches (may be NULL)
    void (*on_frame)(float dt);
} EngineApp;

// Bring up debug output, the display, rdpq, DFS, input, text, audio, the
// atmosphere and the shared Z-buffer. Call once, first.
void engine_init(void);

// The frame loop. Does not return. Each frame: (pacing), a framebuffer
// (display_get), the audio mix, input as of that vblank (input_poll), the
// scene update, debug tooling, the app's on_frame, rumble out, render,
// present. Input is read after the framebuffer wait, not before it, so the
// frame acts on the newest controller read (docs/ENGINE.md, "Frame loop").
//
// dt is display_get_delta_time(): the filtered time between presented
// frames, a whole number of vblanks (1/60 s at a steady 60 FPS), not the
// loop's own wall time, which jitters under triple buffering (D19).
void engine_run(const EngineApp *app);

// Frame rate cap: 30, or 0 for the display rate (60). The display module
// enforces it (display_set_fps_limit): display_get() waits, no busy loop.
void engine_set_fps_limit(int fps);
int  engine_fps_limit(void);

// Frame pacing. THROUGHPUT renders ahead into the free framebuffer (triple
// buffering): the steadiest frame rate, input shown about 2 vblanks after
// its read. LOW_LATENCY starts a frame only once the previous one is on
// screen: 1 vblank, as long as a frame (CPU and RDP) fits the budget; a
// frame that does not waits for the next vblank, like double buffering.
typedef enum {
    ENGINE_PACING_THROUGHPUT,
    ENGINE_PACING_LOW_LATENCY,
} EnginePacing;

void         engine_set_pacing(EnginePacing pacing);
EnginePacing engine_pacing(void);

// Frame budget in ms for the current cap (16.67 or 33.33)
float engine_frame_budget_ms(void);

// The Z-buffer shared by all scenes (ENGINE_SCREEN_W x ENGINE_SCREEN_H, RGBA16),
// from display_get_zbuf(): allocated at the top of RDRAM, away from the
// framebuffers
surface_t *engine_zbuf(void);

#endif
