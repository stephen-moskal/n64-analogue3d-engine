#include "overlay.h"
#include <libdragon.h>
#include <stdarg.h>
#include <stdio.h>
#include "debug_menu.h"
#include "profiler.h"
#include "stats.h"
#include "memstats.h"
#include "frametime.h"
#include "../ui/text.h"

/*
 * Layout is derived from the measured width of the debug mono font, so text
 * columns, bars and panels never overlap. Text lines are limited to
 * TEXT_COLS characters; bars live in their own column to the right.
 * Everything stays inside a conservative safe area (x >= 12, x <= 300) so it
 * survives TV / scaler overscan cropping.
 */
#define SAFE_X0       12
#define SAFE_X1      300
#define PANEL_Y0      34
#define PAD            4
#define TEXT_COLS     26      // max characters per text line
#define LABEL_COLS    17      // profiler: "name       12.34"
#define BAR_W_MAX     80      // widest bar (px)
#define PX_PER_MS      4.5f   // 16.7 ms budget = 75 px
#define LINE_TARGET   10      // line height used (font natural height is 13 px)
#define REFRESH_FRAMES 15     // rebuild page text/bars every 15 frames (4 Hz)

#define COL_TEXT   RGBA32(0xE0, 0xE0, 0xE0, 0xFF)
#define COL_HEAD   RGBA32(0x80, 0xE0, 0xFF, 0xFF)
#define COL_GREEN  RGBA32(0x30, 0xD0, 0x40, 0xFF)
#define COL_YELLOW RGBA32(0xF0, 0xD0, 0x20, 0xFF)
#define COL_RED    RGBA32(0xF0, 0x40, 0x30, 0xFF)
#define COL_MARK   RGBA32(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_DIM    RGBA32(0x70, 0x70, 0x70, 0xFF)

static int char_w = 0;        // measured advance of the mono font (px)
static int line_h = 9;        // measured line advance (px)
static int ascent = 7;        // baseline offset of the first line (px)

// Text is batched: every line of a page is appended to one buffer with inline
// "^xx" style switches and printed with a single rdpq_text_print() call.
// One call per line cost ~0.35 ms each on the Analogue 3D.
enum { ST_TEXT = 0x10, ST_HEAD, ST_GREEN, ST_YELLOW, ST_RED, ST_DIM };
static char text_buf[1536];
static int  text_len;
static int  text_row;
static int  line_spacing;     // added to the font's natural line height

// Cached page: rebuilt at REFRESH_FRAMES, replayed every frame. Laying text
// out (rdpq_paragraph_build) is the expensive part (~17 us per glyph on the
// A3D); rendering a prebuilt paragraph is much cheaper.
static rdpq_paragraph_t *cached_para = NULL;
static int  cached_age  = 1000;
static int  cached_page = -1;
static int  panel_y1;

// Deferred bars: text and rectangles use different RDP modes, so bars are
// queued while text is printed and drawn together afterwards (one mode set).
#define MAX_BARS 48
static struct { int16_t x, y, w, h; color_t c; } bars[MAX_BARS];
static int bar_count;
static int panel_x1;          // right edge of the current panel (for clamping)

// Frame-time stats are sorted on demand; refresh a few times per second only
static FrameTimeStats ft_cache;

static int text_x(void)        { return SAFE_X0 + PAD; }
static int bar_col_x(int cols) { return text_x() + cols * char_w + PAD; }
static int row_y(int row)      { return PANEL_Y0 + PAD + row * line_h; }

