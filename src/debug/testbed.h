#ifndef TESTBED_H
#define TESTBED_H

/*
 * Automated robustness checks for the running scene (ROADMAP_v2 P2.0 / S0).
 * Started from the Debug tab; results go to the debug log.
 *
 * Reset Soak: one warm-up reset, then 10 measured scene resets (a reset every
 *   30 frames). Logs "SOAK,resets=10,heap_before=..,heap_after=..,delta=..".
 *   A leak-free scene reports delta=0.
 *
 * Menu Sweep: with the menu closed, steps every item of every tab except
 *   Controls and Debug (and "Reset Scene") through all its options, holding
 *   each for 15 frames so the scene applies it, then restores the original
 *   values. Logs one "SWEEP,<tab>,<item>,<options>" line per item and
 *   "SWEEP,END,...". Run it with RDP Check on to validate every combination.
 */

#include <stdbool.h>
#include "../scene/scene.h"
#include "../ui/menu.h"

void testbed_init(SceneManager *mgr, Menu *menu);
void testbed_request_reset_soak(void);
void testbed_request_menu_sweep(void);
void testbed_update(void);            // once per frame, after scene_manager_update()
bool testbed_busy(void);
const char *testbed_status(void);     // short on-screen status, or NULL when idle

#endif
