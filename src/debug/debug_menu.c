#include "debug_menu.h"
#include "engine_debug.h"
#include "../input/action.h"
#include "rdp_debug.h"
#include "testbed.h"
#include "../scenes/benchmark_scene.h"
#include "../engine/util.h"

static const char *const overlay_options[] = {"Off", "Stats", "Profiler", "Memory", "Frame", "RSP", "Input"};
static const char *const on_off_options[]  = {"On", "Off"};
static const char *const off_on_options[]  = {"Off", "On"};
static const char *const dump_options[]    = {"---", "Dump!"};
static const char *const reset_options[]   = {"---", "Reset!"};
static const char *const scene_options[]   = {"Demo", "Benchmark"};
static const char *const bench_options[]   = {"All", "Objects", "Particles", "Lights", "Textures", "Shadows", "Fillrate", "Overload", "Layout", "Audio", "UI", "Latency"};
static const char *const run_options[]     = {"---", "Run!"};
static const char *const capture_options[] = {"---", "Capture!"};
static const char *const crash_options[]   = {"---", "Assert!"};
static const char *const talk_options[]    = {"---", "Talk!"};

_Static_assert(ARRAY_LEN(overlay_options) == OVERLAY_PAGE_COUNT, "overlay_options must match OverlayPage");
_Static_assert(ARRAY_LEN(bench_options) == BENCH_KIND_COUNT, "bench_options must match BenchKind");
_Static_assert(DBG_ITEM_COUNT <= MENU_MAX_ITEMS, "the Debug tab holds every DebugMenuItem");

#define DUMP_SETTLE_FRAMES 120   // ~2 s: under 2 % of the menu frames left in the averages

static Menu *dbg_menu = NULL;
static int   dbg_tab  = -1;

static OverlayPage overlay_page     = OVERLAY_OFF;
static bool        profiler_enabled = true;
static bool        rdp_check_active = false;   // validator state actually applied
static bool        dump_requested   = false;
static int         dump_countdown   = 0;      // closed-menu frames until the dump fires
static bool        reset_requested  = false;
static bool        dialog_requested = false;
static int         active_scene     = 0;      // scene currently shown (0 demo, 1 benchmark)
static bool        scene_requested  = false;
static int         requested_scene  = 0;

void debug_menu_init(Menu *menu, int tab) {
    dbg_menu = menu;
    dbg_tab  = tab;

    // Order must match DebugMenuItem
    menu_add_item(menu, tab, "Overlay",     overlay_options, ARRAY_LEN(overlay_options), OVERLAY_OFF);
    menu_add_item(menu, tab, "Profiler",    on_off_options,  ARRAY_LEN(on_off_options), 0);
    menu_add_item(menu, tab, "RDP Check",   off_on_options,  ARRAY_LEN(off_on_options), 0);
    menu_add_item(menu, tab, "Dump CSV",    dump_options,    ARRAY_LEN(dump_options), 0);
    menu_add_item(menu, tab, "Reset Peaks", reset_options,   ARRAY_LEN(reset_options), 0);
    menu_add_item(menu, tab, "Scene",       scene_options,   ARRAY_LEN(scene_options), 0);
    menu_add_item(menu, tab, "Bench",       bench_options,   ARRAY_LEN(bench_options), 0);
    menu_add_item(menu, tab, "RDP Log",     capture_options, ARRAY_LEN(capture_options), 0);
    menu_add_item(menu, tab, "Crash Test",  crash_options,   ARRAY_LEN(crash_options), 0);
    menu_add_item(menu, tab, "Reset Soak",  run_options,     ARRAY_LEN(run_options), 0);
    menu_add_item(menu, tab, "Menu Sweep",  run_options,     ARRAY_LEN(run_options), 0);
    menu_add_item(menu, tab, "Dialog",      talk_options,    ARRAY_LEN(talk_options), 0);

    // D-Up / D-Down shortcuts for player 1, below every game context
    action_push_context(0, &action_ctx_debug);

#if !ENGINE_DEBUG
    // Validator and profiler are compiled out of release builds
    menu_item_set_disabled(menu, tab, DBG_ITEM_RDP_CHECK, true);
    menu_item_set_disabled(menu, tab, DBG_ITEM_PROFILER, true);
    menu_item_set_disabled(menu, tab, DBG_ITEM_RDP_LOG, true);
    menu_item_set_disabled(menu, tab, DBG_ITEM_CRASH_TEST, true);
    profiler_enabled = false;
#endif
}

