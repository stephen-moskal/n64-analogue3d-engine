#include "text.h"
#include <stdarg.h>

static rdpq_font_t *font_mono = NULL;
static rdpq_font_t *font_var = NULL;

void text_init(void) {
    font_mono = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    font_var  = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_VAR);

    rdpq_text_register_font(FONT_DEBUG_MONO, font_mono);
    rdpq_text_register_font(FONT_DEBUG_VAR,  font_var);

    // Style 0: default white
    rdpq_font_style(font_mono, 0, &(rdpq_fontstyle_t){
        .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    });
    rdpq_font_style(font_var, 0, &(rdpq_fontstyle_t){
        .color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    });
}

void text_draw(const TextBoxConfig *config, const char *str) {
    rdpq_font_t *font = (config->font_id == FONT_DEBUG_MONO) ? font_mono : font_var;
    if (!font) return;

    // Set dynamic color as style 1
    rdpq_font_style(font, 1, &(rdpq_fontstyle_t){
        .color = config->color,
    });

    rdpq_text_print(&(rdpq_textparms_t){
        .style_id = 1,
        .width    = config->width,
        .height   = config->height,
        .align    = config->align,
        .valign   = config->valign,
        .wrap     = config->wrap,
    }, config->font_id, config->x, config->y, str);
}

void text_draw_fmt(const TextBoxConfig *config, const char *fmt, ...) {
    rdpq_font_t *font = (config->font_id == FONT_DEBUG_MONO) ? font_mono : font_var;
    if (!font) return;

    rdpq_font_style(font, 1, &(rdpq_fontstyle_t){
        .color = config->color,
    });

    va_list args;
    va_start(args, fmt);
    rdpq_text_vprintf(&(rdpq_textparms_t){
        .style_id = 1,
        .width    = config->width,
        .height   = config->height,
        .align    = config->align,
        .valign   = config->valign,
        .wrap     = config->wrap,
    }, config->font_id, config->x, config->y, fmt, args);
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
}
