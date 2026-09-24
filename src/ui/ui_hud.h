#ifndef UI_HUD_H
#define UI_HUD_H

// HUD panels: blocks of text anchored on screen, cached in a UiLayer and
// refreshed at a fixed rate instead of every frame. Readouts that change
// every frame (positions, counters) re-render only when the panel is due
// and only if their text changed, a few slots per frame at most. Colours,
// shadow and backdrop come from the UiStyle. See docs/UI.md.

#include "ui_layer.h"
#include "ui_style.h"

typedef struct {
    UiLayer layer;
    int16_t x, y;              // screen position of the layer
    uint8_t refresh;           // frames between content updates (0 = every frame)
    uint8_t age;
    bool    first;             // the first update is always due
} HudPanel;

// A panel covering [x, x + w) x [y, y + h) on screen. cached = false draws
// the text every frame (comparison / no surface memory). max_renders spreads
// re-rendering (0 = no limit).
void hud_panel_init(HudPanel *p, int x, int y, int w, int h, bool cached,
                    int refresh, int max_renders);
void hud_panel_free(HudPanel *p);

// A text line in screen coordinates: left edge, baseline, width for the
// alignment. The line's box runs from 8 px above the baseline (10 for
// FONT_UI_VAR) to 4 px below; boxes must not overlap. Returns
// the slot index, or -1.
int  hud_panel_line(HudPanel *p, int x, int baseline, int w, uint8_t font_id,
                    rdpq_align_t align);

// Once per frame: true when the panel's content should be updated now
bool hud_panel_due(HudPanel *p);

static inline void hud_panel_set(HudPanel *p, int line, color_t c, const char *text) {
    ui_layer_set(&p->layer, line, c, text);
}
#define hud_panel_setf(p, line, c, ...) ui_layer_setf(&(p)->layer, (line), (c), __VA_ARGS__)

// Backdrop (style) and text; applies the style's shadow
void hud_panel_draw(HudPanel *p, const UiStyle *style);

#endif
