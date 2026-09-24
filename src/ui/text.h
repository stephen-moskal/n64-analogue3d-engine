#ifndef TEXT_H
#define TEXT_H

#include <libdragon.h>

// Font registry IDs (1-255, 0 reserved by libdragon)
#define FONT_DEBUG_MONO  1    // libdragon builtin, outlined
#define FONT_DEBUG_VAR   2    // libdragon builtin, outlined
#define FONT_UI_MONO     3    // assets/fonts/monogram.ttf, plain (cached UI text)
#define FONT_UI_VAR      4    // assets/fonts/at01.ttf, plain (cached UI text)

typedef struct {
    float x, y;              // Screen position (text baseline)
    int16_t width;           // Bounding box width in px (0 = unbounded)
    int16_t height;          // Bounding box height in px (0 = unbounded)
    uint8_t font_id;         // Registered font ID (FONT_DEBUG_MONO, etc.)
    color_t color;           // Text color (RGBA32)
    rdpq_align_t align;      // ALIGN_LEFT / ALIGN_CENTER / ALIGN_RIGHT
    rdpq_valign_t valign;    // VALIGN_TOP / VALIGN_CENTER / VALIGN_BOTTOM
    rdpq_textwrap_t wrap;    // WRAP_NONE / WRAP_WORD / WRAP_CHAR / WRAP_ELLIPSES
} TextBoxConfig;

void text_init(void);
void text_draw(const TextBoxConfig *config, const char *str);

// Register a fixed color as a style of a font, for inline "^xx" style switches
// in a single multi-line text call (much cheaper than one call per line).
// Style 0 is white and style 1 is rewritten by every text_draw() call.
void text_set_style(uint8_t font_id, uint8_t style_id, color_t color);
void text_draw_fmt(const TextBoxConfig *config, const char *fmt, ...);
void text_cleanup(void);

#endif
