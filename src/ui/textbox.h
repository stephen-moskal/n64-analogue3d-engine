#ifndef TEXTBOX_H
#define TEXTBOX_H

// Text box: shows a DialogRunner (src/dialog/dialog.h) in a UiStyle.
// A styled box with the speaker's name plate, word-wrapped text split into
// pages, a typewriter reveal (with the line's [p=ms] pauses), an "advance"
// prompt and a choice list. Each page is laid out and rendered once into a
// cached layer; the reveal draws a growing part of it, so a page costs one
// render and then a blit or two per frame. See docs/DIALOG.md.

#include "ui_layer.h"
#include "ui_style.h"
#include "../dialog/dialog.h"

// One frame of input, however the game maps its buttons
typedef struct {
    bool confirm;    // advance / pick (A)
    bool cancel;     // show the whole page at once (B)
    bool up, down;   // move through choices
} UiInput;

typedef enum { TB_CLOSED, TB_REVEAL, TB_WAIT, TB_CHOICE } TextBoxState;

#define TB_MAX_LINES   6
#define TB_MAX_PAUSES 16
#define TB_MAX_BREAKS  8
#define TB_LINE_GLYPHS 96           // glyph positions kept per line (reveal)

typedef struct {
    DialogRunner *runner;
    const UiStyle *style;           // style the layers are laid out for
    UiLayer body;                   // the current page (canvas)
    UiLayer ui;                     // speaker name and choices (slots)
    int s_name, s_choice[DIALOG_CHOICE_MAX];
    TextBoxState state;
    uint32_t serial;                // runner line on screen

    // The current line, control bytes removed
    char clean[DIALOG_TEXT_MAX];
    int  clean_len;
    int  breaks[TB_MAX_BREAKS], n_breaks;             // forced page breaks (byte offsets)
    int  pause_at[TB_MAX_PAUSES], n_pauses;           // glyph index in the line
    uint8_t pause_units[TB_MAX_PAUSES];

    // The current page
    bool page_ready;
    int  page_start, page_end;      // bytes of clean
    int  page_glyph_base;           // glyphs before this page
    int  page_glyphs;
    int  nlines;
    int  line_glyphs[TB_MAX_LINES];
    int16_t line_top[TB_MAX_LINES], line_bot[TB_MAX_LINES], line_x1[TB_MAX_LINES];
    int16_t line_gx[TB_MAX_LINES][TB_LINE_GLYPHS];   // glyph pen x, left to right
    int  name_w;                    // name plate text width (px)

    float reveal;                   // glyphs shown on this page
    float pause_left;               // seconds
    int   next_pause;
    int   sel;                      // highlighted choice
    float blink;

    // Optional: called every blip_every revealed glyphs (a "talk" sound)
    void (*on_blip)(void *ctx);
    void *blip_ctx;
    int   blip_every;
    int   blip_count;
} TextBox;

void textbox_init(TextBox *tb);
// Show the runner's conversation (already started with dialog_start)
void textbox_open(TextBox *tb, DialogRunner *runner);
bool textbox_active(const TextBox *tb);
void textbox_update(TextBox *tb, const UiInput *in, float dt);
void textbox_draw(TextBox *tb, const UiStyle *style);
void textbox_close(TextBox *tb);   // also frees the layers' surfaces

#endif
