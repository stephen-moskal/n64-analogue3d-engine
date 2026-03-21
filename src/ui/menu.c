#include "menu.h"
#include "text.h"
#include <string.h>

// Layout constants
#define MENU_X0          30
#define MENU_X1          290
#define MENU_PAD         10
#define MENU_TITLE_Y     50
#define MENU_TAB_Y       64
#define MENU_SEP_Y       72
#define MENU_ITEMS_Y     86
#define MENU_ROW_HEIGHT  18
#define MENU_LABEL_X     44
#define MENU_VALUE_X     170
#define MENU_FOOTER_PAD  6

// Analog stick menu threshold and repeat delay
#define ANALOG_THRESHOLD 40
#define ANALOG_COOLDOWN  10

// Colors
#define COLOR_TITLE    RGBA32(0xFF, 0xFF, 0xFF, 0xFF)
#define COLOR_SELECTED RGBA32(0xFF, 0xFF, 0x00, 0xFF)
#define COLOR_NORMAL   RGBA32(0xAA, 0xAA, 0xAA, 0xFF)
#define COLOR_TAB_ACT  RGBA32(0xFF, 0xFF, 0x00, 0xFF)
#define COLOR_TAB_IDLE RGBA32(0x88, 0x88, 0x88, 0xFF)
#define COLOR_FOOTER   RGBA32(0x88, 0x88, 0x88, 0xFF)
#define COLOR_SEP      RGBA32(0x66, 0x66, 0x88, 0xFF)
#define COLOR_DISABLED RGBA32(0x55, 0x55, 0x55, 0xFF)

// Forward declaration
static int find_next_enabled(const MenuTab *tab, int from, int direction);

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
                  const char **options, int count, int default_idx) {
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
    }
    menu->active_tab = 0;
    menu->analog_cooldown = 0;
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

// Find next enabled item in a given direction (1=down, -1=up).
// Returns current position if all items are disabled.
static int find_next_enabled(const MenuTab *tab, int from, int direction) {
    for (int i = 0; i < tab->item_count; i++) {
        int idx = (from + direction * (i + 1) + tab->item_count * tab->item_count) % tab->item_count;
        if (!tab->items[idx].disabled) return idx;
    }
    return from;
}

void menu_update(Menu *menu) {
    if (!menu->is_open || menu->tab_count == 0) return;

    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    // L/R: switch tabs
    if (pressed.l && menu->tab_count > 1) {
        menu->active_tab = (menu->active_tab - 1 + menu->tab_count) % menu->tab_count;
        // Ensure cursor lands on an enabled item in the new tab
        MenuTab *new_tab = &menu->tabs[menu->active_tab];
        if (new_tab->item_count > 0 && new_tab->items[new_tab->cursor].disabled) {
            new_tab->cursor = find_next_enabled(new_tab, new_tab->cursor, 1);
        }
    }
    if (pressed.r && menu->tab_count > 1) {
        menu->active_tab = (menu->active_tab + 1) % menu->tab_count;
        MenuTab *new_tab = &menu->tabs[menu->active_tab];
        if (new_tab->item_count > 0 && new_tab->items[new_tab->cursor].disabled) {
            new_tab->cursor = find_next_enabled(new_tab, new_tab->cursor, 1);
        }
    }

    MenuTab *tab = &menu->tabs[menu->active_tab];
    if (tab->item_count == 0) goto check_close;

    // D-pad up/down: move cursor, skip disabled items
    if (pressed.d_up) {
        tab->cursor = find_next_enabled(tab, tab->cursor, -1);
    }
    if (pressed.d_down) {
        tab->cursor = find_next_enabled(tab, tab->cursor, 1);
    }

    // Keep cursor in visible scroll window
    if (tab->item_count > MENU_VISIBLE_ITEMS) {
        if (tab->cursor < tab->scroll_offset)
            tab->scroll_offset = tab->cursor;
        if (tab->cursor >= tab->scroll_offset + MENU_VISIBLE_ITEMS)
            tab->scroll_offset = tab->cursor - MENU_VISIBLE_ITEMS + 1;
    }

    // D-pad left/right: cycle option (skip if disabled)
    MenuItem *cur = &tab->items[tab->cursor];
    if (!cur->disabled) {
        if (pressed.d_left) {
            cur->selected = (cur->selected - 1 + cur->option_count) % cur->option_count;
        }
        if (pressed.d_right) {
            cur->selected = (cur->selected + 1) % cur->option_count;
        }

        // Analog stick left/right with cooldown
        if (menu->analog_cooldown > 0) {
            menu->analog_cooldown--;
        } else {
            joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
            if (inputs.stick_x < -ANALOG_THRESHOLD) {
                cur->selected = (cur->selected - 1 + cur->option_count) % cur->option_count;
                menu->analog_cooldown = ANALOG_COOLDOWN;
            } else if (inputs.stick_x > ANALOG_THRESHOLD) {
                cur->selected = (cur->selected + 1) % cur->option_count;
                menu->analog_cooldown = ANALOG_COOLDOWN;
            }
        }
    } else {
        // Still decrement cooldown even when disabled
        if (menu->analog_cooldown > 0) menu->analog_cooldown--;
    }

check_close:
    // A: confirm and close
    if (pressed.a) {
        menu_close(menu, true);
    }
    // B: cancel and close
    if (pressed.b) {
        menu_close(menu, false);
    }
}

