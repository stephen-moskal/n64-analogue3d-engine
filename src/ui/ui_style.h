#ifndef UI_STYLE_H
#define UI_STYLE_H

// UI styles (themes): the colours, fonts, layout metrics and decorations the
// UI widgets draw with. A widget keeps a pointer to its style; switching the
// pointer restyles it (the widget re-renders its cached text). Styles are
// plain data with static storage. See docs/UI.md.

#include <libdragon.h>

typedef struct UiStyle {
    const char *name;

    // Fonts (ids registered with rdpq_text, see text.h)
    uint8_t font_title;
    uint8_t font_body;

    // Text colours
    color_t title;             // menu title
    color_t header;            // tab header
    color_t text;              // normal rows
    color_t hilite;            // cursor row
    color_t disabled;          // greyed-out rows
    color_t footer;            // hints and scroll markers

    // Panel (alpha < 255 = translucent; alpha 0 = not drawn)
    color_t panel;
    color_t border;            // outer frame
    int16_t border_w;          // frame thickness in px (0 = none)
    color_t separator;         // line under the header
    color_t cursor_bar;        // bar behind the cursor row (alpha 0 = none)
    color_t scroll_track;      // scroll bar beside lists longer than the view
    color_t scroll_thumb;
    int16_t scroll_w;          // scroll bar width in px (0 = none)

    // Menu layout, screen px. Text y values are baselines.
    int16_t menu_x0, menu_x1;  // panel left / right edge
    int16_t pad;               // inner padding
    int16_t title_y, tab_y;    // title and tab header baselines
    int16_t sep_y;             // separator line
    int16_t items_y;           // baseline of the first row
    int16_t row_h;             // row pitch
    int16_t label_x;           // label column
    int16_t value_x;           // value column (value centred up to menu_x1 - pad)
    int16_t footer_pad;        // gap between the last row and the footer

    // Menu decorations (printf formats)
    const char *value_fmt;     // "%s" argument: the option
    const char *tab_fmt;       // args: label, tab number, tab count
    const char *tab_fmt_single;// arg: label
    const char *footer_multi;  // with several tabs
    const char *footer_single;
    const char *more;          // text scroll markers ("" = none; the scroll bar is enough)
} UiStyle;

// The engine's original look: translucent black box, yellow cursor row
extern const UiStyle ui_style_debug;

#endif
