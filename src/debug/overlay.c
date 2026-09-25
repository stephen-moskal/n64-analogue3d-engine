#include "overlay.h"
#include <libdragon.h>
#include <stdarg.h>
#include <stdio.h>
#include "debug_menu.h"
#include "profiler.h"
#include "stats.h"
#include "memstats.h"
#include "frametime.h"
#include "../input/input.h"
#include "../engine/engine.h"
#include "../ui/text.h"
#include "../ui/ui_layer.h"

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

static int char_w = 0;        // measured advance of the UI mono font (px)
static int line_h = LINE_TARGET;
static int ascent = 7;        // baseline offset of a row's text (px)

// Text is cached (ui_layer.h): one slot per row, re-rendered only when the
// row's text or colour changes, a few rows per frame at most. The page is
// drawn every frame as one blit. Rows are recomputed at REFRESH_FRAMES.
#define OV_ROWS        20
#define OV_BUDGET       6     // rows re-rendered per frame at most
static UiLayer ov_layer;
static bool    ov_ready;
static int     row_slot[OV_ROWS];
static int     rows_written;
static int     cached_age  = 1000;
static int     cached_page = -1;
static int     panel_y1;

// Deferred bars: text and rectangles use different RDP modes, so bars are
// queued while the page is built and drawn together afterwards.
#define MAX_BARS 96        // the Input page: 18 per port
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
    rdpq_paragraph_t *p = rdpq_paragraph_build(&parms, FONT_UI_MONO, probe, &nbytes);
    char_w = (int)((p->bbox.x1 - p->bbox.x0) / 10.0f + 0.5f);
    ascent = (int)(-p->bbox.y0 + 0.5f);
    rdpq_paragraph_free(p);
    if (char_w < 4 || char_w > 12) char_w = 8;   // sanity fallback
    if (ascent < 4 || ascent > line_h - 2) ascent = line_h - 2;   // keep the text inside its row
    debugf("[overlay] UI mono font: advance %d px, ascent %d px, rows %d px\n", char_w, ascent, line_h);
}

static void layer_setup(void) {
    ui_layer_init(&ov_layer, SAFE_X1 - SAFE_X0, 2 * PAD + OV_ROWS * line_h, true);
    ui_layer_set_budget(&ov_layer, OV_BUDGET);
    for (int r = 0; r < OV_ROWS; r++) {
        int y = row_y(r) - PANEL_Y0;
        row_slot[r] = ui_layer_add(&ov_layer, text_x() - SAFE_X0, y, TEXT_COLS * char_w, line_h,
                                   y + ascent, FONT_UI_MONO, ALIGN_LEFT);
    }
    ov_ready = true;
}

static void text_begin(void) {
    rows_written = 0;
}

// Rows the page did not write this time are emptied
static void text_build(void) {
    for (int r = rows_written; r < OV_ROWS; r++) ui_layer_set(&ov_layer, row_slot[r], COL_TEXT, "");
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

    if (row < 0 || row >= OV_ROWS) return;
    ui_layer_set(&ov_layer, row_slot[row], color, buf);
    if (row + 1 > rows_written) rows_written = row + 1;
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
    int rows = 11;
    panel(bar_col_x(TEXT_COLS), rows);
    int r = 0;
    line(r++, COL_HEAD, "STATS (last frame)");
    line(r++, COL_TEXT, "Tris %lu mesh %lu flr %lu",
         (unsigned long)stats_tris_total(s), (unsigned long)s->tris_mesh, (unsigned long)s->tris_floor);
    line(r++, COL_TEXT, " shd %lu prt %lu ui %lu",
         (unsigned long)s->tris_shadow, (unsigned long)s->tris_particle, (unsigned long)s->tris_ui);
    line(r++, COL_TEXT, " rej near %lu grd %lu bf %lu",
         (unsigned long)s->tris_rejected_near, (unsigned long)s->tris_rejected_guard,
         (unsigned long)s->tris_culled_backface);
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
    line(r++, COL_TEXT, "Snd %lu voices %lu buf",
         (unsigned long)s->snd_voices, (unsigned long)s->snd_buffers);
    line(r++, COL_TEXT, "UI %lu text %lu layers",
         (unsigned long)s->ui_renders, (unsigned long)s->ui_blits);
}

