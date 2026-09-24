#include "ui_layer.h"
#include "text.h"
#include "../debug/stats.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


void ui_layer_init(UiLayer *layer, int w, int h, bool cached) {
    memset(layer, 0, sizeof(*layer));
    layer->w = (int16_t)((w + 7) & ~7);           // whole 8-pixel texel rows for the blit
    layer->h = (int16_t)h;
    layer->cached = cached;
}

void ui_layer_free(UiLayer *layer) {
    if (layer->allocated) {
        rspq_wait();                                // the RDP may still read or write it
        surface_free(&layer->surf);
        layer->allocated = false;
    }
    ui_layer_invalidate(layer);
}

void ui_layer_clear_slots(UiLayer *layer) {
    layer->slot_count = 0;
    layer->clear_all = true;
}

int ui_layer_add(UiLayer *layer, int x, int y, int w, int h, int ty,
                 uint8_t font_id, rdpq_align_t align) {
    if (layer->slot_count >= UI_LAYER_MAX_SLOTS) return -1;
    UiSlot *s = &layer->slots[layer->slot_count];
    memset(s, 0, sizeof(*s));
    // Fill-mode clears on a 16-bit surface need a 4-pixel aligned scissor
    int bx0 = x & ~3, bx1 = (x + w + 3) & ~3;
    s->x = (int16_t)bx0; s->w = (int16_t)(bx1 - bx0);
    s->y = (int16_t)y; s->h = (int16_t)h;
    s->tx = (int16_t)x; s->tw = (int16_t)w;
    s->ty = (int16_t)ty;
    s->font_id = font_id;
    s->align = (uint8_t)align;
    s->dirty = true;
    return layer->slot_count++;
}

void ui_layer_set(UiLayer *layer, int slot, color_t color, const char *text) {
    if (slot < 0 || slot >= layer->slot_count) return;
    UiSlot *s = &layer->slots[slot];
    if (!text) text = "";
    if (color_to_packed32(s->color) == color_to_packed32(color) &&
        strncmp(s->text, text, UI_SLOT_TEXT - 1) == 0) return;
    s->color = color;
    strncpy(s->text, text, UI_SLOT_TEXT - 1);
    s->text[UI_SLOT_TEXT - 1] = '\0';
    s->dirty = true;
}

void ui_layer_setf(UiLayer *layer, int slot, color_t color, const char *fmt, ...) {
    char buf[UI_SLOT_TEXT];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_layer_set(layer, slot, color, buf);
}

void ui_layer_invalidate(UiLayer *layer) {
    layer->clear_all = true;
    for (int i = 0; i < layer->slot_count; i++) layer->slots[i].dirty = true;
}

static void draw_slot_text(const UiSlot *s, int ox, int oy) {
    if (s->text[0] == '\0') return;
    TextBoxConfig cfg = {
        .x = ox + s->tx, .y = oy + s->ty, .width = s->tw,
        .font_id = s->font_id, .color = s->color, .align = (rdpq_align_t)s->align,
    };
    text_draw(&cfg, s->text);
}

// Render the dirty slots into the surface (nested attach: the frame buffer is
// restored by rdpq_detach, the scissor is not, so it is reset afterwards)
static void render_dirty(UiLayer *layer) {
    bool any = layer->clear_all;
    for (int i = 0; i < layer->slot_count && !any; i++) any = layer->slots[i].dirty;
    if (!any) return;

    if (!layer->allocated) {
        layer->surf = surface_alloc(FMT_RGBA16, layer->w, layer->h);
        layer->allocated = true;
        layer->clear_all = true;
    }

    rdpq_attach(&layer->surf, NULL);
    int renders = 0;
    if (layer->clear_all) {
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 0));
        rdpq_fill_rectangle(0, 0, layer->w, layer->h);
    }
    for (int i = 0; i < layer->slot_count; i++) {
        UiSlot *s = &layer->slots[i];
        if (!s->dirty && !layer->clear_all) continue;
        rdpq_set_scissor(s->x, s->y, s->x + s->w, s->y + s->h);
        if (!layer->clear_all) {
            rdpq_set_mode_fill(RGBA32(0, 0, 0, 0));
            rdpq_fill_rectangle(s->x, s->y, s->x + s->w, s->y + s->h);
        }
        draw_slot_text(s, 0, 0);
        s->dirty = false;
        renders++;
    }
    layer->clear_all = false;
    rdpq_detach();

    // Layers are drawn onto the display framebuffer: reopen the whole screen
    rdpq_set_scissor(0, 0, display_get_width(), display_get_height());
    STATS_ADD(ui_renders, renders);
}

void ui_layer_draw(UiLayer *layer, int x, int y, int used_h) {
    if (used_h <= 0 || used_h > layer->h) used_h = layer->h;

    if (!layer->cached) {
        for (int i = 0; i < layer->slot_count; i++) {
            draw_slot_text(&layer->slots[i], x, y);
            layer->slots[i].dirty = false;
        }
        STATS_ADD(ui_renders, layer->slot_count);
        return;
    }

    render_dirty(layer);
    if (!layer->allocated) return;                 // nothing was ever drawn

    // Copy mode: 4 pixels per RDP clock; alpha compare skips transparent texels
    rdpq_set_mode_copy(true);
    rdpq_tex_blit(&layer->surf, x, y, &(rdpq_blitparms_t){ .height = used_h });
    STATS_INC(ui_blits);
}
