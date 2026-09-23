#include "testbed.h"
#include <libdragon.h>
#include <stdio.h>
#include <string.h>
#include "engine_debug.h"
#include "memstats.h"

#define SOAK_RESETS      10
#define SOAK_WAIT_FRAMES 30
#define SWEEP_HOLD       15

static SceneManager *tb_mgr;
static Menu         *tb_menu;
static char          status_buf[48];

// --- Reset soak ---
static bool soak_requested, soak_active;
static int  soak_left, soak_wait, soak_heap_before;

// --- Menu sweep ---
static bool sweep_requested, sweep_active;
static int  sw_tab, sw_item, sw_opt, sw_hold;
static int  sw_items, sw_options;
static int  sw_snapshot[MENU_MAX_TABS][MENU_MAX_ITEMS];

void testbed_init(SceneManager *mgr, Menu *menu) {
    tb_mgr = mgr;
    tb_menu = menu;
}

void testbed_request_reset_soak(void) { soak_requested = true; }
void testbed_request_menu_sweep(void) { sweep_requested = true; }
bool testbed_busy(void) { return soak_active || sweep_active; }

const char *testbed_status(void) {
    if (soak_active) {
        snprintf(status_buf, sizeof(status_buf), "RESET SOAK %d/%d",
                 SOAK_RESETS - soak_left, SOAK_RESETS);
        return status_buf;
    }
    if (sweep_active) {
        const MenuTab *t = &tb_menu->tabs[sw_tab];
        snprintf(status_buf, sizeof(status_buf), "SWEEP %s: %s %d",
                 t->label, t->items[sw_item].label, sw_opt);
        return status_buf;
    }
    return NULL;
}

static int heap_now(void) {
    heap_stats_t hs;
    sys_get_heap_stats(&hs);
    return hs.used;
}

// Tabs/items the sweep must not touch
static bool sweep_skip_tab(int t) {
    const char *l = tb_menu->tabs[t].label;
    return !strcmp(l, "Controls") || !strcmp(l, "Debug");
}
static bool sweep_skip_item(int t, int i) {
    return !strcmp(tb_menu->tabs[t].items[i].label, "Reset Scene");
}

// Advance to the next sweepable (tab, item), starting at the current one
static bool sweep_seek(void) {
    for (; sw_tab < tb_menu->tab_count; sw_tab++, sw_item = 0) {
        if (sweep_skip_tab(sw_tab)) continue;
        for (; sw_item < tb_menu->tabs[sw_tab].item_count; sw_item++) {
            if (!sweep_skip_item(sw_tab, sw_item)) return true;
        }
    }
    return false;
}

static void soak_update(void) {
    if (soak_requested && !soak_active && !sweep_active) {
        soak_requested = false;
        soak_active = true;
        soak_left = SOAK_RESETS + 1;          // +1 warm-up reset, not measured
        soak_wait = 0;
        soak_heap_before = -1;
        debugf("SOAK,start,resets=%d\n", SOAK_RESETS);
    }
    if (!soak_active) return;

    Scene *cur = scene_manager_current(tb_mgr);
    if (!cur || scene_manager_is_transitioning(tb_mgr)) return;
    if (soak_wait > 0) { soak_wait--; return; }

    if (soak_heap_before < 0 && soak_left == SOAK_RESETS) {
        soak_heap_before = heap_now();        // after the warm-up reset settled
    }
    if (soak_left > 0) {
        cur->reset_requested = true;
        soak_left--;
        soak_wait = SOAK_WAIT_FRAMES;
        return;
    }
    int after = heap_now();
    (void)after;   // only used by debugf (compiled out in release)
    debugf("SOAK,resets=%d,heap_before=%d,heap_after=%d,delta=%d,per_reset=%d\n",
           SOAK_RESETS, soak_heap_before, after, after - soak_heap_before,
           (after - soak_heap_before) / SOAK_RESETS);
    memstats_reset_baseline();
    soak_active = false;
}

static void sweep_update(void) {
    if (sweep_requested && !sweep_active && !soak_active) {
        sweep_requested = false;
        for (int t = 0; t < tb_menu->tab_count; t++)
            for (int i = 0; i < tb_menu->tabs[t].item_count; i++)
                sw_snapshot[t][i] = tb_menu->tabs[t].items[i].selected;
        sw_tab = sw_item = sw_opt = sw_hold = 0;
        sw_items = sw_options = 0;
        if (!sweep_seek()) return;
        sweep_active = true;
        debugf("SWEEP,start\n");
        debugf("SWEEP,%s,%s,%d\n", tb_menu->tabs[sw_tab].label,
               tb_menu->tabs[sw_tab].items[sw_item].label,
               tb_menu->tabs[sw_tab].items[sw_item].option_count);
    }
    if (!sweep_active || tb_menu->is_open) return;   // pause while the menu is open

    MenuItem *it = &tb_menu->tabs[sw_tab].items[sw_item];
    if (sw_hold > 0) { sw_hold--; return; }

    if (sw_opt < it->option_count) {
        it->selected = sw_opt++;
        sw_options++;
        sw_hold = SWEEP_HOLD;
        return;
    }

    // Item done: restore it and move on
    it->selected = sw_snapshot[sw_tab][sw_item];
    sw_items++;
    sw_item++;
    sw_opt = 0;
    sw_hold = SWEEP_HOLD;
    if (sweep_seek()) {
        debugf("SWEEP,%s,%s,%d\n", tb_menu->tabs[sw_tab].label,
               tb_menu->tabs[sw_tab].items[sw_item].label,
               tb_menu->tabs[sw_tab].items[sw_item].option_count);
        return;
    }

    for (int t = 0; t < tb_menu->tab_count; t++)
        for (int i = 0; i < tb_menu->tabs[t].item_count; i++)
            tb_menu->tabs[t].items[i].selected = sw_snapshot[t][i];
    debugf("SWEEP,END,items=%d,options=%d\n", sw_items, sw_options);
    sweep_active = false;
}

void testbed_update(void) {
    if (!tb_mgr || !tb_menu) return;
    soak_update();
    sweep_update();
}