// The profiler page's name for a slot: the waits read as idle time
static const char *prof_label(ProfSlot s) {
    switch (s) {
    case PROF_WAIT_DISPLAY: return "idle fb";
    case PROF_WAIT_INPUT:   return "idle input";
    case PROF_PACE:         return "idle pace";
    default:                return profiler_slot_name(s);
    }
}

// The waits are idle time, drawn dim
static bool prof_idle(ProfSlot s) {
    return s == PROF_WAIT_DISPLAY || s == PROF_WAIT_INPUT || s == PROF_PACE;
}

static void page_profiler(float budget_ms) {
    // (menu is left out: the page is hidden while the menu is open; the CSV has it)
    static const ProfSlot all_rows[] = {
        PROF_INPUT, PROF_UPDATE, PROF_SCENE_SYS, PROF_DRAW, PROF_FLOOR, PROF_OBJECTS,
        PROF_MESH_TRIS, PROF_PARTICLE_DRAW, PROF_SHADOWS, PROF_HUD,
        PROF_AUDIO, PROF_OVERLAY, PROF_WAIT_DISPLAY, PROF_WAIT_INPUT, PROF_PACE,
    };
    const ProfilerFrame *pf = profiler_get();
    // A wait that is not in use (pace without low-latency pacing, the input
    // wait when it never waits) gets no row: the panel stays clear of the HUD
    ProfSlot rows[sizeof(all_rows) / sizeof(all_rows[0])];
    int nrows = 0;
    for (unsigned i = 0; i < sizeof(all_rows) / sizeof(all_rows[0]); i++)
        if (!prof_idle(all_rows[i]) || all_rows[i] == PROF_WAIT_DISPLAY || pf->avg_us[all_rows[i]] >= 5.0f)
            rows[nrows++] = all_rows[i];
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
        bool idle = prof_idle(s);
        line(r, idle ? COL_DIM : COL_TEXT, "%s%-10s%6.2f",
             profiler_slot_depth(s) > 1 ? " " : "", prof_label(s), ms);
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
    int rows = 13;
    panel(text_right > hist_right ? text_right : hist_right, rows);

    int r = 0;
    line(r++, COL_HEAD, "FRAME TIME (%d)", f->count);
    line(r++, COL_TEXT, "FPS %.1f  1%%low %.1f", f->fps, f->low1_fps);
    line(r++, COL_TEXT, "Loop %.1f/%.1f/%.1f ms", f->min_ms, f->avg_ms, f->max_ms);
    line(r++, COL_TEXT, "p99 %.2f ms", f->p99_ms);
    line(r++, load_color(f->cpu_max_ms, budget_ms), "CPU %.1f max %.1f ov %d",
         f->cpu_avg_ms, f->cpu_max_ms, f->over_budget);
    // What reached the screen: vblanks each presented frame was shown for
    line(r++, (f->late || f->torn) ? COL_YELLOW : COL_TEXT, "Late %d torn %d of %d", f->late, f->torn, f->presents);
    line(r++, COL_TEXT, "vblanks 1:%d 2:%d 3+:%d", f->present_hist[0], f->present_hist[1],
         f->present_hist[2] + f->present_hist[3]);
    line(r++, COL_TEXT, "Input lag %.2f vbl (%d-%d)", f->lag_avg, f->lag_min, f->lag_max);
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

// --- Input page: ports, players, timing -----------------------------------
// The text refreshes at 4 Hz like every page; the button squares and stick
// dots are rebuilt every frame (input_bars), so a quick press shows.

#define IN_PORT_ROW   3                 // first port row
#define IN_TEXT_COLS 20                 // port rows: text, then the squares
#define IN_SQ          5                // button square pitch (4 px + gap)
#define IN_STICK       9                // stick box (px), inside one row

// Button squares in controller order: A B Z Start, D-pad, L R, C, X Y
static const struct { PadButton btn; int8_t gap; color_t on; } in_buttons[] = {
    {BTN_A, 0, RGBA32(0x40, 0x80, 0xFF, 0xFF)}, {BTN_B, 0, RGBA32(0x30, 0xD0, 0x40, 0xFF)},
    {BTN_Z, 0, COL_MARK}, {BTN_START, 0, COL_RED},
    {BTN_D_UP, 2, COL_MARK}, {BTN_D_DOWN, 0, COL_MARK}, {BTN_D_LEFT, 0, COL_MARK}, {BTN_D_RIGHT, 0, COL_MARK},
    {BTN_L, 2, COL_MARK}, {BTN_R, 0, COL_MARK},
    {BTN_C_UP, 2, COL_YELLOW}, {BTN_C_DOWN, 0, COL_YELLOW}, {BTN_C_LEFT, 0, COL_YELLOW}, {BTN_C_RIGHT, 0, COL_YELLOW},
    {BTN_X, 2, COL_MARK}, {BTN_Y, 0, COL_MARK},
};
#define IN_SQ_W (16 * IN_SQ + 8)          // squares plus the four group gaps

static const char *accessory_short(const PadState *pad) {
    switch (pad->accessory) {
    case PAD_ACC_RUMBLE_PAK:     return "Rmb";
    case PAD_ACC_CONTROLLER_PAK: return "Pak";
    case PAD_ACC_TRANSFER_PAK:   return "Xfr";
    case PAD_ACC_BIO_SENSOR:     return "Bio";
    case PAD_ACC_SNAP_STATION:   return "Snp";
    case PAD_ACC_UNKNOWN:        return "?";
    default:                     return pad->rumble ? "Rmb" : "";
    }
}

static void page_input(void) {
    const InputTiming *t = input_timing();
    frametime_get(&ft_cache, engine_frame_budget_ms());
    int bx = bar_col_x(IN_TEXT_COLS);
    int rows = IN_PORT_ROW + PAD_PORTS + ACTION_PLAYERS + 1;
    panel(bx + IN_SQ_W + PAD + IN_STICK + PAD, rows);

    int r = 0;
    static const char *const sync_names[] = {"auto", "fresh", "latest"};
    line(r++, COL_HEAD, "INPUT %s, %s", sync_names[input_sync()],
         engine_pacing() == ENGINE_PACING_LOW_LATENCY ? "low latency" : "ahead");
    line(r++, COL_TEXT, "Lag %.2f vbl (%d-%d) %.0f ms", ft_cache.lag_avg, ft_cache.lag_min,
         ft_cache.lag_max, ft_cache.lag_avg * (1000.0f / 60.0f));
    line(r++, t->timeouts ? COL_YELLOW : COL_TEXT, "SI %.2f wait %.2f/%.2f", t->read_avg_us / 1000.0f,
         t->wait_avg_us / 1000.0f, t->wait_max_us / 1000.0f);

    for (int port = 0; port < PAD_PORTS; port++) {
        const PadState *pad = input_pad(port);
        if (pad->style == PAD_NONE)
            line(r++, COL_DIM, "%d --", port + 1);
        else
            line(r++, COL_TEXT, "%d %-5s%-4s%+4d%+4d", port + 1, pad_style_name(pad->style),
                 accessory_short(pad), pad->stick_x, pad->stick_y);
    }

    // Players: port, then the context stack from the top
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        char chain[TEXT_COLS + 1];
        int n = 0;
        chain[0] = '\0';
        for (int i = 0; i < action_context_count(p) && n < TEXT_COLS; i++)
            n += snprintf(chain + n, sizeof(chain) - n, "%s%s", i ? ">" : "", action_context_at(p, i)->name);
        int port = action_port(p);
        line(r++, action_connected(p) ? COL_TEXT : COL_DIM, "P%d %c %s", p + 1,
             port >= 0 ? '1' + port : '-', chain);
    }

    // Player 1's active actions, by name
    char held[TEXT_COLS + 1];
    int n = snprintf(held, sizeof(held), "P1:");
    for (int a = 0; a < ACTION_MAX && n < TEXT_COLS; a++)
        if (action_held(0, (ActionId)a))
            n += snprintf(held + n, sizeof(held) - n, " %s", action_name((ActionId)a));
    line(r++, COL_TEXT, "%s", held);
}