static void draw_bg(int y0, int y1) {
    // Semi-transparent black overlay using standard mode + alpha blending
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0x00, 0x00, 0x00, 0xA0));

    // Two triangles forming the background quad
    float tl[2] = {MENU_X0, y0};
    float tr[2] = {MENU_X1, y0};
    float bl[2] = {MENU_X0, y1};
    float br[2] = {MENU_X1, y1};

    rdpq_triangle(&TRIFMT_FILL, tl, br, bl);
    rdpq_triangle(&TRIFMT_FILL, tl, tr, br);
}

void menu_draw(const Menu *menu) {
    if (!menu->is_open || menu->tab_count == 0) return;

    const MenuTab *tab = &menu->tabs[menu->active_tab];

    // Scrollable item range
    int visible = tab->item_count < MENU_VISIBLE_ITEMS ? tab->item_count : MENU_VISIBLE_ITEMS;
    int start = tab->scroll_offset;
    int end = start + visible;
    if (end > tab->item_count) end = tab->item_count;
    bool scroll_up = (start > 0);
    bool scroll_down = (end < tab->item_count);

    // Compute menu height based on visible item count
    int footer_y = MENU_ITEMS_Y + visible * MENU_ROW_HEIGHT + MENU_FOOTER_PAD;
    int menu_y0 = MENU_TITLE_Y - MENU_PAD;
    int menu_y1 = footer_y + MENU_ROW_HEIGHT;

    // Draw semi-transparent background
    draw_bg(menu_y0, menu_y1);

    // Title
    TextBoxConfig title_cfg = {
        .x       = MENU_X0,
        .y       = MENU_TITLE_Y,
        .width   = MENU_X1 - MENU_X0,
        .font_id = FONT_DEBUG_VAR,
        .color   = COLOR_TITLE,
        .align   = ALIGN_CENTER,
    };
    text_draw(&title_cfg, menu->title);

    // Tab headers
    int tab_width = (MENU_X1 - MENU_X0 - 2 * MENU_PAD) / menu->tab_count;
    for (int t = 0; t < menu->tab_count; t++) {
        bool is_active = (t == menu->active_tab);
        TextBoxConfig tab_cfg = {
            .x       = MENU_X0 + MENU_PAD + t * tab_width,
            .y       = MENU_TAB_Y,
            .width   = tab_width,
            .font_id = FONT_DEBUG_MONO,
            .color   = is_active ? COLOR_TAB_ACT : COLOR_TAB_IDLE,
            .align   = ALIGN_CENTER,
        };
        if (is_active) {
            text_draw_fmt(&tab_cfg, "[%s]", menu->tabs[t].label);
        } else {
            text_draw(&tab_cfg, menu->tabs[t].label);
        }
    }

    // Separator line
    rdpq_set_mode_fill(COLOR_SEP);
    rdpq_fill_rectangle(MENU_X0 + MENU_PAD, MENU_SEP_Y,
                        MENU_X1 - MENU_PAD, MENU_SEP_Y + 1);

    // Scroll-up indicator
    if (scroll_up) {
        TextBoxConfig up_cfg = {
            .x       = MENU_X0,
            .y       = MENU_SEP_Y + 3,
            .width   = MENU_X1 - MENU_X0,
            .font_id = FONT_DEBUG_MONO,
            .color   = COLOR_FOOTER,
            .align   = ALIGN_CENTER,
        };
        text_draw(&up_cfg, "...");
    }

    // Menu items for active tab (scrollable window)
    for (int i = start; i < end; i++) {
        float row_y = MENU_ITEMS_Y + (i - start) * MENU_ROW_HEIGHT;
        bool is_disabled = tab->items[i].disabled;
        bool selected = (i == tab->cursor) && !is_disabled;
        color_t color = is_disabled ? COLOR_DISABLED :
                        (selected ? COLOR_SELECTED : COLOR_NORMAL);

        // Label (left column)
        TextBoxConfig label_cfg = {
            .x       = MENU_LABEL_X,
            .y       = row_y,
            .font_id = FONT_DEBUG_MONO,
            .color   = color,
        };
        text_draw(&label_cfg, tab->items[i].label);

        // Value (right column) — no arrows if disabled
        TextBoxConfig value_cfg = {
            .x       = MENU_VALUE_X,
            .y       = row_y,
            .width   = MENU_X1 - MENU_VALUE_X - MENU_PAD,
            .font_id = FONT_DEBUG_MONO,
            .color   = color,
            .align   = ALIGN_CENTER,
        };
        const char *opt = tab->items[i].options[tab->items[i].selected];
        if (is_disabled) {
            text_draw(&value_cfg, opt);
        } else {
            text_draw_fmt(&value_cfg, "< %s >", opt);
        }
    }

    // Scroll-down indicator
    if (scroll_down) {
        float ind_y = MENU_ITEMS_Y + visible * MENU_ROW_HEIGHT - 8;
        TextBoxConfig down_cfg = {
            .x       = MENU_X0,
            .y       = ind_y,
            .width   = MENU_X1 - MENU_X0,
            .font_id = FONT_DEBUG_MONO,
            .color   = COLOR_FOOTER,
            .align   = ALIGN_CENTER,
        };
        text_draw(&down_cfg, "...");
    }

    // Footer hint
    TextBoxConfig footer_cfg = {
        .x       = MENU_X0,
        .y       = footer_y,
        .width   = MENU_X1 - MENU_X0,
        .font_id = FONT_DEBUG_MONO,
        .color   = COLOR_FOOTER,
        .align   = ALIGN_CENTER,
    };
    if (menu->tab_count > 1) {
        text_draw(&footer_cfg, "L/R:Tab  A:OK  B:Cancel");
    } else {
        text_draw(&footer_cfg, "A:OK  B:Cancel");
    }
}

int menu_get_value(const Menu *menu, int tab, int item_index) {
    if (tab < 0 || tab >= menu->tab_count) return 0;
    if (item_index < 0 || item_index >= menu->tabs[tab].item_count) return 0;
    return menu->tabs[tab].items[item_index].selected;
}

void menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled) {
    if (tab < 0 || tab >= menu->tab_count) return;
    if (item < 0 || item >= menu->tabs[tab].item_count) return;
    menu->tabs[tab].items[item].disabled = disabled;
}
