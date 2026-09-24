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

// The frame loop: measure dt, update the scene, debug tooling, the app's
// on_frame, render, present, pace. Does not return.
void engine_run(const EngineApp *app);

// Frame rate cap set by the game: 30, or 0 for the display rate (60)
extern int engine_target_fps;

// Frame budget in ms for the current cap (16.67 or 33.33)
float engine_frame_budget_ms(void);

// The Z-buffer shared by all scenes (ENGINE_SCREEN_W x ENGINE_SCREEN_H, RGBA16)
surface_t *engine_zbuf(void);

#endif
