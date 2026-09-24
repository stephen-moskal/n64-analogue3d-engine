#include "textbox.h"
#include "ui_draw.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

// Layout (all from the style's box, tb_*)
#define NAME_PLATE_H    14          // speaker name plate above the box
#define CHOICE_ROW_H    12
#define CHOICE_BOX_W   136
#define UI_H           (NAME_PLATE_H + DIALOG_CHOICE_MAX * CHOICE_ROW_H + 12)

// --- Text scanning ---------------------------------------------------------

// rdpq_text escapes: ^NN switches style, $NN font; ^^ and $$ are literals
static int escape_len(const char *s) {
    if ((s[0] == '^' || s[0] == '$') && s[1] == s[0]) return 0;      // literal: a glyph
    if ((s[0] == '^' || s[0] == '$') && s[1] && s[2]) return 3;
    return 0;
}

// Glyphs rdpq will draw for s[0..n): spaces and newlines draw nothing
static int count_glyphs(const char *s, int n) {
    int g = 0;
    for (int i = 0; i < n && s[i]; ) {
        int e = escape_len(s + i);
        if (e) { i += e; continue; }
        if ((s[i] == '^' || s[i] == '$') && s[i + 1] == s[i]) { g++; i += 2; continue; }
        if (s[i] != ' ' && s[i] != '\n') g++;
        i++;
    }
    return g;
}

// Style in effect at byte n (the last ^NN before it), for pages that start
// in the middle of a coloured span
static int style_at(const char *s, int n) {
    int st = DIALOG_COL_TEXT;
    for (int i = 0; i < n && s[i]; i++) {
        if (s[i] == '^' && s[i + 1] == '^') { i++; continue; }
        if (s[i] == '^' && s[i + 1] && s[i + 2]) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            st = (int)strtol(hex, NULL, 16);
            i += 2;
        }
    }
    return st;
}

// Runner text -> clean text + pause and page-break positions
static void prepare_line(TextBox *tb) {
    const char *src = dialog_text(tb->runner);
    int o = 0;
    tb->n_breaks = tb->n_pauses = 0;
    for (int i = 0; src[i] && o < DIALOG_TEXT_MAX - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == DIALOG_CTRL_PAUSE && src[i + 1]) {
            if (tb->n_pauses < TB_MAX_PAUSES) {
                tb->pause_at[tb->n_pauses] = count_glyphs(tb->clean, o);
                tb->pause_units[tb->n_pauses++] = (uint8_t)src[i + 1];
            }
            i++;
            continue;
        }
        if (c == DIALOG_CTRL_PAGE) {
            if (tb->n_breaks < TB_MAX_BREAKS) tb->breaks[tb->n_breaks++] = o;
            continue;
        }
        tb->clean[o++] = (char)c;
        tb->clean[o] = '\0';
    }
    tb->clean[o] = '\0';
    tb->clean_len = o;
    tb->page_start = 0;
    tb->page_ready = false;
}

static int segment_end(const TextBox *tb, int from) {
    for (int i = 0; i < tb->n_breaks; i++)
        if (tb->breaks[i] > from) return tb->breaks[i];
    return tb->clean_len;
}

// --- Lifecycle --------------------------------------------------------------

void textbox_init(TextBox *tb) {
    memset(tb, 0, sizeof(*tb));
    tb->state = TB_CLOSED;
    tb->blip_every = 3;
}

static void setup_layers(TextBox *tb, const UiStyle *st) {
    ui_layer_free(&tb->body);
    ui_layer_free(&tb->ui);
    int inner_w = st->tb_x1 - st->tb_x0 - 2 * st->tb_pad;
    int inner_h = st->tb_y1 - st->tb_y0 - 2 * st->tb_pad;
    ui_layer_init(&tb->body, inner_w, inner_h, true);

    int w = st->tb_x1 - st->tb_x0;
    ui_layer_init(&tb->ui, w, UI_H, true);
    // Name plate text, bottom-left of the area above the box
    tb->s_name = ui_layer_add(&tb->ui, st->tb_pad, UI_H - NAME_PLATE_H + 1, 160, NAME_PLATE_H - 2,
                              UI_H - 4, st->font_dialog, ALIGN_LEFT);
    // Choice rows, bottom-up, right side (row r's baseline r rows above the box)
    for (int r = 0; r < DIALOG_CHOICE_MAX; r++) {
        int base = UI_H - 6 - r * CHOICE_ROW_H;
        tb->s_choice[r] = ui_layer_add(&tb->ui, w - CHOICE_BOX_W + 8, base - 9, CHOICE_BOX_W - 12,
                                       CHOICE_ROW_H, base, st->font_dialog, ALIGN_LEFT);
    }
    tb->style = st;
    tb->page_ready = false;
}

