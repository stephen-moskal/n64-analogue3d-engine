#include "benchmark_scene.h"
#include <libdragon.h>
#include <string.h>
#include <malloc.h>
#include "../render/mesh.h"
#include "../render/mesh_defs.h"
#include "../render/texture.h"
#include "../render/particle.h"
#include "../render/shadow.h"
#include "../render/floor.h"
#include "../render/atmosphere.h"
#include "../input/input.h"
#include "../ui/text.h"
#include "../ui/menu.h"
#include "../ui/menu_view.h"
#include "../ui/ui_hud.h"
#include "../ui/textbox.h"
#include "../dialog/dialog.h"
#include "../debug/engine_debug.h"
#include "../engine/engine_config.h"
#include "../engine/engine.h"
#include "../engine/hot.h"
#include "../engine/util.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "../debug/frametime.h"
#include "../debug/memstats.h"
#include "../audio/audio.h"

// ------------------------------------------------------------------------
// Configuration
// ------------------------------------------------------------------------

#define WARMUP_FRAMES    60
#define MEASURE_FRAMES  240
#define MAX_STEPS        32
#define MAX_INSTANCES    64
#define GRID_SPACING    110.0f
#define NUM_TEX_BOXES     8        // one single-texture box mesh per texture slot 0..7
#define CAMERA_SPIN     0.2f       // radians per second of *measured* time (frame-locked)

typedef struct {
    BenchKind kind;
    int       param;
} BenchStep;

static const char *kind_names[BENCH_KIND_COUNT] = {
    "all", "objects", "particles", "lights", "textures", "shadows", "fillrate", "overload", "layout", "audio", "ui",
    "latency",
};

// One-line description shown under the status line (param is substituted)
static const char *kind_desc[BENCH_KIND_COUNT] = {
    "Empty scene: engine baseline",
    "%d flat pillars, 32 tris each",
    "%d: blend*1000+particles (0 additive, 1 alpha, 2 both)",
    "16 pillars lit by %d point lights",
    "16 boxes, %d distinct textures",
    "16 pillars, shadow mode %d:off/blob/proj",
    "%d full-screen blended layers",
    "+%d ms CPU burn, floor + 16 pillars",
    "%d: variant*1000+pillars (0 shared, 1-4 at 0/2/4/6 KB, 5 reversed)",
    "%d: codec*100+poll*10+sfx (0 none 1 raw 2 vadpcm 3 opus)",
    "%d: menu 0x-1x, style 2x-3x, HUD 4x, dlg 5x",
    "%d: latency*100+burn ms (0 classic, 1 low, 2 lowest)",
};

static BenchKind  configured_kind = BENCH_ALL;
static BenchStep  steps[MAX_STEPS];
static int        step_count;
static int        step_index;
static int        step_frame;          // frames since the step started
static bool       finished;
static bool       aborted;

// D32: for several seconds after a console reset the A3D runs CPU and RDP
// ~40 % slower. A run that starts within BOOT_SETTLE_S of reset (make BENCH=1)
// first holds its first step's scene, unmeasured, until then.
#define BOOT_SETTLE_S   15
static bool       settling;
static uint64_t   run_start_ticks;

// Per-step accumulators (measured frames only)
static uint64_t   acc_tris, acc_uploads;
static uint32_t   acc_frames;

// Scene content
static Mesh       tex_boxes[NUM_TEX_BOXES];
static int        instance_count;
static vec3_t     instance_pos[MAX_INSTANCES];
static int        emitters[4] = {-1, -1, -1, -1};
static int        fill_layers;
static bool       saved_fog, saved_sky;
static int        burn_ms;             // OVERLOAD: extra CPU time per frame
static bool       draw_floor;
static void      *draw_frame;          // stack frame of bench_draw (data-layout row, D26)
// RDRAM placement (D37): libdragon's command-buffer write pointer when the
// first frame is drawn, and the framebuffers the benchmark drew into
static const volatile void *draw_rspq;
static const void *draw_fb[3];
static int         draw_fb_count;
// The RDP's two 64 KB command buffers (libdragon rspq.c; not in its headers)
extern void *rspq_rdp_dynamic_buffers[2];
static bool       layout_logged;

// LAYOUT (D26): copies of the pillar whose geometry sits at chosen D-cache
// "colours" (address modulo the 8 KB direct-mapped D-cache), plus one with
// reversed winding, drawn in interleaved steps. Variant 0 is the shared
// pillar; variants 1-5 use layout_copy[0..4]. The pool owns the copies' data,
// so they are never passed to mesh_cleanup().
#define LAYOUT_COPIES 5
static const int  layout_offset[LAYOUT_COPIES]  = {0x0000, 0x0800, 0x1000, 0x1800, 0x2000};
static const bool layout_reverse[LAYOUT_COPIES] = {false, false, false, false, true};
static Mesh       layout_copy[LAYOUT_COPIES];
static void      *layout_pool;         // 16 KB, 8 KB aligned
static int        layout_variant;

// AUDIO: music encoding × poll point × sound-effect load, over a demo-like
// render load (floor + 16 pillars) so the mixer competes with rdpq for the RSP
static bool         audio_sfx;         // retrigger sound effects on every voice
static const SoundId audio_track[4] = {SOUND_NONE, BGM_BENCH_RAW, BGM_DEMO, BGM_BENCH_OPUS};
static SndPollPoint saved_poll_point;

