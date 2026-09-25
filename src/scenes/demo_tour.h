#ifndef DEMO_TOUR_H
#define DEMO_TOUR_H

// Screenshot tour (make TOUR=1 -> engine-debug-tour.z64): walks the demo
// through a fixed list of states on a timer, without a controller: options
// (atmosphere, shadows, point lights, UI style), the Start menu on a tab, the
// debug overlay pages, the dialog, and finally a benchmark. Each state logs
// "TOUR,<step>,<name>,<seconds>" as it begins, so a capture script (or a
// person with the emulator's screenshot key) knows what is on screen. It
// makes the README images reproducible and doubles as a visual check that
// the same states still look the same after a change. docs/DEBUGGING.md.

#include "../scene/scene.h"
#include "../ui/menu.h"

void demo_tour_init(SceneManager *scenes, Menu *menu, int debug_tab);
void demo_tour_frame(void);      // app_frame, once per frame

#endif