void textbox_open(TextBox *tb, DialogRunner *runner) {
    tb->runner = runner;
    tb->serial = runner->serial - 1;          // forces the first line to be picked up
    tb->state = dialog_running(runner) ? TB_REVEAL : TB_CLOSED;
}

bool textbox_active(const TextBox *tb) { return tb->state != TB_CLOSED; }

void textbox_close(TextBox *tb) {
    tb->state = TB_CLOSED;
    ui_layer_free(&tb->body);
    ui_layer_free(&tb->ui);
    tb->style = NULL;
}

// --- Update -----------------------------------------------------------------

static void page_done(TextBox *tb) {
    tb->reveal = (float)tb->page_glyphs;
    tb->pause_left = 0.0f;
    bool more_pages = tb->page_end < tb->clean_len;
    if (!more_pages && tb->runner->choice_count > 0) {
        tb->state = TB_CHOICE;
        tb->sel = 0;
    } else {
        tb->state = TB_WAIT;
    }
}

void textbox_update(TextBox *tb, const UiInput *in, float dt) {
    if (tb->state == TB_CLOSED || !tb->runner) return;
    DialogRunner *r = tb->runner;

    if (r->serial != tb->serial) {                  // a new line (or the end)
        tb->serial = r->serial;
        if (!dialog_running(r)) { textbox_close(tb); return; }
        prepare_line(tb);
        tb->state = TB_REVEAL;
        tb->sel = 0;
    }
    tb->blink += dt;
    if (!tb->page_ready) return;                    // laid out on the next draw

    switch (tb->state) {
    case TB_REVEAL:
        if (in->confirm || in->cancel) {
            tb->next_pause = tb->n_pauses;          // skip this page's pauses
            page_done(tb);
            break;
        }
        if (tb->pause_left > 0.0f) {
            tb->pause_left -= dt;
            break;
        }
        {
            float prev = tb->reveal;
            tb->reveal += tb->style->tb_cps * dt;
            // A pause stops the reveal at its glyph
            while (tb->next_pause < tb->n_pauses) {
                int at = tb->pause_at[tb->next_pause] - tb->page_glyph_base;
                if (at > tb->page_glyphs) break;             // on a later page
                if (at > tb->reveal) break;                  // not reached yet
                if (at >= prev) {
                    tb->reveal = (float)at;
                    tb->pause_left = tb->pause_units[tb->next_pause] * (DIALOG_PAUSE_UNIT_MS / 1000.0f);
                    tb->next_pause++;
                    break;
                }
                tb->next_pause++;
            }
            if (tb->on_blip && tb->blip_every > 0) {
                int shown = (int)tb->reveal;
                while (shown - tb->blip_count >= tb->blip_every) {
                    tb->blip_count += tb->blip_every;
                    tb->on_blip(tb->blip_ctx);
                }
            }
            if (tb->reveal >= tb->page_glyphs) page_done(tb);
        }
        break;
    case TB_WAIT:
        if (in->confirm || in->cancel) {
            if (tb->page_end < tb->clean_len) {    // next page of the same line
                tb->page_start = tb->page_end;
                tb->page_ready = false;
                tb->state = TB_REVEAL;
            } else {
                dialog_advance(r);                  // next line or the end
            }
        }
        break;
    case TB_CHOICE:
        if (in->up)   tb->sel = (tb->sel + r->choice_count - 1) % r->choice_count;
        if (in->down) tb->sel = (tb->sel + 1) % r->choice_count;
        if (in->confirm) dialog_choose(r, tb->sel);
        break;
    default:
        break;
    }
}

// --- Draw -------------------------------------------------------------------

// Style palette on the dialog font: ids 2..6 (see dialog.h)
static void bind_palette(const UiStyle *st) {
    text_set_style(st->font_dialog, DIALOG_COL_TEXT,   st->dialog_text);
    text_set_style(st->font_dialog, DIALOG_COL_ACCENT, st->hud_accent);
    text_set_style(st->font_dialog, DIALOG_COL_HILITE, st->hilite);
    text_set_style(st->font_dialog, DIALOG_COL_TITLE,  st->title);
    text_set_style(st->font_dialog, DIALOG_COL_DIM,    st->disabled);
}

static color_t palette(const UiStyle *st, int id) {
    switch (id) {
    case DIALOG_COL_ACCENT: return st->hud_accent;
    case DIALOG_COL_HILITE: return st->hilite;
    case DIALOG_COL_TITLE:  return st->title;
    case DIALOG_COL_DIM:    return st->disabled;
    default:                return st->dialog_text;
    }
}

