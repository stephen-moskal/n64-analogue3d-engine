#ifndef MENU_H
#define MENU_H

// Tabbed option menu: the model (tabs, items, values, cursor, cancel/revert)
// and what a frame of UI input does to it. Drawing lives in menu_view.h, so
// one model can be shown in any style, and the model is testable on the
// host. See docs/UI.md.

#include <stdbool.h>
#include "ui_input.h"

#define MENU_MAX_TABS        6
#define MENU_MAX_ITEMS      12
#define MENU_MAX_OPTIONS    16
#define MENU_VISIBLE_ITEMS   7

typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];    // Right column choices
    int option_count;
    int selected;                            // Current option index
    bool disabled;                           // Greyed out: the cursor stops on it, the value can't change
} MenuItem;

typedef struct {
    const char *label;                       // Tab header text (e.g., "Settings")
    MenuItem items[MENU_MAX_ITEMS];
    int item_count;
    int cursor;                              // Highlighted row within this tab
    int scroll_offset;                       // First visible item index
} MenuTab;

typedef struct {
    const char *title;
    MenuTab tabs[MENU_MAX_TABS];
    int tab_count;
    int active_tab;                          // Currently visible tab
    bool is_open;
    int snapshot[MENU_MAX_TABS][MENU_MAX_ITEMS]; // Saved values for cancel/revert
} Menu;

// Building
void menu_init(Menu *menu, const char *title);
int  menu_add_tab(Menu *menu, const char *label);
int  menu_add_item(Menu *menu, int tab, const char *label,
                   const char *const *options, int count, int default_idx);

// Open (snapshot values) and close (apply, or revert to the snapshot)
void menu_open(Menu *menu);
void menu_close(Menu *menu, bool apply);

// One frame of UI input (action_ui() for the player driving the menu): up and
// down move the cursor, left and right change the value, the tabs switch
// tabs, confirm closes and applies, cancel closes and reverts. Start is left
// to the caller (the demo closes and applies).
void menu_update(Menu *menu, const UiInput *in);

// Model operations (what menu_update does; also for scripted input and tests)
void menu_move_cursor(Menu *menu, int dir);   // wraps, scrolls; disabled items can be visited
void menu_change_value(Menu *menu, int dir);  // the cursor item, wraps
void menu_switch_tab(Menu *menu, int dir);    // wraps

// Values
int  menu_get_value(const Menu *menu, int tab, int item_index);
void menu_set_value(Menu *menu, int tab, int item_index, int value);
void menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled);

#endif
