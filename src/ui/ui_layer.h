#ifndef UI_LAYER_H
#define UI_LAYER_H

// Retained UI text (see docs/UI.md).
//
// Drawing text costs ~15-20 us per glyph on the Analogue 3D, and almost all
// of it is issuing the glyphs, not laying them out. A UiLayer holds a set of
// text slots (a box, a font, a colour, a string). In cached mode the slots are
// rendered into an offscreen RGBA16 surface only when their text or colour
// changes, and each frame the surface is drawn with one copy-mode blit. In
// immediate mode (no surface, for comparison or tight memory) every slot is
// drawn straight to the screen every frame.
//
// Slots must not overlap: re-rendering one clears its box. Boxes are widened to
// 4-pixel columns, so keep neighbours at least that far apart or on columns.

#include <libdragon.h>
#include <stdbool.h>

#define UI_SLOT_TEXT        40      // bytes per slot, including the terminator
#define UI_LAYER_MAX_SLOTS  24

typedef struct {
    int16_t x, y, w, h;        // box inside the layer (cleared and clipped on re-render);
                               // x and w widened to 4-pixel columns (fill mode needs them)
    int16_t tx, tw;            // text position and alignment width, as given
    int16_t ty;                // text baseline, layer coordinates
    uint8_t font_id;
    uint8_t align;             // rdpq_align_t, within the box width
    color_t color;
    bool    dirty;
    char    text[UI_SLOT_TEXT];
} UiSlot;

typedef struct {
    int16_t w, h;              // layer size (px)
    bool    cached;            // false: immediate mode
    bool    allocated;         // surface exists
    bool    clear_all;         // clear the whole surface on the next render
    uint8_t max_renders;       // slots rendered per frame at most (0 = all dirty ones)
    color_t shadow;            // drop shadow at (+1, +1) behind the text (alpha 0 = none)
    surface_t surf;            // RGBA16, cached mode only
    int     slot_count;
    UiSlot  slots[UI_LAYER_MAX_SLOTS];
} UiLayer;

void ui_layer_init(UiLayer *layer, int w, int h, bool cached);
void ui_layer_free(UiLayer *layer);                 // releases the surface; slots stay
void ui_layer_clear_slots(UiLayer *layer);          // remove every slot (relayout)

// Add a slot; returns its index or -1 when full. Text is drawn at baseline ty
// (layer coordinates) and aligned within [x, x + w).
int  ui_layer_add(UiLayer *layer, int x, int y, int w, int h, int ty,
                  uint8_t font_id, rdpq_align_t align);

// Set a slot's text and colour; it re-renders only if either changed
void ui_layer_set(UiLayer *layer, int slot, color_t color, const char *text);
void ui_layer_setf(UiLayer *layer, int slot, color_t color, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// Re-render every slot on the next draw (e.g. after a style change)
void ui_layer_invalidate(UiLayer *layer);

// Drop shadow behind all text (alpha 0 = none): keeps plain-font text readable
// over the scene. A change re-renders the layer.
void ui_layer_set_shadow(UiLayer *layer, color_t shadow);

// Spread re-rendering: at most n dirty slots per frame (0 = all). The rest
// keep their old text until a later frame. Good for HUDs and overlays whose
// many small updates would otherwise land in one frame.
void ui_layer_set_budget(UiLayer *layer, int n);

// Render what changed, then draw rows [0, used_h) of the layer with its
// top-left corner at (x, y). Call while the frame buffer is attached.
void ui_layer_draw(UiLayer *layer, int x, int y, int used_h);

// Custom content (cached mode): begin allocates the surface if needed,
// attaches it and clears it to transparent; draw anything (text with
// text_draw, a laid-out paragraph with text_render_paragraph - never plain
// rdpq_paragraph_render, see text.h - or rectangles), then end detaches.
// Use on a layer without slots. Returns false in immediate mode.
bool ui_layer_canvas_begin(UiLayer *layer);
void ui_layer_canvas_end(UiLayer *layer);

// Draw part of the layer: the source rectangle (sx, sy, sw, sh) lands at
// screen (x + sx, y + sy). For reveals and scrolling windows.
void ui_layer_draw_part(UiLayer *layer, int x, int y, int sx, int sy, int sw, int sh);

#endif
