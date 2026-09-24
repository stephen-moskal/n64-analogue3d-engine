#include "ui_style.h"
#include "text.h"

// Layout shared by the built-in styles (screen px; text y = baselines)
#define MENU_LAYOUT                                         \
    .menu_x0 = 30, .menu_x1 = 290, .pad = 10,               \
    .title_y = 50, .tab_y = 64, .sep_y = 72, .items_y = 86, \
    .row_h = 18, .label_x = 44, .value_x = 170, .footer_pad = 6

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
    .panel2        = RGBA32(0x00, 0x00, 0x00, 0xA0),
    .border        = RGBA32(0x00, 0x00, 0x00, 0x00),
    .border_w      = 0,
    .separator     = RGBA32(0x66, 0x66, 0x88, 0xFF),
    .cursor_bar    = RGBA32(0x00, 0x00, 0x00, 0x00),
    .scroll_track  = RGBA32(0x33, 0x33, 0x44, 0xFF),
    .scroll_thumb  = RGBA32(0x99, 0x99, 0xBB, 0xFF),
    .scroll_w      = 3,

    .hud_title     = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .hud_text      = RGBA32(0xC0, 0xC0, 0xFF, 0xFF),
    .hud_accent    = RGBA32(0x40, 0xFF, 0x40, 0xFF),
    .hud_shadow    = RGBA32(0x00, 0x00, 0x00, 0xFF),
    .hud_backdrop  = RGBA32(0x00, 0x00, 0x00, 0x00),
    .gauge_bg      = RGBA32(0x20, 0x20, 0x30, 0xC0),
    .gauge_fill    = RGBA32(0x40, 0xD0, 0x40, 0xFF),
    .gauge_warn    = RGBA32(0xF0, 0x40, 0x30, 0xFF),

    MENU_LAYOUT,

    .value_fmt      = "< %s >",
    .tab_fmt        = "< [%s] %d/%d >",
    .tab_fmt_single = "[%s]",
    .footer_multi   = "L/R:Tab  A:OK  B:Cancel",
    .footer_single  = "A:OK  B:Cancel",
    .more           = "",
};

const UiStyle ui_style_classic = {
    .name          = "Classic",
    .font_title    = FONT_UI_VAR,
    .font_body     = FONT_UI_MONO,

    .title         = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .header        = RGBA32(0xFF, 0xE0, 0x70, 0xFF),
    .text          = RGBA32(0xF0, 0xF0, 0xF0, 0xFF),
    .hilite        = RGBA32(0xFF, 0xE0, 0x70, 0xFF),
    .disabled      = RGBA32(0x70, 0x80, 0xA8, 0xFF),
    .footer        = RGBA32(0xB0, 0xC0, 0xE8, 0xFF),

    .panel         = RGBA32(0x18, 0x30, 0x98, 0xF0),
    .panel2        = RGBA32(0x04, 0x0C, 0x40, 0xF0),
    .border        = RGBA32(0xF0, 0xF0, 0xF0, 0xFF),
    .border_w      = 2,
    .separator     = RGBA32(0x90, 0xA8, 0xF0, 0xFF),
    .cursor_bar    = RGBA32(0xFF, 0xFF, 0xFF, 0x30),
    .scroll_track  = RGBA32(0x10, 0x20, 0x60, 0xFF),
    .scroll_thumb  = RGBA32(0xF0, 0xF0, 0xF0, 0xFF),
    .scroll_w      = 4,

    .hud_title     = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .hud_text      = RGBA32(0xF0, 0xF0, 0xF0, 0xFF),
    .hud_accent    = RGBA32(0xFF, 0xE0, 0x70, 0xFF),
    .hud_shadow    = RGBA32(0x00, 0x08, 0x30, 0xFF),
    .hud_backdrop  = RGBA32(0x08, 0x18, 0x60, 0xB0),
    .gauge_bg      = RGBA32(0x08, 0x10, 0x40, 0xFF),
    .gauge_fill    = RGBA32(0x60, 0xC0, 0xFF, 0xFF),
    .gauge_warn    = RGBA32(0xFF, 0x60, 0x40, 0xFF),

    MENU_LAYOUT,

    .value_fmt      = "< %s >",
    .tab_fmt        = "%s  %d/%d",
    .tab_fmt_single = "%s",
    .footer_multi   = "L/R Page  A Accept  B Back",
    .footer_single  = "A Accept  B Back",
    .more           = "",
};

const UiStyle ui_style_minimal = {
    .name          = "Minimal",
    .font_title    = FONT_UI_VAR,
    .font_body     = FONT_UI_MONO,

    .title         = RGBA32(0xE8, 0xDC, 0xC0, 0xFF),
    .header        = RGBA32(0xC0, 0xA0, 0x60, 0xFF),
    .text          = RGBA32(0x98, 0x90, 0x80, 0xFF),
    .hilite        = RGBA32(0xF4, 0xEA, 0xD0, 0xFF),
    .disabled      = RGBA32(0x50, 0x4A, 0x40, 0xFF),
    .footer        = RGBA32(0x78, 0x70, 0x60, 0xFF),

    .panel         = RGBA32(0x0C, 0x0A, 0x08, 0xE0),
    .panel2        = RGBA32(0x00, 0x00, 0x00, 0xF0),
    .border        = RGBA32(0x70, 0x5C, 0x38, 0xFF),
    .border_w      = 1,
    .separator     = RGBA32(0xC0, 0xA0, 0x60, 0xFF),
    .cursor_bar    = RGBA32(0xC0, 0xA0, 0x60, 0x30),
    .scroll_track  = RGBA32(0x28, 0x22, 0x18, 0xFF),
    .scroll_thumb  = RGBA32(0xC0, 0xA0, 0x60, 0xFF),
    .scroll_w      = 2,

    .hud_title     = RGBA32(0xE8, 0xDC, 0xC0, 0xFF),
    .hud_text      = RGBA32(0xB8, 0xB0, 0xA0, 0xFF),
    .hud_accent    = RGBA32(0xE0, 0xC0, 0x78, 0xFF),
    .hud_shadow    = RGBA32(0x00, 0x00, 0x00, 0xFF),
    .hud_backdrop  = RGBA32(0x00, 0x00, 0x00, 0x00),
    .gauge_bg      = RGBA32(0x18, 0x14, 0x10, 0xE0),
    .gauge_fill    = RGBA32(0xC0, 0xA0, 0x60, 0xFF),
    .gauge_warn    = RGBA32(0xB0, 0x30, 0x20, 0xFF),

    MENU_LAYOUT,

    .value_fmt      = "%s",
    .tab_fmt        = "%s",
    .tab_fmt_single = "%s",
    .footer_multi   = "L/R  Tab     A  Confirm     B  Back",
    .footer_single  = "A  Confirm     B  Back",
    .more           = "",
};

const UiStyle *const ui_styles[UI_STYLE_COUNT] = {
    &ui_style_debug, &ui_style_classic, &ui_style_minimal,
};