static void measure_font(void) {
    const char *probe = "0000000000";
    int nbytes = 10;
    rdpq_textparms_t parms = {0};
    rdpq_paragraph_t *p = rdpq_paragraph_build(&parms, FONT_DEBUG_MONO, probe, &nbytes);
    char_w = (int)((p->bbox.x1 - p->bbox.x0) / 10.0f + 0.5f);
    rdpq_paragraph_free(p);
    if (char_w < 4 || char_w > 12) char_w = 8;   // sanity fallback

    // Line advance = height of a 2-line paragraph minus a 1-line one
    nbytes = 1;
    p = rdpq_paragraph_build(&parms, FONT_DEBUG_MONO, "0", &nbytes);
    float one_y0 = p->bbox.y0, one_y1 = p->bbox.y1;
    rdpq_paragraph_free(p);
    nbytes = 3;
    p = rdpq_paragraph_build(&parms, FONT_DEBUG_MONO, "0\n0", &nbytes);
    float two_y1 = p->bbox.y1;
    rdpq_paragraph_free(p);
    int natural = (int)(two_y1 - one_y1 + 0.5f);
    if (natural < 6 || natural > 20) natural = 13;
    line_spacing = LINE_TARGET - natural;
    line_h = LINE_TARGET;
    ascent = (int)(-one_y0 + 0.5f);
    if (ascent < 4 || ascent > 12) ascent = 7;

    text_set_style(FONT_DEBUG_MONO, ST_TEXT,   COL_TEXT);
    text_set_style(FONT_DEBUG_MONO, ST_HEAD,   COL_HEAD);
    text_set_style(FONT_DEBUG_MONO, ST_GREEN,  COL_GREEN);
    text_set_style(FONT_DEBUG_MONO, ST_YELLOW, COL_YELLOW);
    text_set_style(FONT_DEBUG_MONO, ST_RED,    COL_RED);
    text_set_style(FONT_DEBUG_MONO, ST_DIM,    COL_DIM);

    debugf("[overlay] debug mono font: advance %d px, natural line %d px (using %d), ascent %d px\n",
           char_w, natural, line_h, ascent);
}

static int style_for(color_t c) {
    uint32_t v = color_to_packed32(c);
    if (v == color_to_packed32(COL_HEAD))   return ST_HEAD;
    if (v == color_to_packed32(COL_GREEN))  return ST_GREEN;
    if (v == color_to_packed32(COL_YELLOW)) return ST_YELLOW;
    if (v == color_to_packed32(COL_RED))    return ST_RED;
    if (v == color_to_packed32(COL_DIM))    return ST_DIM;
    return ST_TEXT;
}

static void text_begin(void) {
    text_len = 0;
    text_row = 0;
    text_buf[0] = '\0';
}

static void text_build(void) {
    if (cached_para) { rdpq_paragraph_free(cached_para); cached_para = NULL; }
    if (text_len == 0) return;
    int nbytes = text_len;
    cached_para = rdpq_paragraph_build(
        &(rdpq_textparms_t){ .style_id = ST_TEXT, .line_spacing = line_spacing },
        FONT_DEBUG_MONO, text_buf, &nbytes);
    text_len = 0;
}

static void queue_bar(int x, int y, int w, int h, color_t c) {
    if (bar_count >= MAX_BARS || w <= 0 || h <= 0) return;
    if (x + w > panel_x1 - PAD) w = panel_x1 - PAD - x;
    if (w <= 0) return;
    bars[bar_count].x = x; bars[bar_count].y = y;
    bars[bar_count].w = w; bars[bar_count].h = h;
    bars[bar_count].c = c;
    bar_count++;
}

static color_t load_color(float ms, float budget_ms) {
    float f = ms / budget_ms;
    if (f < 0.6f) return COL_GREEN;
    if (f < 0.9f) return COL_YELLOW;
    return COL_RED;
}

static void panel(int x1, int rows) {
    panel_x1 = x1 > SAFE_X1 ? SAFE_X1 : x1;
    panel_y1 = PANEL_Y0 + 2 * PAD + rows * line_h;
}

static void draw_panel(void) {
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 0xB8));
    rdpq_fill_rectangle(SAFE_X0, PANEL_Y0, panel_x1, panel_y1);
}

static void flush_bars(void) {
    if (bar_count == 0) return;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    for (int i = 0; i < bar_count; i++) {
        rdpq_set_prim_color(bars[i].c);
        rdpq_fill_rectangle(bars[i].x, bars[i].y, bars[i].x + bars[i].w, bars[i].y + bars[i].h);
    }
}

