#include "ui_hud.h"
#include "ui_draw.h"
#include "text.h"
#include <string.h>

#define LINE_ASC   8
#define LINE_DESC  4

void hud_panel_init(HudPanel *p, int x, int y, int w, int h, bool cached,
                    int refresh, int max_renders) {
    memset(p, 0, sizeof(*p));
    ui_layer_init(&p->layer, w, h, cached);
    ui_layer_set_budget(&p->layer, max_renders);
    p->x = (int16_t)x;
    p->y = (int16_t)y;
    p->refresh = (uint8_t)refresh;
    p->first = true;
}

void hud_panel_free(HudPanel *p) {
    ui_layer_free(&p->layer);
    p->first = true;
}

int hud_panel_line(HudPanel *p, int x, int baseline, int w, uint8_t font_id, rdpq_align_t align) {
    int asc = (font_id == FONT_UI_VAR) ? 10 : LINE_ASC;       // at01 is taller than monogram
    return ui_layer_add(&p->layer, x - p->x, baseline - asc - p->y, w, asc + LINE_DESC,
                        baseline - p->y, font_id, align);
}

bool hud_panel_due(HudPanel *p) {
    if (p->first || p->refresh == 0) {
        p->first = false;
        p->age = 0;
        return true;
    }
    if (++p->age >= p->refresh) {
        p->age = 0;
        return true;
    }
    return false;
}

void hud_panel_draw(HudPanel *p, const UiStyle *st) {
    ui_layer_set_shadow(&p->layer, st->hud_shadow);
    if (st->hud_backdrop.a)
        ui_rect(st->hud_backdrop, p->x, p->y, p->x + p->layer.w, p->y + p->layer.h);
    ui_layer_draw(&p->layer, p->x, p->y, p->layer.h);
}
