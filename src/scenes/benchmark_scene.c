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
#include "../input/action.h"
#include "../ui/text.h"
#include "../debug/engine_debug.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "../debug/frametime.h"
#include "../debug/memstats.h"

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
#define BUDGET_MS      16.67f

typedef struct {
    BenchKind kind;
    int       param;
} BenchStep;

static const char *kind_names[BENCH_KIND_COUNT] = {
    "all", "objects", "particles", "lights", "textures", "shadows", "fillrate", "overload", "layout",
};

// One-line description shown under the status line (param is substituted)
static const char *kind_desc[BENCH_KIND_COUNT] = {
    "Empty scene: engine baseline",
    "%d flat pillars, 32 tris each",
    "~%d additive particles, 4 emitters",
    "16 pillars lit by %d point lights",
    "16 boxes, %d distinct textures",
    "16 pillars, shadow mode %d:off/blob/proj",
    "%d full-screen blended layers",
    "+%d ms CPU burn, floor + 16 pillars",
    "%d: variant*1000+pillars (0 shared, 1-4 at 0/2/4/6 KB, 5 reversed)",
};

static BenchKind  configured_kind = BENCH_ALL;
static BenchStep  steps[MAX_STEPS];
static int        step_count;
static int        step_index;
static int        step_frame;          // frames since the step started
static bool       finished;
static bool       aborted;
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
        static const int n[] = {32, 64, 96, 128};
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
static ParticleEmitterDef step_particles;

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
    lighting_init(L);
    L->point_light_count = 0;
    for (int i = 0; i < MAX_POINT_LIGHTS; i++) L->point_lights[i].active = false;
    L->shadow.mode = SHADOW_OFF;

    switch (st->kind) {
    case BENCH_OBJECTS:
        layout_grid(st->param);
        break;
    case BENCH_PARTICLES: {
        // Four continuous emitters; alive count settles near rate * lifetime
        step_particles = bench_particles;
        int per = st->param / 4;
        step_particles.spawn_rate = per / step_particles.lifetime_max;
        static const vec3_t corners[4] = {{-150,0,-150},{150,0,-150},{-150,0,150},{150,0,150}};
        for (int i = 0; i < 4; i++) {
            emitters[i] = particle_emitter_create(&step_particles, corners[i], per);
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
    frametime_get(&ft, BUDGET_MS);
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

    // CPU breakdown of the step (profiler moving averages, ~32 frames), so a
    // regression can be pinned to a stage of mesh_draw. Separate row type:
    // BENCH rows and bench_compare.py are unchanged.
    if (g_prof_on) {
        const ProfilerFrame *pf = profiler_get();
        (void)pf;
        debugf("BENCH_PROF,%s,%d,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f\n",
               kind_names[st->kind], step_index, st->param,
               pf->avg_us[PROF_UPDATE], pf->avg_us[PROF_DRAW], pf->avg_us[PROF_OBJECTS],
               pf->avg_us[PROF_MESH_CULL], pf->avg_us[PROF_MESH_LIGHT], pf->avg_us[PROF_MESH_TRIS]);
    }
}

// ------------------------------------------------------------------------
// Scene callbacks
// ------------------------------------------------------------------------

static void bench_init(Scene *scene) {
    finished = false;
    aborted = false;
    draw_frame = NULL;
    layout_logged = false;
    build_steps(configured_kind);
    step_index = 0;

    texture_init();                                  // slots 0-5
    texture_load_slot(6, "rom:/marker.sprite");
    texture_load_slot(7, "rom:/tree.sprite");
    for (int i = 0; i < NUM_TEX_BOXES; i++) build_tex_box(&tex_boxes[i], i);
    mesh_defs_init();
    if (configured_kind == BENCH_LAYOUT) build_layout_copies();
    particle_init();

    // Fog and sky off for comparable numbers; restored on exit
    saved_fog = atmosphere_get_fog_enabled();
    saved_sky = atmosphere_get_sky_enabled();
    atmosphere_set_fog_enabled(false);
    atmosphere_set_sky_enabled(false);

    CameraConfig cfg = CAMERA_DEFAULT;
    cfg.distance  = 1100.0f;
    cfg.elevation = 0.55f;
    cfg.target    = (vec3_t){0.0f, 20.0f, 0.0f};
    camera_init(&scene->camera, &cfg);

    run_start_ticks = get_ticks();
    debugf("BENCH_META,build=%s,date=%s %s,rdram=%d,benchmark=%s,steps=%d,warmup=%d,measure=%d\n",
           ENGINE_BUILD_NAME, __DATE__, __TIME__, get_memory_size(),
           kind_names[configured_kind], step_count, WARMUP_FRAMES, MEASURE_FRAMES);
    debugf("BENCH_HDR,kind,step,param,frames,fps,avg_ms,p99_ms,low1_fps,cpu_avg_ms,cpu_max_ms,"
           "rdp_busy_ms,rdp_busy_pct,tris,tex_uploads,heap_kb\n");
    debugf("BENCH_PROF_HDR,kind,step,param,update_us,draw_us,objects_us,mesh_cull_us,"
           "mesh_light_us,mesh_tris_us\n");
    setup_step(scene);
}

static void bench_update(Scene *scene, float dt) {
    (void)dt;
    if (finished) return;

    action_update();   // polls the joypad (debug shortcuts rely on it too)
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start) {
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

    // Frame-locked camera path: identical on every run regardless of dt
    scene->camera.azimuth = step_frame * (CAMERA_SPIN / 60.0f);
    scene->camera.dirty = true;

    if (step_frame == WARMUP_FRAMES) frametime_reset();
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

static void bench_draw(Scene *scene) {
    if (!draw_frame) draw_frame = __builtin_frame_address(0);
    const BenchStep *st = &steps[step_index < step_count ? step_index : step_count - 1];
    const Camera *cam = &scene->camera;
    const LightConfig *L = &scene->lighting;
    const Mesh *pillar = mesh_defs_get_pillar();
    vec3_t pillar_scale = {40.0f, 100.0f, 40.0f};
    vec3_t box_scale    = {40.0f, 40.0f, 40.0f};

    if (draw_floor) {
        PROF_BEGIN(PROF_FLOOR);
        floor_draw(cam, L);
        PROF_END(PROF_FLOOR);
    }

    // Shadows go down first (Z-read, no Z-write), like the demo
    if (L->shadow.mode != SHADOW_OFF) {
        PROF_BEGIN(PROF_SHADOWS);
        shadow_begin(cam, L);
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
        shadow_end();
        PROF_END(PROF_SHADOWS);
    }

    PROF_BEGIN(PROF_OBJECTS);
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
    PROF_END(PROF_OBJECTS);
}

static void bench_post_draw(Scene *scene) {
    PROF_BEGIN(PROF_PARTICLE_DRAW);
    particle_draw(&scene->camera);
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
            rdpq_fill_rectangle(0, 0, 320, 240);
            STATS_INC(fill_rects);
        }
    }

    // One status line (constant cost in every step)
    PROF_BEGIN(PROF_HUD);
    if (step_index < step_count) {
        const BenchStep *st = &steps[step_index];
        TextBoxConfig cfg = {
            .x = 12, .y = 214, .font_id = FONT_DEBUG_MONO,
            .color = RGBA32(0xFF, 0xFF, 0x80, 0xFF),
        };
        text_draw_fmt(&cfg, "BENCH %s %d/%d n=%d  Start=abort",
                      kind_names[st->kind], step_index + 1, step_count, st->param);
        cfg.y = 226;
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
    mesh_defs_cleanup();
    texture_cleanup();
    atmosphere_set_fog_enabled(saved_fog);
    atmosphere_set_sky_enabled(saved_sky);
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
