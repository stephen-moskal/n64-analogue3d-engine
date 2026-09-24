#ifndef UI_DRAW_H
#define UI_DRAW_H

// Immediate UI primitives: panels, frames, lines and bars, drawn every frame
// with a handful of RDP rectangles (no triangles, no text). Coordinates are
// screen pixels, bottom-right exclusive. Colours with alpha < 255 blend.

#include <libdragon.h>

// Filled rectangle; translucent when the colour's alpha is below 255
void ui_rect(color_t color, int x0, int y0, int x1, int y1);

// Vertical gradient from top to bottom colour, as horizontal bands (a flat
// rectangle when both colours are equal). Rectangles only: hardware-safe.
void ui_vgradient(color_t top, color_t bottom, int x0, int y0, int x1, int y1);

// Frame of the given thickness just inside the rectangle
void ui_frame(color_t color, int thickness, int x0, int y0, int x1, int y1);

// Horizontal gauge: background, then the filled fraction (0..1)
void ui_gauge(color_t bg, color_t fill, float fraction, int x0, int y0, int x1, int y1);

#endif
