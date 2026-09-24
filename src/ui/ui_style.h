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
    uint8_t font_dialog;       // text box body and speaker name

    // Text colours
    color_t title;             // menu title
    color_t header;            // tab header
    color_t text;              // normal rows
    color_t hilite;            // cursor row
    color_t disabled;          // greyed-out rows
    color_t footer;            // hints and scroll markers

    // Panel (alpha < 255 = translucent; alpha 0 = not drawn). panel2 is the
    // bottom colour of a vertical gradient (the same as panel for flat).
    color_t panel;
    color_t panel2;
    color_t border;            // outer frame
    int16_t border_w;          // frame thickness in px (0 = none)
    color_t separator;         // line under the header
    color_t cursor_bar;        // bar behind the cursor row (alpha 0 = none)
    color_t scroll_track;      // scroll bar beside lists longer than the view
    color_t scroll_thumb;
    int16_t scroll_w;          // scroll bar width in px (0 = none)

    // HUD (ui_hud.h)
    color_t hud_title;         // headline text
    color_t hud_text;          // normal readouts
    color_t hud_accent;        // highlighted readouts
    color_t hud_shadow;        // drop shadow behind HUD text (alpha 0 = none)
    color_t hud_backdrop;      // box behind each HUD panel (alpha 0 = none)
    color_t gauge_bg, gauge_fill, gauge_warn;   // gauges; warn above 90 %

    // Text box (ui/textbox.h). Dialog colours: [c=text] dialog_text,
    // [c=accent] hud_accent, [c=hilite] hilite, [c=title] title, [c=dim] disabled.
    color_t dialog_text;
    int16_t tb_x0, tb_y0, tb_x1, tb_y1;   // the box on screen
    int16_t tb_pad;                        // inner padding
    int16_t tb_lines;                      // text lines per page
    int16_t tb_cps;                        // typewriter speed, glyphs per second

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

// Built-in styles
extern const UiStyle ui_style_debug;     // the original look: translucent black box, yellow cursor
extern const UiStyle ui_style_classic;   // classic RPG window: blue gradient, white frame
extern const UiStyle ui_style_minimal;   // dark and quiet: near-black panel, thin gold accents

// All built-in styles, for a style picker (UI_STYLE_COUNT entries)
#define UI_STYLE_COUNT 3
extern const UiStyle *const ui_styles[UI_STYLE_COUNT];

#endif
