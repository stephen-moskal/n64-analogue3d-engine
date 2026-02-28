#ifndef MENU_H
#define MENU_H

#include <libdragon.h>
#include <stdbool.h>

#define MENU_MAX_ITEMS   8
#define MENU_MAX_OPTIONS 8

typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];    // Right column choices
    int option_count;
    int selected;                            // Current option index
} MenuItem;

typedef struct {
    const char *title;
    MenuItem items[MENU_MAX_ITEMS];
    int item_count;
    int cursor;                              // Highlighted row
    bool is_open;
    int snapshot[MENU_MAX_ITEMS];            // Saved values for cancel/revert
    int analog_cooldown;                     // Frame counter for analog repeat
} Menu;

void menu_init(Menu *menu, const char *title);
int  menu_add_item(Menu *menu, const char *label,
                   const char **options, int count, int default_idx);
void menu_open(Menu *menu);
void menu_close(Menu *menu, bool apply);
void menu_update(Menu *menu);
void menu_draw(const Menu *menu);
int  menu_get_value(const Menu *menu, int item_index);

#endif
