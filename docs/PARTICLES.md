# Particle System

Emitter-based particles for fire, sparks, magic and smoke: a fixed pool of 128 particles shared by up to 8 emitters, simulated on the CPU (gravity, drag, colour and size over the lifetime) and drawn as flat-coloured camera-facing quads sent straight to the RDP, additive (light) or alpha-blended (smoke, dust). Effects are data (`ParticleEmitterDef`), so one definition can drive any number of emitters.

## Files

| File | Contents |
|---|---|
| [src/render/particle.h](../src/render/particle.h) | public API, `ParticleEmitterDef`, limits |
| [src/render/particle.c](../src/render/particle.c) | simulation: pool, emitters, spawning, `particle_update()`, and `particle_batches()`, which groups the emitters by blend mode for the renderer. No rendering calls, so it is compiled into the host tests ([tests/host/test_particle.c](../tests/host/test_particle.c)) |
| [src/render/particle_draw.c](../src/render/particle_draw.c) | renderer, `particle_draw()`. `ENGINE_HOT`: linked into the hot-text block (`*render/particle_draw.o` in `src/engine/hot_text.ld`, phase `particle` in `tools/hot_text.py`) |
| [src/render/particle_internal.h](../src/render/particle_internal.h) | `Particle`, `ParticleEmitter` and the pool state shared by the two `.c` files. Not a public API: include `particle.h` |

