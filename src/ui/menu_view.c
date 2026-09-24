#include "menu_view.h"
#include "ui_draw.h"
#include "../debug/stats.h"
#include <stdio.h>
#include <string.h>

// Row text box around a baseline (px above / below it). Boxes of adjacent
// rows and markers must not overlap: re-rendering one clears its box.
#define ASC   9
#define DESC  5

static int visible_rows(const MenuTab *tab) {
    return tab->item_count < MENU_VISIBLE_ITEMS ? tab->item_count : MENU_VISIBLE_ITEMS;
}

static int footer_y(const UiStyle *st, int rows) {
    return st->items_y + rows * st->row_h + st->footer_pad;
}

// Create the slots for the style's layout (screen -> layer coordinates)
static void layout(MenuView *v, int rows) {
    const UiStyle *st = v->style;
    UiLayer *L = &v->layer;
    ui_layer_clear_slots(L);
    int x0 = st->menu_x0, x1 = st->menu_x1, w = x1 - x0;
    int ox = v->ox, oy = v->oy;

    v->s_title = ui_layer_add(L, 0, st->title_y - 12 - oy, w, 12 + DESC, st->title_y - oy,
                              st->font_title, ALIGN_CENTER);
    v->s_tab = ui_layer_add(L, st->pad, st->tab_y - ASC - oy, w - 2 * st->pad, ASC + DESC,
                            st->tab_y - oy, st->font_body, ALIGN_CENTER);
    // Scroll markers: thin boxes between the separator and the first row,
    // and between the last visible row and the footer
    v->s_up = ui_layer_add(L, 0, st->sep_y + 1 - oy, w, 4, st->sep_y + 3 - oy,
                           st->font_body, ALIGN_CENTER);
    int down_ty = st->items_y + MENU_VISIBLE_ITEMS * st->row_h - 8;
    v->s_down = ui_layer_add(L, 0, down_ty - 3 - oy, w, 5, down_ty - oy,
                             st->font_body, ALIGN_CENTER);
    for (int r = 0; r < MENU_VISIBLE_ITEMS; r++) {
        int ty = st->items_y + r * st->row_h;
        v->s_label[r] = ui_layer_add(L, st->label_x - ox, ty - ASC - oy, st->value_x - st->label_x,
                                     ASC + DESC, ty - oy, st->font_body, ALIGN_LEFT);
        v->s_value[r] = ui_layer_add(L, st->value_x - ox, ty - ASC - oy,
                                     x1 - st->pad - st->value_x, ASC + DESC, ty - oy,
                                     st->font_body, ALIGN_CENTER);
    }
    int fy = footer_y(st, rows);
    v->s_footer = ui_layer_add(L, 0, fy - ASC - oy, w, ASC + DESC, fy - oy,
                               st->font_body, ALIGN_CENTER);
    v->footer_rows = rows;
    v->valid = false;
}

static void setup(MenuView *view, const UiStyle *style, bool cached) {
    view->style = style;
    view->ox = style->menu_x0;
    view->oy = style->title_y - 12;
    int h = footer_y(style, MENU_VISIBLE_ITEMS) + DESC + 1 - view->oy;
    ui_layer_init(&view->layer, style->menu_x1 - style->menu_x0, h, cached);
    layout(view, MENU_VISIBLE_ITEMS);
}

void menu_view_init(MenuView *view, const UiStyle *style, bool cached) {
    memset(view, 0, sizeof(*view));
    setup(view, style, cached);
}

void menu_view_set_style(MenuView *view, const UiStyle *style) {
    if (style == view->style) return;
    bool cached = view->layer.cached;
    ui_layer_free(&view->layer);                   // the size may change with the style
    setup(view, style, cached);
}

void menu_view_free(MenuView *view) {
    ui_layer_free(&view->layer);
    view->valid = false;
}

