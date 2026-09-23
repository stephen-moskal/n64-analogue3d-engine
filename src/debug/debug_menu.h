#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

/*
 * Debug menu tab — in-engine toggles for the Phase 1 tooling.
 *
 * The tab lives in the global start menu (built in main.c). Values are read
 * every frame while the menu is closed, so B (Cancel) reverts changes like any
 * other tab. Shortcuts while the menu is closed and the button is not bound to
 * a game action: D-Up cycles overlay pages, D-Down requests a CSV dump.
 */

#include <stdbool.h>
#include "../ui/menu.h"

// Item order in the Debug tab (append new items at the end)
typedef enum {
    DBG_ITEM_OVERLAY,       // Off / Stats / Profiler / Memory / Frame / RSP
    DBG_ITEM_PROFILER,      // On / Off
    DBG_ITEM_RDP_CHECK,     // Off / On  (debug builds only)
    DBG_ITEM_DUMP_CSV,      // --- / Dump!   (self-resetting)
    DBG_ITEM_RESET_PEAKS,   // --- / Reset!  (self-resetting)
    DBG_ITEM_SCENE,         // Demo / Benchmark  (switches scene on menu close)
    DBG_ITEM_BENCH,         // which benchmark: All / Objects / ... (see BenchKind)
    DBG_ITEM_RDP_LOG,       // --- / Capture!  one-frame RDP command log (debug only)
    DBG_ITEM_CRASH_TEST,    // --- / Assert!   trigger assertf() (debug only)
    DBG_ITEM_RESET_SOAK,    // --- / Run!      10 scene resets, heap delta (testbed.h)
    DBG_ITEM_MENU_SWEEP,    // --- / Run!      step every menu option (testbed.h)
    DBG_ITEM_COUNT
} DebugMenuItem;

// Overlay pages (order matches the Overlay item options)
typedef enum {
    OVERLAY_OFF,
    OVERLAY_STATS,
    OVERLAY_PROFILER,
    OVERLAY_MEMORY,
    OVERLAY_FRAMETIME,
    OVERLAY_RSP,
    OVERLAY_PAGE_COUNT
} OverlayPage;

void debug_menu_init(Menu *menu, int tab);

// Call once per frame after the scene update (input has been polled).
void debug_menu_update(void);

OverlayPage debug_overlay_page(void);
const char *debug_overlay_page_name(OverlayPage page);
bool debug_profiler_enabled(void);
bool debug_rdp_check_enabled(void);

// One-shot requests (return true once, then clear)
bool debug_consume_dump_request(void);
bool debug_consume_reset_peaks_request(void);

// Scene switch requested from the Debug tab: returns true once, with the
// target (0 = demo, 1 = benchmark) and the selected benchmark kind.
bool debug_consume_scene_request(int *scene, int *bench_kind);

// Tell the menu which scene is active (e.g. after a benchmark ends) without
// generating a new request.
void debug_menu_set_active_scene(int scene);

#endif
