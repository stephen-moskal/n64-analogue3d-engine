#include "text.h"
#include <stdarg.h>

// display_init() uses FILTERS_RESAMPLE (no VI anti-aliasing), so rdpq_text's
// default "AA fix" (a transparent rectangle drawn behind every print, with its
// own mode change) protects against nothing: turn it off for every call. Set
// to 1 if the display ever enables FILTERS_RESAMPLE_ANTIALIAS*.
#define TEXT_AA_FIX 0

static rdpq_font_t *font_mono = NULL;
static rdpq_font_t *font_var = NULL;
static rdpq_font_t *font_ui_mono = NULL;
static rdpq_font_t *font_ui_var = NULL;

// Style 1 is rewritten per call with the call's colour; remember the last
// colour per font so an unchanged colour skips the write
#define TEXT_MAX_FONTS 8
static uint32_t style1_color[TEXT_MAX_FONTS];
static bool     style1_valid[TEXT_MAX_FONTS];

void text_init(void) {
    font_mono = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    font_var  = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_VAR);

    rdpq_text_register_font(FONT_DEBUG_MONO, font_mono);
    rdpq_text_register_font(FONT_DEBUG_VAR,  font_var);

    // Plain versions of the same two fonts, for text cached in UI layers
    font_ui_mono = rdpq_font_load("rom:/fonts/monogram.font64");
    font_ui_var  = rdpq_font_load("rom:/fonts/at01.font64");
    rdpq_text_register_font(FONT_UI_MONO, font_ui_mono);
    rdpq_text_register_font(FONT_UI_VAR,  font_ui_var);

    // Style 0: default white
    rdpq_font_style(font_mono, 0, &(rdpq_fontstyle_t){
        .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    });
    rdpq_font_style(font_var, 0, &(rdpq_fontstyle_t){
        .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    });
    for (int i = 0; i < TEXT_MAX_FONTS; i++) style1_valid[i] = false;
}

static rdpq_font_t *font_of(uint8_t font_id) {
    if (font_id == FONT_DEBUG_MONO) return font_mono;
    if (font_id == FONT_DEBUG_VAR)  return font_var;
    return (rdpq_font_t *)rdpq_text_get_font(font_id);
}

void text_set_style(uint8_t font_id, uint8_t style_id, color_t color) {
    rdpq_font_t *font = font_of(font_id);
    if (!font) return;
    rdpq_font_style(font, style_id, &(rdpq_fontstyle_t){ .color = color });
    if (style_id == 1 && font_id < TEXT_MAX_FONTS) style1_valid[font_id] = false;
}

// Point style 1 of the font at the colour; false if the font is not loaded
static bool use_color(uint8_t font_id, color_t color) {
    rdpq_font_t *font = font_of(font_id);
    if (!font) return false;
    uint32_t packed = color_to_packed32(color);
    if (font_id < TEXT_MAX_FONTS && style1_valid[font_id] && style1_color[font_id] == packed)
        return true;
    rdpq_font_style(font, 1, &(rdpq_fontstyle_t){ .color = color });
    if (font_id < TEXT_MAX_FONTS) {
        style1_color[font_id] = packed;
        style1_valid[font_id] = true;
    }
    return true;
}

// The font sets its render mode inside a recorded rspq block, which the CPU
// side of rdpq does not track: it would still believe the previous mode (fill
// from a panel line, copy from a UI layer blit) and emit the glyph rectangles
// for that mode, and the text comes out invisible. rdpq_text's AA-fix
// rectangle happened to set standard mode first; without it, set it here.
static void begin_text(void) {
    if (!TEXT_AA_FIX) rdpq_set_mode_standard();
}

static rdpq_textparms_t parms_of(const TextBoxConfig *config) {
    return (rdpq_textparms_t){
        .style_id       = 1,
        .width          = config->width,
        .height         = config->height,
        .align          = config->align,
        .valign         = config->valign,
        .wrap           = config->wrap,
        .disable_aa_fix = !TEXT_AA_FIX,
    };
}

void text_draw(const TextBoxConfig *config, const char *str) {
    if (!use_color(config->font_id, config->color)) return;
    begin_text();
    rdpq_textparms_t parms = parms_of(config);
    rdpq_text_print(&parms, config->font_id, config->x, config->y, str);
}

void text_draw_fmt(const TextBoxConfig *config, const char *fmt, ...) {
    if (!use_color(config->font_id, config->color)) return;
    begin_text();
    rdpq_textparms_t parms = parms_of(config);
    va_list args;
    va_start(args, fmt);
    rdpq_text_vprintf(&parms, config->font_id, config->x, config->y, fmt, args);
    va_end(args);
}

void text_cleanup(void) {
    if (font_mono) {
        rdpq_text_unregister_font(FONT_DEBUG_MONO);
        rdpq_font_free(font_mono);
        font_mono = NULL;
    }
    if (font_var) {
        rdpq_text_unregister_font(FONT_DEBUG_VAR);
        rdpq_font_free(font_var);
        font_var = NULL;
    }
    if (font_ui_mono) {
        rdpq_text_unregister_font(FONT_UI_MONO);
        rdpq_font_free(font_ui_mono);
        font_ui_mono = NULL;
    }
    if (font_ui_var) {
        rdpq_text_unregister_font(FONT_UI_VAR);
        rdpq_font_free(font_ui_var);
        font_ui_var = NULL;
    }
    for (int i = 0; i < TEXT_MAX_FONTS; i++) style1_valid[i] = false;
}