static void line(int row, color_t color, const char *fmt, ...) {
    char buf[TEXT_COLS + 8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[TEXT_COLS] = '\0';          // hard cap so text never reaches the bar column

    // Newlines up to the requested row, then the style switch and the text
    while (text_row < row && text_len < (int)sizeof(text_buf) - 2) {
        text_buf[text_len++] = '\n';
        text_row++;
    }
    int room = (int)sizeof(text_buf) - text_len;
    int n = snprintf(text_buf + text_len, room, "^%02X", style_for(color));
    if (n > 0 && n < room) text_len += n;
    for (const char *c = buf; *c && text_len < (int)sizeof(text_buf) - 3; c++) {
        if (*c == '^' || *c == '$') text_buf[text_len++] = *c;   // escape
        text_buf[text_len++] = *c;
    }
    text_buf[text_len] = '\0';
}

// Horizontal meter: grey track + filled part, on the given row
static void meter(int x, int row, float frac, color_t c) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    queue_bar(x, row_y(row) + 2, BAR_W_MAX, 5, COL_DIM);
    queue_bar(x, row_y(row) + 2, (int)(frac * BAR_W_MAX + 0.5f), 5, c);
}

// --- Pages ----------------------------------------------------------------

static void page_stats(void) {
    const EngineStats *s = stats_get();
    int rows = 9;
    panel(bar_col_x(TEXT_COLS), rows);
    int r = 0;
    line(r++, COL_HEAD, "STATS (last frame)");
    line(r++, COL_TEXT, "Tris %lu mesh %lu flr %lu",
         (unsigned long)stats_tris_total(s), (unsigned long)s->tris_mesh, (unsigned long)s->tris_floor);
    line(r++, COL_TEXT, " shd %lu prt %lu ui %lu",
         (unsigned long)s->tris_shadow, (unsigned long)s->tris_particle, (unsigned long)s->tris_ui);
    line(r++, COL_TEXT, " rejected near %lu grd %lu",
         (unsigned long)s->tris_rejected_near, (unsigned long)s->tris_rejected_guard);
    line(r++, COL_TEXT, "Mesh %lu cull %lu grp %lu/%lu",
         (unsigned long)s->mesh_draws, (unsigned long)s->mesh_culled_frustum,
         (unsigned long)s->groups_drawn, (unsigned long)s->groups_culled_backface);
    line(r++, COL_TEXT, "Tex %lu (%luB) mode %lu",
         (unsigned long)s->tex_uploads, (unsigned long)s->tex_upload_bytes, (unsigned long)s->mode_changes);
    line(r++, COL_TEXT, "Particles %lu drawn %lu",
         (unsigned long)s->particles_alive, (unsigned long)s->particles_drawn);
    line(r++, COL_TEXT, "Coll %lu pair %lu ray %lu",
         (unsigned long)s->colliders, (unsigned long)s->collision_pairs, (unsigned long)s->raycasts);
    line(r++, COL_TEXT, "Phys %lu body %lu step",
         (unsigned long)s->physics_bodies, (unsigned long)s->physics_steps);
}

static void page_profiler(float budget_ms) {
    static const ProfSlot rows[] = {
        PROF_UPDATE, PROF_SCENE_SYS, PROF_DRAW, PROF_FLOOR, PROF_OBJECTS,
        PROF_MESH_TRIS, PROF_PARTICLE_DRAW, PROF_SHADOWS, PROF_HUD, PROF_MENU,
        PROF_AUDIO, PROF_OVERLAY, PROF_WAIT_DISPLAY,
    };
    const int nrows = (int)(sizeof(rows) / sizeof(rows[0]));
    const ProfilerFrame *pf = profiler_get();
    float cpu = profiler_cpu_ms();
    int bx = bar_col_x(LABEL_COLS);
    int budget_px = (int)(budget_ms * PX_PER_MS + 0.5f);

    if (!debug_profiler_enabled()) {
        panel(bar_col_x(TEXT_COLS), 3);
        line(0, COL_HEAD, "PROFILER is off");
        line(1, COL_DIM,  "(Debug > Profiler)");
        line(2, COL_TEXT, "CPU %.2f ms", cpu);
        return;
    }

    panel(bx + budget_px + 2 * PAD + 6, nrows + 2);
    int r = 0;
    line(r++, COL_HEAD, "PROFILER avg ms");
    line(r, COL_TEXT, "%-11s%6.2f", "CPU total", cpu);
    queue_bar(bx, row_y(r) + 2, (int)(cpu * PX_PER_MS + 0.5f), 5, load_color(cpu, budget_ms));
    r++;
    for (int i = 0; i < nrows; i++) {
        ProfSlot s = rows[i];
        float ms = pf->avg_us[s] / 1000.0f;
        bool idle = (s == PROF_WAIT_DISPLAY);
        line(r, idle ? COL_DIM : COL_TEXT, "%s%-10s%6.2f",
             profiler_slot_depth(s) > 1 ? " " : "", idle ? "idle" : profiler_slot_name(s), ms);
        queue_bar(bx, row_y(r) + 2, (int)(ms * PX_PER_MS + 0.5f), 5,
                  idle ? COL_DIM : load_color(ms, budget_ms));
        r++;
    }
    // White budget marker through all bar rows
    queue_bar(bx + budget_px, row_y(1), 1, (nrows + 1) * line_h, COL_MARK);
}

