#include "menu.h"
#include <string.h>

void menu_init(Menu *menu, const char *title) {
    memset(menu, 0, sizeof(Menu));
    menu->title = title;
}

int menu_add_tab(Menu *menu, const char *label) {
    if (menu->tab_count >= MENU_MAX_TABS) return -1;
    int idx = menu->tab_count++;
    MenuTab *tab = &menu->tabs[idx];
    memset(tab, 0, sizeof(MenuTab));
    tab->label = label;
    return idx;
}

int menu_add_item(Menu *menu, int tab, const char *label,
                  const char *const *options, int count, int default_idx) {
    if (tab < 0 || tab >= menu->tab_count) return -1;
    MenuTab *t = &menu->tabs[tab];
    if (t->item_count >= MENU_MAX_ITEMS) return -1;
    int idx = t->item_count++;
    MenuItem *item = &t->items[idx];
    item->label = label;
    item->option_count = (count > MENU_MAX_OPTIONS) ? MENU_MAX_OPTIONS : count;
    for (int i = 0; i < item->option_count; i++) {
        item->options[i] = options[i];
    }
    item->selected = default_idx;
    return idx;
}

// Find next enabled item in a given direction (1=down, -1=up).
// Returns current position if all items are disabled.
static int find_next_enabled(const MenuTab *tab, int from, int direction) {
    for (int i = 0; i < tab->item_count; i++) {
        int idx = (from + direction * (i + 1) + tab->item_count * tab->item_count) % tab->item_count;
        if (!tab->items[idx].disabled) return idx;
    }
    return from;
}

// Keep the cursor inside the visible window of MENU_VISIBLE_ITEMS rows
static void keep_visible(MenuTab *tab) {
    if (tab->item_count <= MENU_VISIBLE_ITEMS) {
        tab->scroll_offset = 0;
        return;
    }
    if (tab->cursor < tab->scroll_offset)
        tab->scroll_offset = tab->cursor;
    if (tab->cursor >= tab->scroll_offset + MENU_VISIBLE_ITEMS)
        tab->scroll_offset = tab->cursor - MENU_VISIBLE_ITEMS + 1;
}

void menu_open(Menu *menu) {
    // Snapshot current values for cancel/revert (all tabs)
    for (int t = 0; t < menu->tab_count; t++) {
        for (int i = 0; i < menu->tabs[t].item_count; i++) {
            menu->snapshot[t][i] = menu->tabs[t].items[i].selected;
        }
        menu->tabs[t].cursor = 0;
        menu->tabs[t].scroll_offset = 0;
        // Ensure cursor starts on an enabled item
        if (menu->tabs[t].item_count > 0 && menu->tabs[t].items[0].disabled) {
            menu->tabs[t].cursor = find_next_enabled(&menu->tabs[t], -1, 1);
        }
        keep_visible(&menu->tabs[t]);
    }
    menu->active_tab = 0;
    menu->is_open = true;
}

void menu_close(Menu *menu, bool apply) {
    if (!apply) {
        // Revert ALL tabs to snapshot
        for (int t = 0; t < menu->tab_count; t++) {
            for (int i = 0; i < menu->tabs[t].item_count; i++) {
                menu->tabs[t].items[i].selected = menu->snapshot[t][i];
            }
        }
    }
    menu->is_open = false;
}

void menu_move_cursor(Menu *menu, int dir) {
    if (menu->tab_count == 0) return;
    MenuTab *tab = &menu->tabs[menu->active_tab];
    if (tab->item_count == 0 || dir == 0) return;
    // Disabled items can be visited (so a long list scrolls to them and shows
    // why they are grey) but not changed
    int step = dir > 0 ? 1 : -1;
    tab->cursor = (tab->cursor + step + tab->item_count) % tab->item_count;
    keep_visible(tab);
}

void menu_change_value(Menu *menu, int dir) {
    if (menu->tab_count == 0) return;
    MenuTab *tab = &menu->tabs[menu->active_tab];
    if (tab->item_count == 0 || dir == 0) return;
    MenuItem *cur = &tab->items[tab->cursor];
    if (cur->disabled || cur->option_count <= 0) return;
    int step = dir > 0 ? 1 : -1;
    cur->selected = (cur->selected + step + cur->option_count) % cur->option_count;
}

void menu_switch_tab(Menu *menu, int dir) {
    if (menu->tab_count <= 1 || dir == 0) return;
    int step = dir > 0 ? 1 : -1;
    menu->active_tab = (menu->active_tab + step + menu->tab_count) % menu->tab_count;
    // Ensure cursor lands on an enabled item in the new tab
    MenuTab *tab = &menu->tabs[menu->active_tab];
    if (tab->item_count > 0 && tab->items[tab->cursor].disabled) {
        tab->cursor = find_next_enabled(tab, tab->cursor, 1);
    }
    keep_visible(tab);
}

void menu_update(Menu *menu, const UiInput *in) {
    if (!menu->is_open || menu->tab_count == 0 || !in) return;

    if (in->prev_tab) menu_switch_tab(menu, -1);
    if (in->next_tab) menu_switch_tab(menu, 1);
    if (in->up)    menu_move_cursor(menu, -1);
    if (in->down)  menu_move_cursor(menu, 1);
    if (in->left)  menu_change_value(menu, -1);
    if (in->right) menu_change_value(menu, 1);

    if (in->confirm) menu_close(menu, true);
    else if (in->cancel) menu_close(menu, false);
}

int menu_get_value(const Menu *menu, int tab, int item_index) {
    if (tab < 0 || tab >= menu->tab_count) return 0;
    if (item_index < 0 || item_index >= menu->tabs[tab].item_count) return 0;
    return menu->tabs[tab].items[item_index].selected;
}

void menu_set_value(Menu *menu, int tab, int item_index, int value) {
    if (tab < 0 || tab >= menu->tab_count) return;
    if (item_index < 0 || item_index >= menu->tabs[tab].item_count) return;
    MenuItem *it = &menu->tabs[tab].items[item_index];
    if (value < 0 || value >= it->option_count) return;
    it->selected = value;
}

void menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled) {
    if (tab < 0 || tab >= menu->tab_count) return;
    if (item < 0 || item >= menu->tabs[tab].item_count) return;
    menu->tabs[tab].items[item].disabled = disabled;
}