// Lay out and render the current page into the body layer; record its lines
static void build_page(TextBox *tb) {
    const UiStyle *st = tb->style;
    // Skip the whitespace a page break left at the start of the page
    while (tb->page_start < tb->clean_len &&
           (tb->clean[tb->page_start] == ' ' || tb->clean[tb->page_start] == '\n'))
        tb->page_start++;
    int seg_end = segment_end(tb, tb->page_start);
    int nbytes = seg_end - tb->page_start;

    bind_palette(st);
    rdpq_textparms_t parms = {
        .style_id = (int16_t)style_at(tb->clean, tb->page_start),
        .width = tb->body.w, .height = tb->body.h,
        .wrap = WRAP_WORD, .disable_aa_fix = true,
    };
    rdpq_paragraph_t *p = rdpq_paragraph_build(&parms, st->font_dialog,
                                               tb->clean + tb->page_start, &nbytes);
    tb->page_end = tb->page_start + (nbytes > 0 ? nbytes : seg_end - tb->page_start);

    // Lines: group glyphs by baseline
    tb->nlines = 0;
    for (int i = 0; i < p->nchars; i++) {
        int y = (int)p->y0 + p->chars[i].y;
        int x1 = (int)p->x0 + p->chars[i].x + 8;
        int l = 0;
        while (l < tb->nlines && tb->line_bot[l] != y) l++;
        if (l == tb->nlines) {
            if (tb->nlines == TB_MAX_LINES) continue;
            tb->nlines++;
            tb->line_bot[l] = (int16_t)y;       // baseline for now
            tb->line_glyphs[l] = 0;
            tb->line_x1[l] = 0;
        }
        // Keep the line's glyph x positions sorted (chars come in atlas order)
        int k = tb->line_glyphs[l]++;
        if (k < TB_LINE_GLYPHS) {
            int16_t gx = (int16_t)(x1 - 8);
            while (k > 0 && tb->line_gx[l][k - 1] > gx) { tb->line_gx[l][k] = tb->line_gx[l][k - 1]; k--; }
            tb->line_gx[l][k] = gx;
        }
        if (x1 > tb->line_x1[l]) tb->line_x1[l] = (int16_t)x1;
    }
    // Sort lines top to bottom, then turn baselines into bands
    for (int a = 1; a < tb->nlines; a++)
        for (int b = a; b > 0 && tb->line_bot[b] < tb->line_bot[b - 1]; b--) {
            int16_t t = tb->line_bot[b]; tb->line_bot[b] = tb->line_bot[b - 1]; tb->line_bot[b - 1] = t;
            t = tb->line_x1[b]; tb->line_x1[b] = tb->line_x1[b - 1]; tb->line_x1[b - 1] = t;
            int g = tb->line_glyphs[b]; tb->line_glyphs[b] = tb->line_glyphs[b - 1]; tb->line_glyphs[b - 1] = g;
            int16_t tmp[TB_LINE_GLYPHS];
            memcpy(tmp, tb->line_gx[b], sizeof(tmp));
            memcpy(tb->line_gx[b], tb->line_gx[b - 1], sizeof(tmp));
            memcpy(tb->line_gx[b - 1], tmp, sizeof(tmp));
        }
    for (int l = 0; l < tb->nlines; l++) {
        int base = tb->line_bot[l];
        tb->line_top[l] = (int16_t)(l == 0 ? 0 : tb->line_bot[l - 1]);
        tb->line_bot[l] = (int16_t)(l == tb->nlines - 1 ? tb->body.h : base + 3);
    }
    // Bands of consecutive lines must not overlap: previous bottom = this top
    for (int l = 1; l < tb->nlines; l++) tb->line_top[l] = tb->line_bot[l - 1];

    tb->page_glyphs = p->nchars;
    tb->page_glyph_base = count_glyphs(tb->clean, tb->page_start);

    if (ui_layer_canvas_begin(&tb->body)) {
        text_render_paragraph(p, 0, 0);
        ui_layer_canvas_end(&tb->body);
    }
    rdpq_paragraph_free(p);

    tb->reveal = 0.0f;
    tb->blip_count = 0;
    tb->pause_left = 0.0f;
    tb->next_pause = 0;
    while (tb->next_pause < tb->n_pauses &&
           tb->pause_at[tb->next_pause] < tb->page_glyph_base) tb->next_pause++;
    tb->page_ready = true;
    if (tb->page_glyphs == 0) page_done(tb);   // an empty page (e.g. only a pause)
}

