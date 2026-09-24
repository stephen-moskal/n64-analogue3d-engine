#include "ui_draw.h"
#include "../debug/stats.h"

// Opaque rectangles use fill mode (fastest). Translucent ones use standard
// mode with a flat combiner and the blender; rdpq_fill_rectangle works in
// both (rectangles are hardware-safe in any mode; triangles are not in fill).
void ui_rect(color_t color, int x0, int y0, int x1, int y1) {
    if (color.a == 0 || x1 <= x0 || y1 <= y0) return;
    if (color.a == 0xFF) {
        rdpq_set_mode_fill(color);
    } else {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(color);
    }
    rdpq_fill_rectangle(x0, y0, x1, y1);
    STATS_INC(fill_rects);
}

void ui_frame(color_t color, int t, int x0, int y0, int x1, int y1) {
    if (t <= 0 || color.a == 0) return;
    ui_rect(color, x0, y0, x1, y0 + t);            // top
    ui_rect(color, x0, y1 - t, x1, y1);            // bottom
    ui_rect(color, x0, y0 + t, x0 + t, y1 - t);    // left
    ui_rect(color, x1 - t, y0 + t, x1, y1 - t);    // right
}

void ui_gauge(color_t bg, color_t fill, float fraction, int x0, int y0, int x1, int y1) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    int xf = x0 + (int)((x1 - x0) * fraction + 0.5f);
    ui_rect(fill, x0, y0, xf, y1);
    ui_rect(bg, xf, y0, x1, y1);
}
