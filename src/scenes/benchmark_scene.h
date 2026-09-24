#ifndef BENCHMARK_SCENE_H
#define BENCHMARK_SCENE_H

/*
 * Benchmark scene (ROADMAP_v2 P1.8).
 *
 * A deterministic stress test: each benchmark ramps one load (objects,
 * particles, point lights, distinct textures, shadows, full-screen fill
 * layers) in steps. Each step runs 60 warm-up frames and 240 measured frames
 * with a fixed camera path, then prints one CSV row over debugf:
 *
 *   BENCH,<kind>,<step>,<param>,<frames>,<fps>,<avg_ms>,<p99_ms>,<low1_fps>,
 *         <cpu_avg_ms>,<cpu_max_ms>,<rdp_busy_ms>,<rdp_busy_pct>,<tris>,
 *         <tex_uploads>,<heap_kb>
 *
 * and "BENCH,END,..." when done. Start from the Debug tab (Scene: Benchmark,
 * Bench: which one). Press Start to abort. The scene returns to the demo when
 * finished. Capture with `sc64deployer debug | Tee-Object capture.log` and
 * compare runs with tools/bench_compare.py.
 */

#include <stdbool.h>
#include "../scene/scene.h"

typedef enum {
    BENCH_ALL,
    BENCH_OBJECTS,
    BENCH_PARTICLES,
    BENCH_LIGHTS,
    BENCH_TEXTURES,
    BENCH_SHADOWS,
    BENCH_FILLRATE,
    BENCH_OVERLOAD,     // deliberate CPU overrun (not part of "All"); reproduces D18
    BENCH_LAYOUT,       // data-placement sensitivity (not part of "All"); roadmap D26
    BENCH_AUDIO,        // mixer cost per music encoding and poll point (not part of "All")
    BENCH_UI,           // Start menu drawn immediate vs cached, with scripted input (not part of "All")
    BENCH_KIND_COUNT
} BenchKind;

Scene *benchmark_scene_get(void);
void   benchmark_scene_configure(BenchKind kind);   // before switching to the scene
bool   benchmark_scene_finished(void);              // true once done or aborted
const char *benchmark_kind_name(BenchKind kind);

#endif