static int measure(uint8_t font, const char *s) {
    if (!s || !s[0]) return 0;
    int n = (int)strlen(s);
    rdpq_paragraph_t *p = rdpq_paragraph_build(&(rdpq_textparms_t){ .disable_aa_fix = true }, font, s, &n);
    int w = (int)(p->bbox.x1 - p->bbox.x0 + 0.5f);
    rdpq_paragraph_free(p);
    return w;
}

void textbox_draw(TextBox *tb, const UiStyle *st) {
    if (tb->state == TB_CLOSED || !tb->runner || !dialog_running(tb->runner)) return;
    bool relayout = st != tb->style;              // first draw, or the style changed
    if (relayout) setup_layers(tb, st);
    if (!tb->page_ready) {
        TextBoxState was = tb->state;
        int sel = tb->sel;
        build_page(tb);
        tb->name_w = measure(st->font_dialog, dialog_speaker(tb->runner));
        // A page that was already fully shown stays shown (and keeps the
        // highlighted choice) when a style change lays it out again
        if (relayout && was != TB_REVEAL) {
            page_done(tb);
            if (tb->state == TB_CHOICE && sel < tb->runner->choice_count) tb->sel = sel;
        }
    }
    DialogRunner *r = tb->runner;
    int x0 = st->tb_x0, y0 = st->tb_y0, x1 = st->tb_x1, y1 = st->tb_y1;
    int uy = y0 - UI_H;                           // top of the ui layer

    // Name plate
    const char *name = dialog_speaker(r);
    if (name[0]) {
        int nx1 = x0 + tb->name_w + 2 * st->tb_pad;
        ui_vgradient(st->panel, st->panel, x0, y0 - NAME_PLATE_H, nx1, y0);
        ui_frame(st->border, st->border_w, x0, y0 - NAME_PLATE_H, nx1, y0 + st->border_w);
    }
    ui_layer_set(&tb->ui, tb->s_name, palette(st, dialog_speaker_color(r)), name);

    // Choices (only once the question is fully shown)
    int n = tb->state == TB_CHOICE ? r->choice_count : 0;
    for (int row = 0; row < DIALOG_CHOICE_MAX; row++) {
        int i = n - 1 - row;                       // row 0 is the bottom one
        if (i < 0) { ui_layer_set(&tb->ui, tb->s_choice[row], st->dialog_text, ""); continue; }
        bool on = (i == tb->sel);
        ui_layer_setf(&tb->ui, tb->s_choice[row], on ? st->hilite : st->dialog_text,
                      "%s%s", on ? "> " : "  ", r->choice_text[i]);
    }
    if (n > 0) {
        int cy0 = y0 - 4 - n * CHOICE_ROW_H - 4;
        ui_vgradient(st->panel, st->panel2, x1 - CHOICE_BOX_W, cy0, x1, y0 - 2);
        ui_frame(st->border, st->border_w, x1 - CHOICE_BOX_W, cy0, x1, y0 - 2);
    }

    // The box
    ui_vgradient(st->panel, st->panel2, x0, y0, x1, y1);
    ui_frame(st->border, st->border_w, x0, y0, x1, y1);

    // Revealed text: whole lines, then the growing part of the current one
    int bx = x0 + st->tb_pad, by = y0 + st->tb_pad;
    int shown = (int)tb->reveal, full_bot = 0, l = 0;
    for (; l < tb->nlines && shown >= tb->line_glyphs[l]; l++) {
        shown -= tb->line_glyphs[l];
        full_bot = tb->line_bot[l];
    }
    if (full_bot > 0) ui_layer_draw_part(&tb->body, bx, by, 0, 0, tb->body.w, full_bot);
    if (l < tb->nlines && shown > 0) {
        // Up to the pen position of the first hidden glyph
        int w = shown < TB_LINE_GLYPHS ? tb->line_gx[l][shown] : tb->line_x1[l];
        ui_layer_draw_part(&tb->body, bx, by, 0, tb->line_top[l], w, tb->line_bot[l] - tb->line_top[l]);
    }

    // Name and choices
    ui_layer_draw(&tb->ui, x0, uy, UI_H);
    // Advance prompt: a small blinking wedge, bottom right
    if (tb->state == TB_WAIT && ((int)(tb->blink * 3.0f) & 1) == 0) {
        int px = x1 - st->tb_pad - 6, py = y1 - st->tb_pad + 1;
        ui_rect(st->hilite, px, py, px + 5, py + 1);
        ui_rect(st->hilite, px + 1, py + 1, px + 4, py + 2);
        ui_rect(st->hilite, px + 2, py + 2, px + 3, py + 3);
    }
}