// UI: a copy of the Start menu, open, drawn by a view that renders every
// text element every frame (mode 0, the pre-S5 cost) or re-renders only what
// changed (mode 1). Input is scripted: none, a cursor move every 8 frames, a
// value change every 2 frames, a tab switch every 30 frames, or closing
// for 30 frames and reopening (the cached text must survive it).
extern Menu start_menu;
static Menu     ui_menu;
static MenuView ui_view[2];
static bool     ui_views_ready;
static bool     ui_active;
static int      ui_mode, ui_input;
// Steps 20 / 30: the cached menu in the Classic / Minimal style (view 2).
// Steps 40 / 41: a demo-like HUD (title + six readouts whose values change
// every frame) drawn direct every frame, or cached at ~6 Hz, 2 lines a frame.
static MenuView ui_style_view;
static HudPanel ui_hud[2][2];          // [cached][top, bottom]
// Steps 50 / 51: the demo conversation in the text box, driven by a scripted
// reader: 50 waits ~0.4 s on every finished page and 1 s on a choice list
// (reading pace), 51 presses
// A every 3 frames (skipping: a new page is laid out every few frames, the
// worst case for the page render). Choices rotate so every branch is shown.
static int      ui_hud_line[2][7];
static bool     ui_hud_active, ui_hud_cached;
static DialogBank  *ui_dlg_bank;
static DialogRunner ui_dlg_runner;
static TextBox      ui_dlg_box;
static bool         ui_dlg_active, ui_dlg_skip;
static int          ui_dlg_wait, ui_dlg_pick;

static bool ui_dlg_check(const char *cond, void *ctx) {
    (void)ctx;
    return cond[0] == '!';             // nothing is "true" in the bench: !name passes
}
static bool ui_dlg_var(const char *name, char *out, int len, void *ctx) {
    (void)ctx; (void)name;
    snprintf(out, len, "Bench");
    return true;
}
static void ui_dlg_restart(void) {
    DialogHooks hooks = { NULL, ui_dlg_check, ui_dlg_var, NULL };
    if (ui_dlg_bank && dialog_start(&ui_dlg_runner, ui_dlg_bank, "intro", &hooks))
        textbox_open(&ui_dlg_box, &ui_dlg_runner);
}

static void ui_hud_setup(void) {
    for (int c = 0; c < 2; c++) {
        HudPanel *t = &ui_hud[c][0], *b = &ui_hud[c][1];
        hud_panel_init(t, 20, 10, 280, 14, c, c ? 10 : 0, c ? 2 : 0);
        hud_panel_init(b, 0, 186, 320, 38, c, c ? 10 : 0, c ? 2 : 0);
        ui_hud_line[c][0] = hud_panel_line(t, 20, 20, 280, FONT_UI_VAR, ALIGN_CENTER);
        for (int i = 0; i < 3; i++) {
            ui_hud_line[c][1 + i] = hud_panel_line(b, 4, 196 + 12 * i, 172, FONT_UI_MONO, ALIGN_LEFT);
            ui_hud_line[c][4 + i] = hud_panel_line(b, 176, 196 + 12 * i, 140, FONT_UI_MONO, ALIGN_RIGHT);
        }
    }
}

static void ui_hud_draw(int frame) {
    int c = ui_hud_cached ? 1 : 0;
    const UiStyle *st = &ui_style_debug;
    HudPanel *t = &ui_hud[c][0], *b = &ui_hud[c][1];
    if (hud_panel_due(t)) hud_panel_set(t, ui_hud_line[c][0], st->hud_title, "SMozN64 Dev Engine [debug]");
    if (hud_panel_due(b)) {
        int *l = ui_hud_line[c];
        hud_panel_setf(b, l[1], st->hud_text, "OBJ:%d/%d VIS:%d", 9, 64, 5 + (frame / 30) % 4);
        hud_panel_setf(b, l[2], st->hud_text, "T:%d U:%d COL:%d RAY:%d", 400 + frame % 60, 6, 5, 300 + frame % 90);
        hud_panel_setf(b, l[3], st->hud_accent, "FPS: 60 CPU:%d.%dms", 10 + frame % 3, frame % 10);
        hud_panel_setf(b, l[4], st->hud_accent, "SEL:Pillar L");
        hud_panel_setf(b, l[5], st->hud_text, "CAM:ORBITAL");
        hud_panel_setf(b, l[6], st->hud_text, "XYZ:%d,%d,%d", frame % 400 - 200, 150, 400 - frame % 300);
    }
    hud_panel_draw(t, st);
    hud_panel_draw(b, st);
}

// ------------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------------

const char *benchmark_kind_name(BenchKind kind) {
    return (kind >= 0 && kind < BENCH_KIND_COUNT) ? kind_names[kind] : "?";
}

void benchmark_scene_configure(BenchKind kind) {
    configured_kind = kind;
}

bool benchmark_scene_finished(void) {
    return finished;
}

static void add_step(BenchKind kind, int param) {
    if (step_count < MAX_STEPS) steps[step_count++] = (BenchStep){kind, param};
}