// Every frame: a square per button (lit while held) and the stick's position
static void input_bars(void) {
    int bx = bar_col_x(IN_TEXT_COLS);
    for (int port = 0; port < PAD_PORTS; port++) {
        const PadState *pad = input_pad(port);
        if (pad->style == PAD_NONE) continue;
        int y = row_y(IN_PORT_ROW + port) + 2;
        int x = bx;
        for (unsigned i = 0; i < sizeof(in_buttons) / sizeof(in_buttons[0]); i++) {
            x += in_buttons[i].gap;
            bool on = pad->held & PAD_BIT(in_buttons[i].btn);
            queue_bar(x, y, IN_SQ - 1, 6, on ? in_buttons[i].on : COL_DIM);
            x += IN_SQ;
        }
        // Stick: +-80 raw fills the box
        int sx = bx + IN_SQ_W + PAD, sy = row_y(IN_PORT_ROW + port);
        queue_bar(sx, sy, IN_STICK, IN_STICK, COL_DIM);
        int dx = pad->stick_x * (IN_STICK / 2) / 80, dy = -pad->stick_y * (IN_STICK / 2) / 80;
        if (dx < -IN_STICK / 2) dx = -IN_STICK / 2;
        if (dx > IN_STICK / 2 - 1) dx = IN_STICK / 2 - 1;
        if (dy < -IN_STICK / 2) dy = -IN_STICK / 2;
        if (dy > IN_STICK / 2 - 1) dy = IN_STICK / 2 - 1;
        queue_bar(sx + IN_STICK / 2 + dx, sy + IN_STICK / 2 + dy, 2, 2, COL_GREEN);
    }
}

