#include "ui_style.h"
#include "text.h"

const UiStyle ui_style_debug = {
    .name          = "Debug",
    .font_title    = FONT_UI_VAR,
    .font_body     = FONT_UI_MONO,

    .title         = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .header        = RGBA32(0xFF, 0xFF, 0x00, 0xFF),
    .text          = RGBA32(0xAA, 0xAA, 0xAA, 0xFF),
    .hilite        = RGBA32(0xFF, 0xFF, 0x00, 0xFF),
    .disabled      = RGBA32(0x55, 0x55, 0x55, 0xFF),
    .footer        = RGBA32(0x88, 0x88, 0x88, 0xFF),

    .panel         = RGBA32(0x00, 0x00, 0x00, 0xA0),
    .border        = RGBA32(0x00, 0x00, 0x00, 0x00),
    .border_w      = 0,
    .separator     = RGBA32(0x66, 0x66, 0x88, 0xFF),
    .cursor_bar    = RGBA32(0x00, 0x00, 0x00, 0x00),
    .scroll_track  = RGBA32(0x33, 0x33, 0x44, 0xFF),
    .scroll_thumb  = RGBA32(0x99, 0x99, 0xBB, 0xFF),
    .scroll_w      = 3,

    .menu_x0       = 30,
    .menu_x1       = 290,
    .pad           = 10,
    .title_y       = 50,
    .tab_y         = 64,
    .sep_y         = 72,
    .items_y       = 86,
    .row_h         = 18,
    .label_x       = 44,
    .value_x       = 170,
    .footer_pad    = 6,

    .value_fmt      = "< %s >",
    .tab_fmt        = "< [%s] %d/%d >",
    .tab_fmt_single = "[%s]",
    .footer_multi   = "L/R:Tab  A:OK  B:Cancel",
    .footer_single  = "A:OK  B:Cancel",
    .more           = "",
};
