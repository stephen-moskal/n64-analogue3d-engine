#ifndef MENU_VIEW_H
#define MENU_VIEW_H

// Draws a Menu in a UiStyle. The panel and lines are drawn every frame (a few
// rectangles); the text lives in a UiLayer and is re-rendered only for the
// rows whose content changed. The view compares what it last drew (tab,
// scroll, cursor, each visible row's value and state) with the model every
// frame, so values set directly or by menu_set_value() both show up.

#include "menu.h"
#include "ui_layer.h"
#include "ui_style.h"

typedef struct {
    const UiStyle *style;
    UiLayer layer;
    int16_t ox, oy;                        // layer position on screen

    // Slots
    int s_title, s_tab, s_up, s_down, s_footer;
    int s_label[MENU_VISIBLE_ITEMS], s_value[MENU_VISIBLE_ITEMS];
    int footer_rows;                       // visible row count the footer is placed for

    // What the text currently shows
    bool    valid;
    int16_t tab, scroll, cursor, count;
    int16_t sel[MENU_VISIBLE_ITEMS];
    bool    dis[MENU_VISIBLE_ITEMS];
} MenuView;

// cached = false draws every text element every frame (the pre-S5 cost; no
// surface memory). Cached views allocate their surface on first draw.
void menu_view_init(MenuView *view, const UiStyle *style, bool cached);
void menu_view_set_style(MenuView *view, const UiStyle *style);
void menu_view_free(MenuView *view);

// Draw the menu if it is open. Call after the scene, framebuffer attached.
void menu_draw(const Menu *menu, MenuView *view);

#endif
