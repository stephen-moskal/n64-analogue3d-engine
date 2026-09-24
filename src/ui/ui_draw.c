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

#define GRADIENT_BANDS 8

void ui_vgradient(color_t top, color_t bottom, int x0, int y0, int x1, int y1) {
    if (color_to_packed32(top) == color_to_packed32(bottom) || y1 - y0 < GRADIENT_BANDS) {
        ui_rect(top, x0, y0, x1, y1);
        return;
    }
    int h = y1 - y0;
    for (int b = 0; b < GRADIENT_BANDS; b++) {
        int ya = y0 + h * b / GRADIENT_BANDS, yb = y0 + h * (b + 1) / GRADIENT_BANDS;
        int t = b * 256 / (GRADIENT_BANDS - 1);            // 0..256
        color_t c = {
            .r = (uint8_t)(top.r + (bottom.r - top.r) * t / 256),
            .g = (uint8_t)(top.g + (bottom.g - top.g) * t / 256),
            .b = (uint8_t)(top.b + (bottom.b - top.b) * t / 256),
            .a = (uint8_t)(top.a + (bottom.a - top.a) * t / 256),
        };
        ui_rect(c, x0, ya, x1, yb);
    }
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