static void build_steps(BenchKind which) {
    step_count = 0;
    bool all = (which == BENCH_ALL);
    if (all) add_step(BENCH_ALL, 0);                 // empty scene reference
    if (all || which == BENCH_OBJECTS) {
        static const int n[] = {8, 16, 24, 32, 48, 64};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_OBJECTS, n[i]);
    }
    if (all || which == BENCH_PARTICLES) {
        // param = blend * 1000 + particles: additive, then 128 alpha-blended,
        // then 64 of each (two batches, one blender change: D13)
        static const int n[] = {32, 64, 96, 128, 1128, 2128};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_PARTICLES, n[i]);
    }
    if (all || which == BENCH_LIGHTS) {
        static const int n[] = {0, 1, 2, 4};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_LIGHTS, n[i]);
    }
    if (all || which == BENCH_TEXTURES) {
        static const int n[] = {1, 2, 4, 8};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_TEXTURES, n[i]);
    }
    if (all || which == BENCH_SHADOWS) {
        for (int m = 0; m <= 2; m++) add_step(BENCH_SHADOWS, m);   // off / blob / projected
    }
    if (all || which == BENCH_FILLRATE) {
        static const int n[] = {1, 2, 4, 8};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_FILLRATE, n[i]);
    }
    if (which == BENCH_OVERLOAD) {
        // Deliberate overruns: the demo-like load is ~8 ms, so +10 stays under
        // 16.7 ms, +14/+17 straddle it, +20/+25 always overrun.
        static const int n[] = {0, 10, 14, 17, 20, 25};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_OVERLOAD, n[i]);
    }
    if (which == BENCH_LAYOUT) {
        // Interleaved so drift (heat, background load) hits every variant alike
        static const int n[] = {32, 64};
        for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++)
            for (int v = 0; v <= LAYOUT_COPIES; v++) add_step(BENCH_LAYOUT, v * 1000 + n[i]);
    }
    if (which == BENCH_AUDIO) {
        // param = codec*100 + poll point*10 + sfx. Codec: 0 no music, 1 raw,
        // 2 VADPCM (the demo track), 3 Opus. Poll point: SndPollPoint.
        // Opus first: the raw track then takes the other music slot and the
        // VADPCM track the one Opus used, so a codec change on one channel is
        // exercised as well (audio.c, channel_play)
        static const int p[] = {
              20,                         // silence
             300, 320,                    // Opus (only with SND_OPUS=1)
             100, 120,                    // raw
             200, 210, 220,               // VADPCM at each poll point
             201, 221,                    // VADPCM + a new sound effect every 4 frames
        };
        for (unsigned i = 0; i < sizeof(p) / sizeof(p[0]); i++) {
            int codec = p[i] / 100;
#if !defined(SND_ENABLE_OPUS) || !SND_ENABLE_OPUS
            if (codec == 3) continue;             // no decoder linked (SND_OPUS=0)
#endif
            // The other encodings are packed into debug ROMs only
            if (codec != 0 && !snd_available(audio_track[codec])) continue;
            add_step(BENCH_AUDIO, p[i]);
        }
    }
    if (which == BENCH_UI) {
        static const int p[] = {0, 10, 1, 11, 2, 12, 3, 13, 4, 14, 20, 30, 40, 41, 50, 51};
        for (unsigned i = 0; i < sizeof(p) / sizeof(p[0]); i++) add_step(BENCH_UI, p[i]);
    }
    if (which == BENCH_LATENCY) {
        // param = latency setting*100 + CPU burn (ms) over the demo-like load
        // (floor + 16 pillars, ~8 ms): each setting light, loaded, near the budget
        static const int burn[] = {0, 4, 8};
        for (unsigned i = 0; i < sizeof(burn) / sizeof(burn[0]); i++)
            for (int l = 0; l <= 2; l++) add_step(BENCH_LATENCY, l * 100 + burn[i]);
    }
}

// Lay out n instances on a square grid centred on the origin
static void layout_grid(int n) {
    if (n > MAX_INSTANCES) n = MAX_INSTANCES;
    int side = 1;
    while (side * side < n) side++;
    float half = (side - 1) * GRID_SPACING * 0.5f;
    for (int i = 0; i < n; i++) {
        int gx = i % side, gz = i / side;
        instance_pos[i] = (vec3_t){gx * GRID_SPACING - half, 0.0f, gz * GRID_SPACING - half};
    }
    instance_count = n;
}