void overlay_draw(float budget_ms) {
    OverlayPage page = debug_overlay_page();
    if (page == OVERLAY_OFF) {
        cached_page = -1;
        if (ov_ready) ui_layer_free(&ov_layer);   // ~120 KB back while no page is shown
        return;
    }

    PROF_BEGIN(PROF_OVERLAY);
    if (char_w == 0) measure_font();
    if (!ov_ready) layer_setup();

    if ((int)page != cached_page || ++cached_age >= REFRESH_FRAMES) {
        bar_count = 0;
        text_begin();
        switch (page) {
        case OVERLAY_STATS:     page_stats();              break;
        case OVERLAY_PROFILER:  page_profiler(budget_ms);  break;
        case OVERLAY_MEMORY:    page_memory();             break;
        case OVERLAY_FRAMETIME: page_frametime(budget_ms); break;
        case OVERLAY_RSP:       page_rsp(budget_ms);       break;
        case OVERLAY_INPUT:     page_input();              break;
        default: break;
        }
        text_build();
        cached_page = (int)page;
        cached_age = 0;
    }
    if (page == OVERLAY_INPUT) {
        bar_count = 0;
        input_bars();
    }

    draw_panel();
    ui_layer_draw(&ov_layer, SAFE_X0, PANEL_Y0, panel_y1 - PANEL_Y0);
    flush_bars();

    PROF_END(PROF_OVERLAY);
}