static void page_memory(void) {
    const MemStats *m = memstats_get();
    int bx = bar_col_x(18);
    int rows = 7;
    panel(bx + BAR_W_MAX + 2 * PAD, rows);
    int r = 0;
    line(r++, COL_HEAD, "MEMORY");
    line(r++, COL_TEXT, "RDRAM %dKB %s", m->rdram_total / 1024, m->expansion_pak ? "+ExpPak" : "");
    line(r, COL_TEXT, "Heap %d/%dK", m->heap_used / 1024, m->heap_total / 1024);
    if (m->heap_total > 0) meter(bx, r, (float)m->heap_used / m->heap_total, COL_GREEN);
    r++;
    line(r++, COL_TEXT, " peak %dK", m->heap_peak / 1024);
    line(r++, m->heap_delta > 1024 ? COL_YELLOW : COL_TEXT, " delta %+dB", m->heap_delta);
    line(r++, COL_TEXT, "FB %dK  Z %dK", m->fb_bytes / 1024, m->zbuf_bytes / 1024);
    line(r, COL_TEXT, "Stack %d/%dK", m->stack_used_peak, m->stack_size / 1024);
    meter(bx, r, (float)m->stack_used_peak / m->stack_size,
          m->stack_used_peak * 2 > m->stack_size ? COL_RED : COL_GREEN);
}

static void page_frametime(float budget_ms) {
    frametime_get(&ft_cache, budget_ms);
    const FrameTimeStats *f = &ft_cache;

    // Histogram: 24 buckets x 5 px (4 px bar + 1 px gap) = 120 px
    const int bucket_px = 5;
    const int hist_h = 30;
    int text_right = bar_col_x(TEXT_COLS);
    int hist_right = text_x() + FRAMETIME_BUCKETS * bucket_px + PAD;
    int rows = 10;
    panel(text_right > hist_right ? text_right : hist_right, rows);

    int r = 0;
    line(r++, COL_HEAD, "FRAME TIME (%d)", f->count);
    line(r++, COL_TEXT, "FPS %.1f  1%%low %.1f", f->fps, f->low1_fps);
    line(r++, COL_TEXT, "Loop %.1f/%.1f/%.1f ms", f->min_ms, f->avg_ms, f->max_ms);
    line(r++, COL_TEXT, "p99 %.2f ms", f->p99_ms);
    line(r++, load_color(f->cpu_max_ms, budget_ms), "CPU %.1f max %.1f ov %d",
         f->cpu_avg_ms, f->cpu_max_ms, f->over_budget);
    line(r++, COL_DIM, "loop ms, 1.5/bar");

    int base_y = row_y(r) + hist_h;
    int maxc = 1;
    for (int b = 0; b < FRAMETIME_BUCKETS; b++) if (f->histogram[b] > maxc) maxc = f->histogram[b];
    for (int b = 0; b < FRAMETIME_BUCKETS; b++) {
        int h = f->histogram[b] * hist_h / maxc;
        if (f->histogram[b] > 0 && h == 0) h = 1;
        float ms = (b + 1) * FRAMETIME_BUCKET_US / 1000.0f;
        queue_bar(text_x() + bucket_px * b, base_y - h, bucket_px - 1, h, load_color(ms, budget_ms));
    }
    int mx = text_x() + (int)(budget_ms * 1000.0f / FRAMETIME_BUCKET_US * bucket_px);
    queue_bar(mx, base_y - hist_h - 2, 1, hist_h + 2, COL_MARK);
}