// Fill the slots from the model (only changed slots re-render)
static void refresh_text(const Menu *menu, MenuView *v, int rows) {
    const UiStyle *st = v->style;
    const MenuTab *tab = &menu->tabs[menu->active_tab];
    UiLayer *L = &v->layer;
    int start = tab->scroll_offset;

    ui_layer_set(L, v->s_title, st->title, menu->title);
    if (menu->tab_count > 1)
        ui_layer_setf(L, v->s_tab, st->header, st->tab_fmt, tab->label,
                      menu->active_tab + 1, menu->tab_count);
    else
        ui_layer_setf(L, v->s_tab, st->header, st->tab_fmt_single, tab->label);

    const char *more = st->more ? st->more : "";
    ui_layer_set(L, v->s_up, st->footer, start > 0 ? more : "");
    ui_layer_set(L, v->s_down, st->footer, start + rows < tab->item_count ? more : "");

    for (int r = 0; r < MENU_VISIBLE_ITEMS; r++) {
        int i = start + r;
        if (r >= rows) {
            ui_layer_set(L, v->s_label[r], st->text, "");
            ui_layer_set(L, v->s_value[r], st->text, "");
            continue;
        }
        const MenuItem *it = &tab->items[i];
        bool selected = (i == tab->cursor);
        color_t c = it->disabled ? st->disabled : (selected ? st->hilite : st->text);
        color_t lc = selected ? st->hilite : c;        // a visited disabled row: label lit, value grey
        const char *opt = (it->selected >= 0 && it->selected < it->option_count)
                          ? it->options[it->selected] : "?";
        ui_layer_set(L, v->s_label[r], lc, it->label);
        if (it->disabled) ui_layer_set(L, v->s_value[r], c, opt);
        else              ui_layer_setf(L, v->s_value[r], c, st->value_fmt, opt);
    }
    ui_layer_set(L, v->s_footer, st->footer,
                 menu->tab_count > 1 ? st->footer_multi : st->footer_single);
}

// True if the model shows something other than what the text was built from
static bool changed(const Menu *menu, MenuView *v, int rows) {
    const MenuTab *tab = &menu->tabs[menu->active_tab];
    bool diff = !v->valid || v->tab != menu->active_tab || v->scroll != tab->scroll_offset ||
                v->cursor != tab->cursor || v->count != tab->item_count;
    for (int r = 0; r < rows && !diff; r++) {
        const MenuItem *it = &tab->items[tab->scroll_offset + r];
        diff = v->sel[r] != it->selected || v->dis[r] != it->disabled;
    }
    if (!diff) return false;
    v->valid = true;
    v->tab = (int16_t)menu->active_tab;
    v->scroll = (int16_t)tab->scroll_offset;
    v->cursor = (int16_t)tab->cursor;
    v->count = (int16_t)tab->item_count;
    for (int r = 0; r < rows; r++) {
        const MenuItem *it = &tab->items[tab->scroll_offset + r];
        v->sel[r] = (int16_t)it->selected;
        v->dis[r] = it->disabled;
    }
    return true;
}

void menu_draw(const Menu *menu, MenuView *v) {
    if (!menu->is_open || menu->tab_count == 0) return;
    const UiStyle *st = v->style;
    const MenuTab *tab = &menu->tabs[menu->active_tab];
    int rows = visible_rows(tab);

    // The footer follows the last row: re-lay out when the row count changes
    if (rows != v->footer_rows) layout(v, rows);
    if (changed(menu, v, rows)) refresh_text(menu, v, rows);

    // Panel, frame, separator, cursor bar: a few rectangles every frame
    int fy = footer_y(st, rows);
    int y0 = st->title_y - st->pad;
    int y1 = fy + st->row_h;
    ui_rect(st->panel, st->menu_x0, y0, st->menu_x1, y1);
    ui_frame(st->border, st->border_w, st->menu_x0, y0, st->menu_x1, y1);
    ui_rect(st->separator, st->menu_x0 + st->pad, st->sep_y, st->menu_x1 - st->pad, st->sep_y + 1);
    if (st->cursor_bar.a && tab->item_count > 0) {
        int r = tab->cursor - tab->scroll_offset;
        if (r >= 0 && r < rows) {
            int ty = st->items_y + r * st->row_h;
            ui_rect(st->cursor_bar, st->menu_x0 + st->pad / 2, ty - ASC,
                    st->menu_x1 - st->pad / 2, ty + DESC);
        }
    }

    // Scroll bar: the track spans the visible rows, the thumb shows the
    // visible share of the tab and where it sits
    if (st->scroll_w > 0 && tab->item_count > rows && rows > 0) {
        int sx1 = st->menu_x1 - st->pad / 2, sx0 = sx1 - st->scroll_w;
        int ty0 = st->items_y - ASC, ty1 = st->items_y + (rows - 1) * st->row_h + DESC;
        int span = ty1 - ty0;
        int th = span * rows / tab->item_count;
        if (th < 4) th = 4;
        int t0 = ty0 + (span - th) * tab->scroll_offset / (tab->item_count - rows);
        ui_rect(st->scroll_track, sx0, ty0, sx1, ty1);
        ui_rect(st->scroll_thumb, sx0, t0, sx1, t0 + th);
    }

    ui_layer_draw(&v->layer, v->ox, v->oy, fy + DESC + 1 - v->oy);
}