static int item_value(DebugMenuItem item) {
    return menu_get_value(dbg_menu, dbg_tab, item);
}

static void item_set(DebugMenuItem item, int value) {
    menu_set_value(dbg_menu, dbg_tab, item, value);
}

void debug_menu_update(void) {
    if (!dbg_menu || dbg_menu->is_open) return;   // apply only after the menu closes

    // Shortcuts: the debug context's actions (a context above that uses the
    // button, or a menu or dialog, takes it first)
    if (action_pressed(0, ACTION_DEBUG_OVERLAY)) {
        int next = (item_value(DBG_ITEM_OVERLAY) + 1) % OVERLAY_PAGE_COUNT;
        item_set(DBG_ITEM_OVERLAY, next);
    }
    if (action_pressed(0, ACTION_DEBUG_DUMP)) {
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
    // CSV dump: wait until the profiler's ~32-frame averages have settled
    // without the menu (it was just open to pick the item), so the rows show
    // the scene, not the menu. The countdown pauses while the menu is open.
    if (item_value(DBG_ITEM_DUMP_CSV) == 1) {
        item_set(DBG_ITEM_DUMP_CSV, 0);
        dump_countdown = DUMP_SETTLE_FRAMES;
        ENGINE_LOG("[debug] CSV dump in %d frames\n", DUMP_SETTLE_FRAMES);
    }
    if (dump_countdown > 0 && --dump_countdown == 0) {
        dump_requested = true;
        ENGINE_LOG("[debug] CSV dump requested\n");
    }
    int scene = item_value(DBG_ITEM_SCENE);
    if (scene != active_scene) {
        active_scene = scene;
        requested_scene = scene;
        scene_requested = true;
        ENGINE_LOG("[debug] scene switch requested: %s\n", scene_options[scene]);
    }

    if (item_value(DBG_ITEM_RDP_LOG) == 1) {
        item_set(DBG_ITEM_RDP_LOG, 0);
        rdp_debug_request_capture();
        ENGINE_LOG("[debug] one-frame RDP capture requested\n");
    }
    if (item_value(DBG_ITEM_RESET_SOAK) == 1) {
        item_set(DBG_ITEM_RESET_SOAK, 0);
        testbed_request_reset_soak();
        ENGINE_LOG("[debug] reset soak requested\n");
    }
    if (item_value(DBG_ITEM_MENU_SWEEP) == 1) {
        item_set(DBG_ITEM_MENU_SWEEP, 0);
        testbed_request_menu_sweep();
        ENGINE_LOG("[debug] menu sweep requested\n");
    }
    if (item_value(DBG_ITEM_DIALOG) == 1) {
        item_set(DBG_ITEM_DIALOG, 0);
        dialog_requested = true;
        ENGINE_LOG("[debug] dialog requested\n");
    }
    if (item_value(DBG_ITEM_CRASH_TEST) == 1) {
        item_set(DBG_ITEM_CRASH_TEST, 0);
        rdp_debug_crash_test();
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

bool debug_consume_scene_request(int *scene, int *bench_kind) {
    if (!scene_requested) return false;
    scene_requested = false;
    *scene = requested_scene;
    *bench_kind = item_value(DBG_ITEM_BENCH);
    return true;
}

void debug_menu_set_active_scene(int scene) {
    active_scene = scene;
    if (dbg_menu) item_set(DBG_ITEM_SCENE, scene);
}

bool debug_consume_dialog_request(void) {
    bool r = dialog_requested;
    dialog_requested = false;
    return r;
}

bool debug_consume_reset_peaks_request(void) {
    bool r = reset_requested;
    reset_requested = false;
    return r;
}
