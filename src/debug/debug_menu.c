#include "debug_menu.h"
#include "engine_debug.h"
#include "../input/action.h"

static const char *overlay_options[] = {"Off", "Stats", "Profiler", "Memory", "Frame", "RSP"};
static const char *on_off_options[]  = {"On", "Off"};
static const char *off_on_options[]  = {"Off", "On"};
static const char *dump_options[]    = {"---", "Dump!"};
static const char *reset_options[]   = {"---", "Reset!"};

_Static_assert(sizeof(overlay_options) / sizeof(overlay_options[0]) == OVERLAY_PAGE_COUNT,
               "overlay_options must match OverlayPage");

static Menu *dbg_menu = NULL;
static int   dbg_tab  = -1;

static OverlayPage overlay_page     = OVERLAY_OFF;
static bool        profiler_enabled = true;
static bool        rdp_check_active = false;   // validator state actually applied
static bool        dump_requested   = false;
static bool        reset_requested  = false;

void debug_menu_init(Menu *menu, int tab) {
    dbg_menu = menu;
    dbg_tab  = tab;

    // Order must match DebugMenuItem
    menu_add_item(menu, tab, "Overlay",     overlay_options, OVERLAY_PAGE_COUNT, OVERLAY_OFF);
    menu_add_item(menu, tab, "Profiler",    on_off_options, 2, 0);
    menu_add_item(menu, tab, "RDP Check",   off_on_options, 2, 0);
    menu_add_item(menu, tab, "Dump CSV",    dump_options,   2, 0);
    menu_add_item(menu, tab, "Reset Peaks", reset_options,  2, 0);

#if !ENGINE_DEBUG
    // Validator and profiler are compiled out of release builds
    menu_item_set_disabled(menu, tab, DBG_ITEM_RDP_CHECK, true);
    menu_item_set_disabled(menu, tab, DBG_ITEM_PROFILER, true);
    profiler_enabled = false;
#endif
}

// True if no game action is bound to this button (so a fixed shortcut can use it)
static bool button_is_free(PhysicalButton btn) {
    for (int a = 0; a < ACTION_COUNT; a++) {
        if (action_get_binding((GameAction)a) == btn) return false;
    }
    return true;
}

static int item_value(DebugMenuItem item) {
    return menu_get_value(dbg_menu, dbg_tab, item);
}

static void item_set(DebugMenuItem item, int value) {
    dbg_menu->tabs[dbg_tab].items[item].selected = value;
}

void debug_menu_update(void) {
    if (!dbg_menu || dbg_menu->is_open) return;   // apply only after the menu closes

    // Shortcuts (fixed buttons, only when unbound)
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.d_up && button_is_free(BTN_D_UP)) {
        int next = (item_value(DBG_ITEM_OVERLAY) + 1) % OVERLAY_PAGE_COUNT;
        item_set(DBG_ITEM_OVERLAY, next);
    }
    if (pressed.d_down && button_is_free(BTN_D_DOWN)) {
        item_set(DBG_ITEM_DUMP_CSV, 1);
    }

    // Overlay page
    OverlayPage page = (OverlayPage)item_value(DBG_ITEM_OVERLAY);
    if (page != overlay_page) {
        overlay_page = page;
        ENGINE_LOG("[debug] overlay page: %s\n", debug_overlay_page_name(page));
    }

#if ENGINE_DEBUG
    // Profiler collection
    bool prof = (item_value(DBG_ITEM_PROFILER) == 0);
    if (prof != profiler_enabled) {
        profiler_enabled = prof;
        ENGINE_LOG("[debug] profiler %s\n", prof ? "on" : "off");
    }

    // RDP validator: off at boot. Its CPU cost can push frames past 16.7 ms,
    // which shows as flicker on the Analogue 3D (roadmap defect D18).
    bool rdp = (item_value(DBG_ITEM_RDP_CHECK) == 1);
    if (rdp != rdp_check_active) {
        // Toggle only at a clean frame boundary: drain the RSP/RDP first.
        // Starting mid-stream makes the validator read a half-seen frame
        // (bogus SET_COLOR_IMAGE / combiner errors), and stopping while the
        // RSP is paused for a trace fetch can leave it halted (RSP crash in
        // the mixer's rspq_highpri_sync). This runs before display_get() /
        // rdpq_attach(), so the next frame is validated from its first command.
        rspq_wait();
        if (rdp) rdpq_debug_start();
        else     rdpq_debug_stop();
        rdp_check_active = rdp;
        ENGINE_LOG("[debug] RDP validator %s\n", rdp ? "started" : "stopped");
    }
#endif

    // Self-resetting one-shot items
    if (item_value(DBG_ITEM_DUMP_CSV) == 1) {
        item_set(DBG_ITEM_DUMP_CSV, 0);
        dump_requested = true;
        ENGINE_LOG("[debug] CSV dump requested\n");
    }
    if (item_value(DBG_ITEM_RESET_PEAKS) == 1) {
        item_set(DBG_ITEM_RESET_PEAKS, 0);
        reset_requested = true;
        ENGINE_LOG("[debug] reset peaks requested\n");
    }
}

OverlayPage debug_overlay_page(void) { return overlay_page; }

const char *debug_overlay_page_name(OverlayPage page) {
    if (page < 0 || page >= OVERLAY_PAGE_COUNT) return "?";
    return overlay_options[page];
}

bool debug_profiler_enabled(void)  { return profiler_enabled; }
bool debug_rdp_check_enabled(void) { return rdp_check_active; }

bool debug_consume_dump_request(void) {
    bool r = dump_requested;
    dump_requested = false;
    return r;
}

bool debug_consume_reset_peaks_request(void) {
    bool r = reset_requested;
    reset_requested = false;
    return r;
}