static void page_rsp(float budget_ms) {
    const RdpCounters *c = profiler_rdp_get();
    const RspProfile *p = profiler_rsp_get();
    int bx = bar_col_x(LABEL_COLS);
    int budget_px = (int)(budget_ms * PX_PER_MS + 0.5f);

    int shown = 0;
    if (p->available && p->valid)
        for (int i = 0; i < p->slot_count; i++) if (p->slot_ms[i] >= 0.01f) shown++;
    int rows = 6 + (p->available ? shown + 1 : 1);
    panel(bx + budget_px + 2 * PAD + 6, rows);

    int r = 0;
    line(r++, COL_HEAD, "RDP (hw counters) ms");
    if (!c->available) {
        line(r++, COL_DIM, "counters unavailable");
    } else {
        line(r, COL_TEXT, "%-11s%6.2f", "busy", c->busy_ms);
        queue_bar(bx, row_y(r) + 2, (int)(c->busy_ms * PX_PER_MS + 0.5f), 5, load_color(c->busy_ms, budget_ms));
        r++;
        line(r, COL_TEXT, "%-11s%6.2f", " pipe", c->pipe_ms);
        queue_bar(bx, row_y(r) + 2, (int)(c->pipe_ms * PX_PER_MS + 0.5f), 5, load_color(c->pipe_ms, budget_ms));
        r++;
        line(r, COL_TEXT, "%-11s%6.2f", " tmem", c->tmem_ms);
        queue_bar(bx, row_y(r) + 2, (int)(c->tmem_ms * PX_PER_MS + 0.5f), 5, load_color(c->tmem_ms, budget_ms));
        r++;
        line(r++, COL_DIM, "busy %.0f%% peak %.1f", c->busy_pct, c->busy_peak_ms);
        queue_bar(bx + budget_px, row_y(1), 1, 3 * line_h, COL_MARK);
    }
    line(r++, COL_DIM, "frame %.2f ms", c->clock_ms);

    if (!p->available) {
        line(r++, COL_DIM, "RSP split: n/a (P1.7)");
        return;
    }
    line(r++, COL_HEAD, "RSP ms (%d fr avg)", RSP_WINDOW_FRAMES);
    if (!p->valid) return;
    for (int i = 0; i < p->slot_count; i++) {
        if (p->slot_ms[i] < 0.01f) continue;
        line(r, COL_TEXT, " %-10.10s%6.2f", p->slot_name[i], p->slot_ms[i]);
        queue_bar(bx, row_y(r) + 2, (int)(p->slot_ms[i] * PX_PER_MS + 0.5f), 5,
                  load_color(p->slot_ms[i], budget_ms));
        r++;
    }
}

void overlay_draw(float budget_ms) {
    OverlayPage page = debug_overlay_page();
    if (page == OVERLAY_OFF) {
        cached_page = -1;
        return;
    }

    PROF_BEGIN(PROF_OVERLAY);
    if (char_w == 0) measure_font();

    if ((int)page != cached_page || ++cached_age >= REFRESH_FRAMES) {
        bar_count = 0;
        text_begin();
        switch (page) {
        case OVERLAY_STATS:     page_stats();              break;
        case OVERLAY_PROFILER:  page_profiler(budget_ms);  break;
        case OVERLAY_MEMORY:    page_memory();             break;
        case OVERLAY_FRAMETIME: page_frametime(budget_ms); break;
        case OVERLAY_RSP:       page_rsp(budget_ms);       break;
        default: break;
        }
        text_build();
        cached_page = (int)page;
        cached_age = 0;
    }

    draw_panel();
    if (cached_para) rdpq_paragraph_render(cached_para, text_x(), row_y(0) + ascent);
    flush_bars();

    PROF_END(PROF_OVERLAY);
}
