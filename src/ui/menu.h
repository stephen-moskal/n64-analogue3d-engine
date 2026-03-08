#ifndef MENU_H
#define MENU_H

#include <libdragon.h>
#include <stdbool.h>

#define MENU_MAX_TABS    4
#define MENU_MAX_ITEMS   8
#define MENU_MAX_OPTIONS 12

typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];    // Right column choices
    int option_count;
    int selected;                            // Current option index
} MenuItem;

typedef struct {
    const char *label;                       // Tab header text (e.g., "Settings")
    MenuItem items[MENU_MAX_ITEMS];
    int item_count;
    int cursor;                              // Highlighted row within this tab
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

void menu_init(Menu *menu, const char *title);
int  menu_add_tab(Menu *menu, const char *label);
int  menu_add_item(Menu *menu, int tab, const char *label,
                   const char **options, int count, int default_idx);
void menu_open(Menu *menu);
void menu_close(Menu *menu, bool apply);
void menu_update(Menu *menu);
void menu_draw(const Menu *menu);
int  menu_get_value(const Menu *menu, int tab, int item_index);

#endif