The split (Phase 2 S3) keeps the simulation testable on the host and lets the hot-text linker script pick the renderer by its object file name (see [EXTENDING.md](EXTENDING.md#add-per-triangle-render-code)).

## Effect definitions

| `ParticleEmitterDef` field | Meaning |
|---|---|
| `burst_count` | particles per `particle_emitter_burst()` (0 = continuous only) |
| `spawn_rate` | particles per second while the emitter is active (0 = bursts only) |
| `lifetime_min`, `lifetime_max` | lifetime range in seconds |
| `velocity_min`, `velocity_max` | initial velocity range, per axis |
| `gravity` | acceleration per second² (any direction: the demo's magic effect uses +20 on Y to float up) |
| `drag` | each update multiplies the velocity by `1 − drag × dt` (0 = no drag) |
| `color_start`, `color_end` | RGBA at birth and at death; alpha scales an additive particle's brightness and is an alpha-blended particle's opacity |
| `scale_start`, `scale_end` | quad half-size in world units at birth and at death |
| `spawn_shape`, `spawn_radius` | `PARTICLE_SPAWN_POINT`, or `PARTICLE_SPAWN_SPHERE` with this radius |
| `blend_mode` | `PARTICLE_BLEND_ADDITIVE` adds light (fire, sparks, magic); `PARTICLE_BLEND_ALPHA` covers what is behind it by its alpha (smoke, dust). See [Rendering](#rendering) |

From `demo_scene.c`:

```c
static const ParticleEmitterDef fire_effect = {
    .burst_count   = 30,
    .spawn_rate    = 0.0f,
    .lifetime_min  = 0.6f,
    .lifetime_max  = 1.5f,
    .velocity_min  = {-40.0f,  60.0f, -40.0f},
    .velocity_max  = { 40.0f, 180.0f,  40.0f},
    .gravity       = { 0.0f, -50.0f, 0.0f},
    .drag          = 0.3f,
    .color_start   = {255, 200, 60, 255},    // Bright yellow-orange
    .color_end     = {180, 40, 10, 0},        // Dark red, fade out
    .scale_start   = 8.0f,
    .scale_end     = 3.0f,
    .spawn_shape   = PARTICLE_SPAWN_SPHERE,
    .spawn_radius  = 15.0f,
    .blend_mode    = PARTICLE_BLEND_ADDITIVE,
};
```

**The emitter keeps the `def` pointer; it does not copy the definition.** Definitions therefore need static storage: `static const` for fixed effects, or a static variable you edit, like the benchmark's `step_particles`, which is copied from `bench_particles` and given a new `spawn_rate` for each step. A definition on the stack leaves the emitter reading a dead stack frame: the first benchmark run drew no particles at all for that reason.

## Pool and emitters

`PARTICLE_MAX_POOL` (128) particles and `PARTICLE_MAX_EMITTERS` (8) emitters form one global system for the whole engine, with no heap allocation. Each emitter owns a contiguous slice of the pool, reserved at creation at the end of the used range (`particle_pool_allocated`). The demo with point lights on fills the pool:

```
particle_pool[128]
| fire 0-39 | magic 40-79 | smoke 80-95 | torch L 96-111 | torch R 112-127 |
                                                                           ^ particle_pool_allocated = 128
```

- `particle_emitter_create()` returns -1 when `particle_init()` hasn't run, `def` is NULL, fewer than `pool_size` particles are free at the end of the pool, or all 8 emitters exist.
- `particle_emitter_destroy()` kills the emitter's particles, frees its slot, and lowers `particle_pool_allocated` to the end of the highest remaining emitter. **Space is reclaimed only from the tail:** destroying `fire` above would leave 0–39 unused until every emitter after it is gone too. The demo's torches come and go with the Pt Lights item and sit at the tail, so their space comes back.
- Size a slice for its peak: at least `burst_count` for a burst (a burst fills only the dead slots of its own slice), about `spawn_rate × lifetime` for a continuous emitter (the benchmark sizes it exactly).

## API

| Function | Notes |
|---|---|
| `particle_init()` | clears the pool and the emitters and seeds `rand()`; call it in `on_init`, before creating emitters |
| `particle_cleanup()` | clears everything; call it in `on_cleanup` (every handle becomes invalid) |
| `particle_emitter_create(def, position, pool_size)` | handle 0–7, or -1; the new emitter is **inactive** |
| `particle_emitter_destroy(handle)` | kills its particles and frees its slot |
| `particle_emitter_set_position(handle, pos)` | moves the spawn point; live particles stay where they are |
| `particle_emitter_burst(handle)` | spawns up to `burst_count` particles at once, active or not |
| `particle_emitter_set_active(handle, on)` | turns continuous spawning at `spawn_rate` on or off |
| `particle_update(dt)` | spawning and simulation; call once per frame from `on_update` |
| `particle_draw(cam)` | draws every live particle; call from `on_post_draw`, after the opaque geometry |
| `particle_alive_count()` | live particles in the used part of the pool |

Invalid handles are ignored. The demo's use of the API:

```c
// on_init
particle_init();
emitter_fire = particle_emitter_create(&fire_effect,
    (vec3_t){-250.0f, 100.0f, 0.0f}, 40);

// on_update: a burst on B, then the simulation
particle_emitter_burst(emitter_fire);
...
PROF_BEGIN(PROF_PARTICLE_UPDATE);
particle_update(dt);
PROF_END(PROF_PARTICLE_UPDATE);

// on_post_draw: after the opaque geometry, before the HUD
PROF_BEGIN(PROF_PARTICLE_DRAW);
particle_draw(scene_view_camera());   // the frame's pinned camera copy
PROF_END(PROF_PARTICLE_DRAW);

// on_cleanup
particle_cleanup();
```

## Spawning

- **Where.** At the emitter position. `PARTICLE_SPAWN_SPHERE` adds an offset in a random direction at a uniformly random distance up to `spawn_radius`, so spawns cluster toward the centre. `PARTICLE_SPAWN_POINT` (or a zero radius) spawns at the position itself.
- **Initial state.** Velocity uniform per axis between `velocity_min` and `velocity_max`, lifetime uniform between `lifetime_min` and `lifetime_max` (a zero lifetime dies on the next update), colour and scale at their `_start` values.
- **Bursts.** `particle_emitter_burst()` spawns up to `burst_count` particles into dead slots of the emitter's slice, whether or not the emitter is active.
- **Continuous.** While the emitter is active, each update adds `spawn_rate × dt` to an accumulator and spawns one particle per whole unit into the first dead slot of the slice. When the slice is full, the backlog is dropped rather than saved up.
- **Randomness.** C `rand()`, seeded by `particle_init()` from `TICKS_READ()`, so spawn patterns differ between runs. The host shim's `TICKS_READ()` returns 0, so the tests repeat.

## Update

`particle_update(dt)` walks the emitters rather than the particles: each slice is updated with its own emitter's definition, whose constants are loaded once, and no particle searches for its owner (the O(particles × emitters) lookup of D14, removed in S3). For each emitter, continuous spawning runs first (above), then every live particle of the slice is advanced. Excerpt from `particle.c` (x and z are handled like y):

```c
p->lifetime -= dt;
if (p->lifetime <= 0.0f) {
    p->alive = false;
    continue;
}

// Gravity, then drag   (gy = gravity.y * dt, damp = max(0, 1 - drag * dt))
p->velocity.y = (p->velocity.y + gy) * damp;

// Integrate position with the new velocity
p->position.y += p->velocity.y * dt;

// Interpolation factor: 0 at birth, 1 at death
float t = 1.0f - p->lifetime * p->inv_max_lifetime;
```

Colour (each RGBA channel, clamped to 0–255) and scale are then interpolated linearly from their `_start` to their `_end` values by `t`. `test_particle.c` pins the numbers down: with gravity −100, one 0.25 s update gives velocity −25 and position −6.25, and a 1 s particle that grows from scale 10 to 20 measures 12.5 after that quarter of its life.

## Rendering

`particle_draw(cam)` follows the direct-RDP pattern of `floor_draw()` rather than going through `mesh_draw()`: one render mode for every particle, a blender per blend mode, then two triangles each.

1. `particle_batches()` (in `particle.c`, once per frame, outside the hot-text block) groups the emitters' pool slices by blend mode and counts each mode's live particles (`particles_alive`). With none, return before touching the RDP.
2. Set the mode once:

   ```c
   rdpq_set_mode_standard();
   rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
   rdpq_mode_zbuf(true, false);                  // Z read on, Z write off
   ```
3. Draw the alpha batch, then the additive batch, each only if it has live particles:

   ```c
   rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);     // alpha:    colour x a + framebuffer x (1 - a)
   ...                                           //           the alpha emitters' slices
   rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);     // additive: colour x a + framebuffer
   ...                                           //           the additive emitters' slices
   ```
4. For every live particle with a non-zero alpha, transform only its centre:

   ```c
   const float px = cam->proj.m[0][0] * 160.0f;   // once per call
   const float py = cam->proj.m[1][1] * 120.0f;
   ...
   mat4_mul_vec3(&clip, &cam->vp, &p->position);
   if (clip.w < cam->near_plane) continue;        // centre in front of the near plane
   ...
   float hx = p->scale * px * inv_w;              // half-size in pixels
   float hy = p->scale * py * inv_w;
   ```

   The quad spans the camera's right and up axes, so it lies parallel to the image plane (the camera has no roll) and projects to a screen-aligned square at a single depth. One transform gives all four corners exactly; S3 replaced four corner transforms and a frustum-sphere test with it. Particles beyond the far plane, entirely off screen or crossing the guard band are skipped.
5. **Fog.** When fog is on, `f = 1 − fog_calculate_factor(clip.w)`. An additive particle's RGBA is multiplied by `f`: fading to black is fading out, whereas blending toward the fog colour would make distant particles brighter. An alpha particle's alpha alone is multiplied, so distant smoke thins into the fog and keeps its colour.
6. `rdpq_set_prim_color()` is issued only when the colour differs from the previous particle's.
7. Two `TRIFMT_ZBUF` triangles per particle: X, Y and Z only, no texture, no TMEM.

**Z read without Z write.** Opaque geometry drawn earlier hides the particles behind it, but particles don't hide each other or anything drawn after them. Additive blending doesn't depend on order. Alpha blending does, and **alpha particles are not sorted by depth**: where puffs overlap, a nearer one may be covered by a farther one drawn later, which shows little at the low alphas smoke uses. The alpha batch is drawn first, so smoke covers what is behind it and fire seen through smoke stays bright. Draw particles after all opaque geometry: the demo and the benchmark call `particle_draw()` at the start of `on_post_draw`, before the HUD.

## Stats, profiler and cost

| Counter | Meaning |
|---|---|
| `particles_alive` | live particles, set by `particle_draw()` |
| `particles_drawn` | quads submitted after culling |
| `tris_particle` | two per drawn particle |

The overlay's Stats page shows them (`Particles … drawn …`, and `prt` in the triangle line). The particle code has no profiler scopes of its own: callers wrap `particle_update()` in `PROF_PARTICLE_UPDATE` (`particle_upd`, under `update`) and `particle_draw()` in `PROF_PARTICLE_DRAW` (`particle_draw`, under `draw`). Only `particle_draw` is on the overlay's Profiler page; both are in the `PROF` CSV rows ([PROFILING.md](PROFILING.md)).

Measured in the Phase 1 baseline (2026-09-23, debug build, Analogue 3D, before the S3 rewrite; [BENCHMARKS.md](BENCHMARKS.md)):

| Particles step (alive) | CPU avg ms | RDP busy ms |
|---|---|---|
| empty scene | 1.20 | 1.1 |
| 32 (27) | 2.59 | 1.3 |
| 64 (58) | 4.07 | 1.4 |
| 96 (89) | 5.60 | 1.5 |
| 128 (121) | 7.21 | 1.6 |

That was about 50 µs of CPU per particle; a burst in the demo (60–76 particles alive) cost about 2.3 ms. After S3 and S10 (2026-09-25, same build and console, `BENCH_PROF` `particle_us` and `rsp_wait_us`):

| Particles step (alive) | CPU avg ms | `particle_draw` µs | RDP busy ms | CPU waiting for the RSP µs |
|---|---|---|---|---|
| 32 additive (27) | 2.27 | 1155 | 1.17 | 308 |
| 64 additive (58) | 3.05 | 1854 | 1.26 | 300 |
| 96 additive (89) | 3.85 | 2545 | 1.36 | 307 |
| 128 additive (121) | 4.68 | 3260 | 1.47 | 286 |
| 128 alpha (param 1128) | 4.69 | 3279 | 1.47 | 307 |
| 64 alpha + 64 additive (2128) | 4.70 | 3277 | 1.48 | 309 |

About 22 µs of CPU per particle, the same for both blend modes (the 128 step needs 35 % less CPU than the baseline), and a second batch costs nothing measurable. The RSP wait does not grow with the particle count: the renderer is CPU-bound.

## Usage in the demo and the benchmark

| Emitter | Definition | Slice | Where and when |
|---|---|---|---|
| fire | burst of 30, rising, yellow-orange to dark red | 40 | top of the left pillar (−250, 100, 0); bursts when B (Cancel) spawns or relaunches the physics ball |
| magic | burst of 25, floats up and grows, light blue to purple | 40 | top of the right pillar (250, 100, 0); same trigger |
| smoke | burst of 12, **alpha-blended**, rises slowly and spreads (scale 7 → 24), dark grey thinning out over 1.6–2.8 s | 16 | above the left pillar (−250, 115, 0); bursts with the fire |
| torch L, torch R | continuous, 20 per second, 0.3–0.8 s | 16 each | on both pillars while Lighting → Pt Lights is On; created and destroyed with that item |
| benchmark (×4) | continuous, 0.9 s lifetime, rate = slice ÷ 0.9 s; additive, all alpha (param 1128) or two of each (2128) | particles ÷ 4 each | the four corners (±150, 0, ±150) |

## Known limits

- **Alpha particles are not depth-sorted** (see Rendering), and all alpha particles are drawn before all additive ones, whatever their depth.
- **Slices are reclaimed only from the tail** (see Pool and emitters), and a slice's size is fixed when the emitter is created.
- 128 particles and 8 emitters in total, for the whole engine.
- Flat-coloured squares: no texture, rotation or animation (sprite-sheet particles belong to Feature 8, ROADMAP_v2 §7.2), and scene lights don't affect them.
- No clipping: a particle whose centre is nearer than the near plane is dropped whole, and so is a quad crossing the guard band. All four corners share the centre's depth.
- libdragon's `rdpq_mode.h` notes that the RDP's additive blend doesn't saturate: a sum above 1.0 wraps around to 0. Many bright particles over a bright background may show dark pixels. ares shows it as coloured noise in the benchmark's dense clusters of bright orange particles; it hasn't been characterised on the A3D yet.
- Spawn randomness is seeded from the tick counter, so runs don't repeat exactly.