// Box mesh with all six faces using one texture slot
static void build_tex_box(Mesh *m, int slot) {
    static const float n6[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    static const float v6[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{ 1,-1,-1},{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1}},
        {{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1},{-1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{ 1,-1, 1},{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}},
    };
    static const float uv4[4][2] = {{0,32},{32,32},{32,0},{0,0}};
    mesh_init(m);
    int mat = mesh_add_material(m, (Material){
        .type = MATERIAL_TEXTURED, .texture_slot = slot, .base_color = {255, 255, 255},
    });
    for (int f = 0; f < 6; f++) {
        mesh_begin_group(m, mat);
        int base = m->vertex_count;
        for (int v = 0; v < 4; v++) {
            mesh_add_vertex(m, (MeshVertex){
                .position = {v6[f][v][0], v6[f][v][1], v6[f][v][2]},
                .normal   = {n6[f][0], n6[f][1], n6[f][2]},
                .uv       = {uv4[v][0], uv4[v][1]},
            });
        }
        mesh_add_triangle(m, base, base + 1, base + 2);
        mesh_add_triangle(m, base, base + 2, base + 3);
        mesh_end_group(m);
    }
    mesh_finalize(m);
}

static const ParticleEmitterDef bench_particles = {
    .burst_count  = 0,
    .spawn_rate   = 8.0f,               // overwritten per step
    .lifetime_min = 0.9f,
    .lifetime_max = 0.9f,
    .velocity_min = {-30.0f, 40.0f, -30.0f},
    .velocity_max = { 30.0f, 90.0f,  30.0f},
    .gravity      = {0.0f, -20.0f, 0.0f},
    .drag         = 0.2f,
    .color_start  = {255, 200, 80, 255},
    .color_end    = {200, 60, 20, 0},
    .scale_start  = 7.0f,
    .scale_end    = 3.0f,
    .spawn_shape  = PARTICLE_SPAWN_SPHERE,
    .spawn_radius = 20.0f,
    .blend_mode   = PARTICLE_BLEND_ADDITIVE,
};

// The particle system keeps a pointer to the definition, so it must outlive
// the emitters (a stack copy here produced no particles at all).
static ParticleEmitterDef step_particles, step_particles_alpha;

static void destroy_emitters(void) {
    for (int i = 0; i < 4; i++) {
        if (emitters[i] >= 0) particle_emitter_destroy(emitters[i]);
        emitters[i] = -1;
    }
}

// Configure the scene for the current step
static void setup_step(Scene *scene) {
    const BenchStep *st = &steps[step_index];
    LightConfig *L = &scene->lighting;

    destroy_emitters();
    fill_layers = 0;
    instance_count = 0;
    burn_ms = 0;
    draw_floor = false;
    ui_active = false;
    ui_hud_active = false;
    ui_dlg_active = false;
    lighting_init(L);
    L->point_light_count = 0;
    for (int i = 0; i < MAX_POINT_LIGHTS; i++) L->point_lights[i].active = false;
    L->shadow.mode = SHADOW_OFF;
    input_set_sync(INPUT_SYNC_AUTO);            // the defaults; LATENCY steps set their own
    engine_set_pacing(ENGINE_PACING_THROUGHPUT);

    switch (st->kind) {
    case BENCH_OBJECTS:
        layout_grid(st->param);
        break;
    case BENCH_PARTICLES: {
        // Four continuous emitters; alive count settles near rate * lifetime.
        // Blend (param / 1000): 0 all additive, 1 all alpha, 2 two of each
        int blend = st->param / 1000;
        step_particles = bench_particles;
        int per = (st->param % 1000) / 4;
        step_particles.spawn_rate = per / step_particles.lifetime_max;
        step_particles_alpha = step_particles;
        step_particles_alpha.blend_mode = PARTICLE_BLEND_ALPHA;
        static const vec3_t corners[4] = {{-150,0,-150},{150,0,-150},{-150,0,150},{150,0,150}};
        for (int i = 0; i < 4; i++) {
            bool alpha = blend == 1 || (blend == 2 && i < 2);
            emitters[i] = particle_emitter_create(alpha ? &step_particles_alpha : &step_particles,
                                                  corners[i], per);
            if (emitters[i] >= 0) particle_emitter_set_active(emitters[i], true);
        }
        break;
    }
    case BENCH_LIGHTS: {
        layout_grid(16);
        static const vec3_t lp[4] = {{-200,120,-200},{200,120,-200},{-200,120,200},{200,120,200}};
        static const float lc[4][3] = {{1.0f,0.2f,0.1f},{0.1f,1.0f,0.2f},{0.2f,0.3f,1.0f},{1.0f,1.0f,0.2f}};
        for (int i = 0; i < st->param && i < MAX_POINT_LIGHTS; i++) {
            L->point_lights[i] = (PointLight){
                .position = lp[i], .color = {lc[i][0], lc[i][1], lc[i][2]},
                .intensity = 4.0f, .radius = 700.0f, .active = true,
            };
        }
        // Dim sun and ambient so the point lights dominate visually
        L->sun_intensity = 0.3f;
        L->ambient[0] = L->ambient[1] = L->ambient[2] = 0.1f;
        L->point_light_count = st->param;
        break;
    }
    case BENCH_TEXTURES:
        layout_grid(16);
        break;
    case BENCH_SHADOWS:
        layout_grid(16);
        L->shadow.mode = (ShadowMode)st->param;
        break;
    case BENCH_FILLRATE:
        fill_layers = st->param;
        break;
    case BENCH_OVERLOAD:
        layout_grid(16);
        draw_floor = true;
        burn_ms = st->param;
        break;
    case BENCH_LAYOUT:
        layout_grid(st->param % 1000);
        layout_variant = (layout_pool != NULL) ? st->param / 1000 : 0;
        break;
    case BENCH_AUDIO: {
        layout_grid(16);
        draw_floor = true;
        int codec = st->param / 100;
        snd_set_poll_point((SndPollPoint)((st->param / 10) % 10));
        audio_sfx = (st->param % 10) != 0;
        if (codec == 0) snd_music_stop(0.0f);
        else            snd_music_play(audio_track[codec], 0.0f);
        break;
    }
    case BENCH_LATENCY: {
        // The demo's Latency choices (settings.h, LatencyChoice)
        int l = st->param / 100;
        layout_grid(16);
        draw_floor = true;
        burn_ms = st->param % 100;
        input_set_sync(l == 0 ? INPUT_SYNC_LATEST : l == 1 ? INPUT_SYNC_AUTO : INPUT_SYNC_FRESH);
        engine_set_pacing(l == 2 ? ENGINE_PACING_LOW_LATENCY : ENGINE_PACING_THROUGHPUT);
        break;
    }
    case BENCH_UI:
        layout_grid(16);
        draw_floor = true;
        ui_dlg_active = st->param >= 50;
        ui_dlg_skip = st->param == 51;
        if (ui_dlg_active) { ui_dlg_wait = ui_dlg_pick = 0; ui_dlg_restart(); break; }
        ui_hud_active = st->param >= 40;
        ui_hud_cached = st->param == 41;
        if (ui_hud_active) break;
        ui_menu = start_menu;                 // a copy: the real settings stay untouched
        menu_open(&ui_menu);
        ui_mode = st->param / 10;
        ui_input = st->param % 10;
        ui_active = true;
        if (ui_mode >= 2) menu_view_set_style(&ui_style_view, ui_mode == 2 ? &ui_style_classic : &ui_style_minimal);
        break;
    default:
        break;   // BENCH_ALL step 0: empty reference scene
    }

    step_frame = 0;
    acc_tris = acc_uploads = 0;
    acc_frames = 0;
}

// Addresses of the hot render data, once per run: with the 8 KB direct-mapped
// D-cache, vertex data that aliases the render stack costs misses on every
// triangle (roadmap D26). Index = address bits 4..12.
static void log_layout(void) {
    if (layout_logged || !draw_frame) return;
    layout_logged = true;
    const Mesh *pillar = mesh_defs_get_pillar();
    (void)pillar;   // debugf is compiled out in release
    debugf("BENCH_LAYOUT,draw_frame=%p,pillar_vtx=%p,pillar_vtx_bytes=%d,pillar_idx=%p,pillar_idx_bytes=%d,pillar_mesh=%p\n",
           draw_frame, (void *)pillar->vertices, (int)(pillar->vertex_count * sizeof(MeshVertex)),
           (void *)pillar->indices, (int)(pillar->index_count * sizeof(uint16_t)), (void *)pillar);
    const surface_t *zb = engine_zbuf();
    (void)zb;
    debugf("BENCH_LAYOUT,rspq=%p,rdp_buf0=%p,rdp_buf1=%p,zbuf=%p,fb0=%p,fb1=%p,fb2=%p\n",
           (const void *)draw_rspq, rspq_rdp_dynamic_buffers[0], rspq_rdp_dynamic_buffers[1],
           zb ? zb->buffer : NULL, draw_fb[0], draw_fb[1], draw_fb[2]);
    if (layout_pool) {
        for (int c = 0; c < LAYOUT_COPIES; c++) {
            debugf("BENCH_LAYOUT,variant=%d,vtx=%p,idx=%p,mesh=%p,reversed=%d\n", c + 1,
                   (void *)layout_copy[c].vertices, (void *)layout_copy[c].indices,
                   (void *)&layout_copy[c], (int)layout_reverse[c]);
        }
    }
}

static void build_layout_copies(void) {
    const Mesh *src = mesh_defs_get_pillar();
    size_t vbytes = sizeof(MeshVertex) * (size_t)src->vertex_count;
    size_t ibytes = sizeof(uint16_t) * (size_t)src->index_count;
    if (vbytes + ibytes > 0x800) return;           // each copy must fit its 2 KB slot
    layout_pool = memalign(0x2000, 0x4000);
    if (!layout_pool) return;
    for (int c = 0; c < LAYOUT_COPIES; c++) {
        Mesh *m = &layout_copy[c];
        *m = *src;                                  // materials, groups, bounds, flags
        char *data = (char *)layout_pool + layout_offset[c];
        memcpy(data, src->vertices, vbytes);
        uint16_t *idx = (uint16_t *)(data + vbytes);
        for (int i = 0; i < src->index_count; i += 3) {
            idx[i]     = src->indices[i];
            idx[i + 1] = src->indices[i + (layout_reverse[c] ? 2 : 1)];
            idx[i + 2] = src->indices[i + (layout_reverse[c] ? 1 : 2)];
        }
        m->vertices = (MeshVertex *)data;
        m->indices = idx;
        m->block = NULL;
    }
}

static void free_layout_copies(void) {
    free(layout_pool);
    layout_pool = NULL;
    memset(layout_copy, 0, sizeof(layout_copy));
    layout_variant = 0;
}

static void finish_step(void) {
    const BenchStep *st = &steps[step_index];
    log_layout();
    FrameTimeStats ft;
    frametime_get(&ft, engine_frame_budget_ms());   // presents are judged against the current cap
    const RdpCounters *rdp = profiler_rdp_get();
    const MemStats *mem = memstats_get();
    float tris = acc_frames ? (float)acc_tris / acc_frames : 0.0f;
    float ups  = acc_frames ? (float)acc_uploads / acc_frames : 0.0f;
    (void)st; (void)rdp; (void)mem; (void)tris; (void)ups;   // debugf is compiled out in release

    debugf("BENCH,%s,%d,%d,%d,%.1f,%.2f,%.2f,%.1f,%.2f,%.2f,%.2f,%.1f,%.0f,%.1f,%d\n",
           kind_names[st->kind], step_index, st->param, ft.count, ft.fps, ft.avg_ms,
           ft.p99_ms, ft.low1_fps, ft.cpu_avg_ms, ft.cpu_max_ms,
           rdp->available ? rdp->busy_ms : -1.0f, rdp->available ? rdp->busy_pct : -1.0f,
           tris, ups, mem->heap_used / 1024);

    // What reached the screen during the step: vblanks per presented frame
    // (frametime.h; the last 256 presents of the measured frames)
    debugf("BENCH_PRESENT,%s,%d,%d,%d,%u,%u,%u,%u,%d,%.3f,%d,%d,%.3f,%d,%d\n",
           kind_names[st->kind], step_index, st->param, ft.presents,
           ft.present_hist[0], ft.present_hist[1], ft.present_hist[2], ft.present_hist[3],
           ft.late, ft.present_avg_vblanks, ft.torn, ft.torn_worst_halfline,
           ft.lag_avg, ft.lag_min, ft.lag_max);

    // CPU breakdown of the step (profiler moving averages, ~32 frames), so a
    // regression can be pinned to a stage of mesh_draw. Separate row type:
    // BENCH rows and bench_compare.py are unchanged.
    if (g_prof_on) {
        const ProfilerFrame *pf = profiler_get();
        (void)pf;
        debugf("BENCH_PROF,%s,%d,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n",
               kind_names[st->kind], step_index, st->param,
               pf->avg_us[PROF_UPDATE], pf->avg_us[PROF_DRAW], pf->avg_us[PROF_OBJECTS],
               pf->avg_us[PROF_MESH_CULL], pf->avg_us[PROF_MESH_LIGHT], pf->avg_us[PROF_MESH_TRIS],
               pf->avg_us[PROF_AUDIO], pf->avg_us[PROF_MENU], pf->avg_us[PROF_HUD],
               pf->avg_us[PROF_DIALOG], pf->avg_us[PROF_INPUT], pf->avg_us[PROF_WAIT_INPUT],
               pf->avg_us[PROF_PARTICLE_DRAW], pf->avg_us[PROF_RSP_WAIT]);
    }

    // The controller read over the measured frames (input.h): how many frames
    // got the latest vblank's read, when it came in after its vblank (-1: no
    // read timed), the frame's wait for it, the worst wait, the timeouts
    const InputTiming *it = input_timing();
    float fresh_pct = it->frames ? 100.0f * it->fresh_frames / it->frames : 0.0f;
    float read_us = it->reads_timed ? it->read_sum_us / it->reads_timed : -1.0f;
    float wait_us = it->frames ? it->wait_sum_us / it->frames : 0.0f;
    (void)it; (void)fresh_pct; (void)read_us; (void)wait_us;
    debugf("BENCH_INPUT,%s,%d,%d,%s,%s,%.1f,%.0f,%.0f,%.0f,%lu,%lu\n",
           kind_names[st->kind], step_index, st->param,
           input_sync() == INPUT_SYNC_AUTO ? "auto" : input_sync() == INPUT_SYNC_FRESH ? "fresh" : "latest",
           engine_pacing() == ENGINE_PACING_LOW_LATENCY ? "low_latency" : "throughput",
           fresh_pct, read_us, wait_us, it->wait_max_us, (unsigned long)it->timeouts,
           (unsigned long)it->auto_skips);
}

// ------------------------------------------------------------------------
// Scene callbacks
// ------------------------------------------------------------------------

// Start (any player) aborts the run. Consuming, not modal: the debug
// context below it keeps the D-Up / D-Down shortcuts during a run.
static const ActionBinding bench_bindings[] = { BIND(ACTION_UI_START, BTN_START) };
static ActionContext bench_ctx;
static InputSync     saved_sync;
static EnginePacing  saved_pacing;

static void bench_init(Scene *scene) {
    finished = false;
    aborted = false;
    draw_frame = NULL;
    draw_rspq = NULL;
    draw_fb_count = 0;
    for (int i = 0; i < 3; i++) draw_fb[i] = NULL;
    layout_logged = false;
    build_steps(configured_kind);
    step_index = 0;

    texture_init();                                  // slots 0-5
    texture_load_slot(6, "rom:/marker.sprite");
    texture_load_slot(7, "rom:/tree.sprite");
    for (int i = 0; i < NUM_TEX_BOXES; i++) build_tex_box(&tex_boxes[i], i);
    mesh_defs_init();
    if (configured_kind == BENCH_LAYOUT) build_layout_copies();
    if (configured_kind == BENCH_UI && !ui_views_ready) {
        menu_view_init(&ui_view[0], &ui_style_debug, false);
        menu_view_init(&ui_view[1], &ui_style_debug, true);
        menu_view_init(&ui_style_view, &ui_style_classic, true);
        ui_hud_setup();
        ui_dlg_bank = dialog_bank_load("rom:/dialog/demo.dlg");
        textbox_init(&ui_dlg_box);
        ui_views_ready = true;
    }
    saved_poll_point = snd_get_poll_point();
    audio_sfx = false;
    if (configured_kind == BENCH_AUDIO) {
        // Audible, so the music really decodes (the demo reapplies its Sound tab on return)
        snd_set_volume(SND_VOL_MASTER, 1.0f, 0.0f);
        snd_set_volume(SND_VOL_MUSIC, 1.0f, 0.0f);
        snd_set_volume(SND_VOL_SFX, 1.0f, 0.0f);
    }
    particle_init();

    // Fog and sky off for comparable numbers; restored on exit
    saved_fog = atmosphere_get_fog_enabled();
    saved_sky = atmosphere_get_sky_enabled();
    atmosphere_set_fog_enabled(false);
    atmosphere_set_sky_enabled(false);

    // The default latency settings for comparable numbers (a demo on Lowest
    // would drop heavy steps to 30 FPS); restored on exit
    saved_sync = input_sync();
    saved_pacing = engine_pacing();
    input_set_sync(INPUT_SYNC_AUTO);
    engine_set_pacing(ENGINE_PACING_THROUGHPUT);

    action_context_init(&bench_ctx, "Benchmark", 0, CTX_CONSUME, bench_bindings, ARRAY_LEN(bench_bindings));
    for (int p = 0; p < ACTION_PLAYERS; p++) action_push_context(p, &bench_ctx);

    CameraConfig cfg = CAMERA_DEFAULT;
    cfg.distance  = 1100.0f;
    cfg.elevation = 0.55f;
    cfg.target    = (vec3_t){0.0f, 20.0f, 0.0f};
    camera_init(&scene->camera, &cfg);

    run_start_ticks = get_ticks();
    settling = run_start_ticks < (uint64_t)BOOT_SETTLE_S * TICKS_PER_SECOND;
    if (settling)
        debugf("BENCH_SETTLE,start,%.1f s after reset,measuring from %d s\n",
               (float)run_start_ticks / TICKS_PER_SECOND, BOOT_SETTLE_S);
    debugf("BENCH_META,build=%s,date=%s %s,rdram=%d,benchmark=%s,steps=%d,warmup=%d,measure=%d\n",
           ENGINE_BUILD_NAME, __DATE__, __TIME__, get_memory_size(),
           kind_names[configured_kind], step_count, WARMUP_FRAMES, MEASURE_FRAMES);
    debugf("BENCH_HDR,kind,step,param,frames,fps,avg_ms,p99_ms,low1_fps,cpu_avg_ms,cpu_max_ms,"
           "rdp_busy_ms,rdp_busy_pct,tris,tex_uploads,heap_kb\n");
    debugf("BENCH_PRESENT_HDR,kind,step,param,presents,vb1,vb2,vb3,vb4plus,late,avg_vblanks,torn,torn_worst_halfline,"
           "lag_avg_vblanks,lag_min,lag_max\n");
    debugf("BENCH_PROF_HDR,kind,step,param,update_us,draw_us,objects_us,mesh_cull_us,"
           "mesh_light_us,mesh_tris_us,audio_us,menu_us,hud_us,dialog_us,input_us,wait_input_us,"
           "particle_us,rsp_wait_us\n");
    debugf("BENCH_INPUT_HDR,kind,step,param,sync,pacing,fresh_pct,read_us,wait_us,wait_max_us,timeouts,auto_skips\n");
    setup_step(scene);
}

static void bench_update(Scene *scene, float dt) {
    (void)dt;
    if (finished) return;

    if (action_any_pressed(ACTION_UI_START, NULL)) {
        aborted = true;
        finished = true;
        debugf("BENCH,ABORTED,%d\n", step_index);
        return;
    }

    // The particle system spawns and moves particles in particle_update()
    PROF_BEGIN(PROF_PARTICLE_UPDATE);
    particle_update(1.0f / 60.0f);   // fixed step: identical load on every run
    PROF_END(PROF_PARTICLE_UPDATE);

    // OVERLOAD: burn CPU time to push the frame past the budget on purpose
    if (burn_ms > 0) {
        uint32_t t0 = TICKS_READ();
        uint32_t burn = (uint32_t)burn_ms * (TICKS_PER_SECOND / 1000);
        while ((uint32_t)TICKS_DISTANCE(t0, TICKS_READ()) < burn) { }
    }

    // AUDIO: keep the sound-effect voices busy (a new sound every 4 frames)
    if (audio_sfx && (step_frame % 4) == 0) {
        snd_play((SoundId)(SFX_MENU_OPEN + (step_frame / 4) % (SFX_COLLISION - SFX_MENU_OPEN + 1)));
    }

    // UI: scripted menu input
    if (ui_active) {
        if (ui_input == 1 && step_frame % 8 == 0)  menu_move_cursor(&ui_menu, 1);
        if (ui_input == 2 && step_frame % 2 == 0)  menu_change_value(&ui_menu, 1);
        if (ui_input == 3 && step_frame % 30 == 0) menu_switch_tab(&ui_menu, 1);
        if (ui_input == 4 && step_frame % 100 == 70) menu_close(&ui_menu, false);  // closed for 30 frames,
        if (ui_input == 4 && step_frame % 100 == 0 && !ui_menu.is_open) menu_open(&ui_menu); // then reopened
    }
    if (ui_dlg_active) {
        PROF_BEGIN(PROF_DIALOG);
        if (!textbox_active(&ui_dlg_box)) ui_dlg_restart();   // the conversation ended
        UiInput in = {0};
        TextBoxState s = ui_dlg_box.state;
        if (ui_dlg_skip) {
            in.confirm = (step_frame % 3) == 0;
        } else if (s == TB_WAIT || s == TB_CHOICE) {
            in.confirm = ++ui_dlg_wait >= (s == TB_CHOICE ? 60 : 24);
        }
        if (in.confirm) {
            ui_dlg_wait = 0;
            if (s == TB_CHOICE && ui_dlg_runner.choice_count > 0)
                ui_dlg_box.sel = ui_dlg_pick++ % ui_dlg_runner.choice_count;
        }
        textbox_update(&ui_dlg_box, &in, 1.0f / 60.0f);
        PROF_END(PROF_DIALOG);
    }

    // D32 settle: keep the first step's scene on screen, unmeasured
    if (settling) {
        if (get_ticks() < (uint64_t)BOOT_SETTLE_S * TICKS_PER_SECOND) return;
        settling = false;
        run_start_ticks = get_ticks();
        debugf("BENCH_SETTLE,done\n");
    }

    // Frame-locked camera path: identical on every run regardless of dt
    scene->camera.azimuth = step_frame * (CAMERA_SPIN / 60.0f);
    scene->camera.dirty = true;

    if (step_frame == WARMUP_FRAMES) {
        frametime_reset();
        input_reset_timing();
    }
    if (step_frame > WARMUP_FRAMES) {
        const EngineStats *s = stats_get();          // previous (measured) frame
        acc_tris    += stats_tris_total(s);
        acc_uploads += s->tex_uploads;
        acc_frames++;
    }

    step_frame++;
    if (step_frame >= WARMUP_FRAMES + MEASURE_FRAMES) {
        finish_step();
        step_index++;
        if (step_index >= step_count) {
            float secs = (float)(get_ticks() - run_start_ticks) / (float)TICKS_PER_SECOND;
            debugf("BENCH,END,%d,%.1f\n", step_count, secs);
            (void)secs;
            finished = true;
            return;
        }
        setup_step(scene);
    }
}

// The per-object loops run between every two shadow or mesh_draw calls, so
// each is pinned with the code it drives (src/engine/hot.h, D35): unpinned,
// the object loop once shared 36 of the mesh phase's I-cache lines (+22 us
// per object)
static ENGINE_HOT_HEAD void bench_draw_shadows(const Camera *cam, const LightConfig *L) {
    const Mesh *pillar = mesh_defs_get_pillar();
    const vec3_t pillar_scale = {40.0f, 100.0f, 40.0f};
    for (int i = 0; i < instance_count; i++) {
        mat4_t model;
        mat4_from_srt(&model, &pillar_scale, 0, 0, 0, &instance_pos[i]);
        ShadowCaster c = {
            .mesh = pillar, .model = &model, .position = instance_pos[i],
            .bound_radius = pillar->bound_radius * 100.0f,
        };
        if (L->shadow.mode == SHADOW_BLOB) shadow_draw_blob(cam, L, &c);
        else                               shadow_draw_projected(cam, L, &c);
    }
}

static ENGINE_HOT_LOOP void bench_draw_objects(const BenchStep *st, const Camera *cam,
                                               const LightConfig *L) {
    const Mesh *pillar = mesh_defs_get_pillar();
    const vec3_t pillar_scale = {40.0f, 100.0f, 40.0f};
    const vec3_t box_scale    = {40.0f, 40.0f, 40.0f};
    for (int i = 0; i < instance_count; i++) {
        mat4_t model;
        if (st->kind == BENCH_TEXTURES) {
            // Cycle through `param` distinct textures across the grid
            const Mesh *m = &tex_boxes[i % st->param];
            mat4_from_srt(&model, &box_scale, 0, 0, 0, &instance_pos[i]);
            mesh_draw(m, &model, cam, L);
        } else {
            mat4_from_srt(&model, &pillar_scale, 0, 0, 0, &instance_pos[i]);
            const Mesh *m = (st->kind == BENCH_LAYOUT && layout_variant > 0)
                ? &layout_copy[layout_variant - 1] : pillar;
            mesh_draw(m, &model, cam, L);
        }
    }
}

static void bench_draw(Scene *scene) {
    (void)scene;
    if (!draw_frame) draw_frame = __builtin_frame_address(0);
    if (!draw_rspq) draw_rspq = rspq_cur_pointer;
    const surface_t *fb = engine_framebuffer();
    if (fb && draw_fb_count < 3) {
        bool seen = false;
        for (int i = 0; i < draw_fb_count; i++) seen |= draw_fb[i] == fb->buffer;
        if (!seen) draw_fb[draw_fb_count++] = fb->buffer;
    }
    const BenchStep *st = &steps[step_index < step_count ? step_index : step_count - 1];
    const Camera *cam = scene_view_camera();
    const LightConfig *L = scene_view_light();

    if (draw_floor) {
        PROF_BEGIN(PROF_FLOOR);
        floor_draw(cam, L);
        PROF_END(PROF_FLOOR);
    }

    // Shadows go down first (Z-read, no Z-write), like the demo
    if (L->shadow.mode != SHADOW_OFF) {
        PROF_BEGIN(PROF_SHADOWS);
        shadow_begin(cam, L);
        bench_draw_shadows(cam, L);
        shadow_end();
        PROF_END(PROF_SHADOWS);
    }

    PROF_BEGIN(PROF_OBJECTS);
    bench_draw_objects(st, cam, L);
    PROF_END(PROF_OBJECTS);
}

static void bench_post_draw(Scene *scene) {
    (void)scene;
    PROF_BEGIN(PROF_PARTICLE_DRAW);
    particle_draw(scene_view_camera());
    PROF_END(PROF_PARTICLE_DRAW);

    // Fill-rate layers: full-screen blended rectangles (reads + writes the framebuffer)
    if (fill_layers > 0) {
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        for (int i = 0; i < fill_layers; i++) {
            static const color_t layer_col[4] = {
                RGBA32(0xFF, 0x40, 0x40, 0x60), RGBA32(0x40, 0xFF, 0x40, 0x60),
                RGBA32(0x40, 0x80, 0xFF, 0x60), RGBA32(0xFF, 0xFF, 0x40, 0x60),
            };
            rdpq_set_prim_color(layer_col[i % 4]);
            rdpq_fill_rectangle(0, 0, ENGINE_SCREEN_W, ENGINE_SCREEN_H);
            STATS_INC(fill_rects);
        }
    }

    if (ui_active) {
        PROF_BEGIN(PROF_MENU);
        menu_draw(&ui_menu, ui_mode >= 2 ? &ui_style_view : &ui_view[ui_mode ? 1 : 0]);
        PROF_END(PROF_MENU);
    }
    if (ui_hud_active) {
        PROF_BEGIN(PROF_HUD);
        ui_hud_draw(step_frame);
        PROF_END(PROF_HUD);
    }
    if (ui_dlg_active) {
        PROF_BEGIN(PROF_DIALOG);
        textbox_draw(&ui_dlg_box, &ui_style_debug);
        PROF_END(PROF_DIALOG);
    }

    // One status line (constant cost in every step)
    PROF_BEGIN(PROF_HUD);
    if (settling) {
        TextBoxConfig cfg = {
            .x = 12, .y = 214, .font_id = FONT_DEBUG_MONO,
            .color = RGBA32(0xFF, 0xFF, 0x80, 0xFF),
        };
        text_draw_fmt(&cfg, "BENCH settling after reset (D32): %d s",
                      BOOT_SETTLE_S - (int)(get_ticks() / TICKS_PER_SECOND));
        cfg.y = 226;
        cfg.color = RGBA32(0xC0, 0xC0, 0xC0, 0xFF);
        text_draw_fmt(&cfg, "%s", "not measured");
    } else if (step_index < step_count) {
        const BenchStep *st = &steps[step_index];
        int sy = ui_dlg_active ? 20 : 214;
        TextBoxConfig cfg = {
            .x = 12, .y = sy, .font_id = FONT_DEBUG_MONO,
            .color = RGBA32(0xFF, 0xFF, 0x80, 0xFF),
        };
        text_draw_fmt(&cfg, "BENCH %s %d/%d n=%d  Start=abort",
                      kind_names[st->kind], step_index + 1, step_count, st->param);
        cfg.y = sy + 12;
        cfg.color = RGBA32(0xC0, 0xC0, 0xC0, 0xFF);
        text_draw_fmt(&cfg, kind_desc[st->kind], st->param);
    }
    PROF_END(PROF_HUD);
}

static void bench_cleanup(Scene *scene) {
    (void)scene;
    destroy_emitters();
    particle_cleanup();
    for (int i = 0; i < NUM_TEX_BOXES; i++) mesh_cleanup(&tex_boxes[i]);
    free_layout_copies();
    ui_active = false;
    ui_dlg_active = false;
    if (ui_views_ready) {
        textbox_close(&ui_dlg_box);
        dialog_bank_free(ui_dlg_bank);
        ui_dlg_bank = NULL;
        menu_view_free(&ui_view[0]);
        menu_view_free(&ui_view[1]);
        menu_view_free(&ui_style_view);
        for (int c = 0; c < 2; c++) { hud_panel_free(&ui_hud[c][0]); hud_panel_free(&ui_hud[c][1]); }
        ui_views_ready = false;
    }
    snd_music_stop(0.0f);
    snd_stop_all_sfx();
    snd_set_poll_point(saved_poll_point);
    mesh_defs_cleanup();
    texture_cleanup();
    atmosphere_set_fog_enabled(saved_fog);
    atmosphere_set_sky_enabled(saved_sky);
    input_set_sync(saved_sync);
    engine_set_pacing(saved_pacing);
    for (int p = 0; p < ACTION_PLAYERS; p++) action_pop_context(p, &bench_ctx);
    if (!aborted && !finished) debugf("BENCH,ABORTED,%d\n", step_index);
}

static Scene benchmark_scene = {
    .name = "Benchmark",
    .object_count = 0,
    .texture_count = 0,
    .world_offset = {0, 0, 0},
    .bg_color = {0x20, 0x20, 0x30, 0xFF},
    .on_init = bench_init,
    .on_update = bench_update,
    .on_draw = bench_draw,
    .on_post_draw = bench_post_draw,
    .on_cleanup = bench_cleanup,
    .loaded = false,
};

Scene *benchmark_scene_get(void) {
    return &benchmark_scene;
}
