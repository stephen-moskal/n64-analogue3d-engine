#include "demo_tour.h"
#include <libdragon.h>
#include "../ui/settings.h"
#include "../debug/debug_menu.h"
#include "../debug/engine_debug.h"
#include "benchmark_scene.h"

#if defined(ENGINE_TOUR) && ENGINE_TOUR          // make TOUR=1 only

static SceneManager *mgr;
static Menu         *menu;
static int           dbg_tab;
static int           step = -1;
static uint32_t      step_start;

// The demo's camera (orbital): angle around the target, height, distance
static void camera_at(float azimuth, float elevation, float distance) {
    Scene *s = scene_manager_current(mgr);
    if (!s) return;
    s->camera.azimuth = azimuth;
    s->camera.elevation = elevation;
    s->camera.distance = distance;
    s->camera.dirty = true;
}

static void debug_item(DebugMenuItem item, int value) {
    menu_set_value(menu, dbg_tab, item, value);
}

// Atmosphere choices: Custom, Clear Day, Overcast, Foggy, Dense Fog, Sunset, Dusk, Night
enum { ATM_CUSTOM, ATM_CLEAR_DAY, ATM_OVERCAST, ATM_FOGGY, ATM_DENSE_FOG, ATM_SUNSET, ATM_DUSK, ATM_NIGHT };

static void hero(void) {
    settings_set_choice(SETTING_ATMOSPHERE, ATM_CLEAR_DAY);
    settings_set_choice(SETTING_SHADOWS, 2);                  // Projected
    camera_at(0.0f, 0.32f, 850.0f);
}

static void menu_settings(void) {
    menu_open(menu);                                         // Settings tab
    for (int i = 0; i < SETTING_LATENCY - SETTING_BG_COLOR; i++)
        menu_move_cursor(menu, 1);                           // on Latency (scrolled)
    camera_at(0.35f, 0.30f, 700.0f);
}

static void menu_lighting_classic(void) {
    settings_set_choice(SETTING_UI_STYLE, 1);                 // Classic
    menu->active_tab = SETTINGS_TAB_LIGHTING;
}

static void sunset(void) {
    menu_close(menu, true);
    settings_set_choice(SETTING_UI_STYLE, 0);                 // Debug
    settings_set_choice(SETTING_ATMOSPHERE, ATM_SUNSET);
    camera_at(0.75f, 0.22f, 800.0f);
}

static void night(void) {
    settings_set_choice(SETTING_ATMOSPHERE, ATM_NIGHT);
    settings_set_choice(SETTING_POINT_LIGHTS, 1);             // On: torches on the pillars
    settings_set_choice(SETTING_POINT_INTENSITY, 6);          // 8.0
    settings_set_choice(SETTING_POINT_RADIUS, 4);             // 600
    camera_at(0.12f, 0.26f, 640.0f);
}

static void profiler_page(void) {
    settings_set_choice(SETTING_ATMOSPHERE, ATM_CLEAR_DAY);
    settings_set_choice(SETTING_POINT_LIGHTS, 0);
    settings_set_choice(SETTING_DEBUG_TEXT, 1);               // HUD off: the page is tall
    camera_at(0.0f, 0.32f, 850.0f);
    debug_item(DBG_ITEM_OVERLAY, OVERLAY_PROFILER);
}

static void frame_page(void) {
    settings_set_choice(SETTING_DEBUG_TEXT, 0);               // HUD on
    debug_item(DBG_ITEM_OVERLAY, OVERLAY_FRAMETIME);
}

static void input_page(void) {
    debug_item(DBG_ITEM_OVERLAY, OVERLAY_INPUT);
}

static void dialog(void) {
    debug_item(DBG_ITEM_OVERLAY, OVERLAY_OFF);
    settings_set_choice(SETTING_ATMOSPHERE, ATM_OVERCAST);
    camera_at(0.2f, 0.25f, 600.0f);
    debug_item(DBG_ITEM_DIALOG, 1);                           // the demo opens "intro"
}

static void benchmark(void) {
    debug_item(DBG_ITEM_BENCH, BENCH_OBJECTS);                // 8 .. 64 pillars, 5 s each
    debug_item(DBG_ITEM_SCENE, 1);
}

static void done(void) { }

static const struct {
    const char *name;
    float       seconds;
    void      (*enter)(void);
} steps[] = {
    {"hero",           6.0f, hero},
    {"menu_settings",  5.0f, menu_settings},
    {"menu_classic",   5.0f, menu_lighting_classic},
    {"sunset",         5.0f, sunset},
    {"night",          5.0f, night},
    {"profiler",       5.0f, profiler_page},
    {"frame",          5.0f, frame_page},
    {"input",          5.0f, input_page},
    {"dialog",         7.0f, dialog},
    {"benchmark",     35.0f, benchmark},     // the 64-pillar step runs ~25-30 s in
    {"end",            1.0f, done},
};

void demo_tour_init(SceneManager *scenes, Menu *m, int debug_tab) {
    mgr = scenes;
    menu = m;
    dbg_tab = debug_tab;
    step = -1;
}

void demo_tour_frame(void) {
    if (!mgr || step >= (int)(sizeof(steps) / sizeof(steps[0]))) return;
    uint32_t now = TICKS_READ();
    if (step >= 0 && TICKS_DISTANCE(step_start, now) < (int32_t)(steps[step].seconds * TICKS_PER_SECOND))
        return;
    if (++step >= (int)(sizeof(steps) / sizeof(steps[0]))) return;
    step_start = now;
    steps[step].enter();
    debugf("TOUR,%d,%s,%.1f\n", step, steps[step].name, (float)get_ticks() / TICKS_PER_SECOND);
}

#endif
