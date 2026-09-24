#ifndef MENU_H
#define MENU_H

// Tabbed option menu: the model (tabs, items, values, cursor, cancel/revert)
// and its joypad input. Drawing lives in menu_view.h, so one model can be
// shown in any style and the model is testable on the host. See docs/UI.md.

#include <stdbool.h>

#define MENU_MAX_TABS        6
#define MENU_MAX_ITEMS      12
#define MENU_MAX_OPTIONS    16
#define MENU_VISIBLE_ITEMS   7

typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];    // Right column choices
    int option_count;
    int selected;                            // Current option index
    bool disabled;                           // Greyed out, cursor skips over
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
    int analog_cooldown;                     // Frame counter for analog repeat
} Menu;

// Building
void menu_init(Menu *menu, const char *title);
int  menu_add_tab(Menu *menu, const char *label);
int  menu_add_item(Menu *menu, int tab, const char *label,
                   const char **options, int count, int default_idx);

// Open (snapshot values) and close (apply, or revert to the snapshot)
void menu_open(Menu *menu);
void menu_close(Menu *menu, bool apply);

// Joypad: D-pad/stick, L/R tabs, A apply, B cancel. Port 1, already polled.
void menu_update(Menu *menu);

// Model operations (what menu_update does; also for scripted input and tests)
void menu_move_cursor(Menu *menu, int dir);   // wraps, scrolls; disabled items can be visited
void menu_change_value(Menu *menu, int dir);  // the cursor item, wraps
void menu_switch_tab(Menu *menu, int dir);    // wraps

// Values
int  menu_get_value(const Menu *menu, int tab, int item_index);
void menu_set_value(Menu *menu, int tab, int item_index, int value);
void menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled);

#endif
