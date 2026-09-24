# Extending the Engine

Recipes for the changes contributors make most often. Each one lists the files to touch, the steps, and the gotchas that have already bitten someone. The system docs linked from each recipe explain the background; build, deploy and log commands are in [WORKFLOW.md](WORKFLOW.md).

**Rules for every recipe**

- The Makefile compiles every `src/*.c` and `src/*/*.c` by itself, so a new file needs no Makefile edit. A file two directories deep (`src/a/b/x.c`) is not built.
- The ROM (libdragon's `n64.mk`) and the host tests both build with `-Wall -Werror`. `debugf`, `assertf` and `ENGINE_LOG` compile out of release builds, so a variable used only in a log line needs `(void)var;` or the release build fails.
- New code that does per-frame work gets a profiler slot or a stats counter (recipes below).
- Before committing, run `libdragon exec bash tools/ci_build.sh` (both ROMs, host tests, budgets, hot-text check). Python tools run in the container: `libdragon exec python3 tools/<tool>.py`.

**Recipes:** [scene](#add-a-scene) · [mesh](#add-a-mesh) · [texture](#add-a-texture) · [Start menu item](#add-a-start-menu-item) · [Debug tab item](#add-a-debug-tab-item) · [profiler slot](#add-a-profiler-slot) · [stats counter](#add-a-stats-counter) · [benchmark kind](#add-a-benchmark-kind) · [host unit test](#add-a-host-unit-test) · [per-triangle render code](#add-per-triangle-render-code) · [sound](#add-a-sound) · [dialog](#add-dialog) · [contributing a change](#contributing-a-change)

---

## Add a scene

**Files:** `src/scenes/<name>_scene.c/.h` (new), `src/main.c`, and `src/debug/debug_menu.c` to reach the scene from the Debug tab. Background: [SCENE_SYSTEM.md](SCENE_SYSTEM.md).

A scene is a static `Scene` (`src/scene/scene.h`) whose callbacks the scene manager calls:

| Member | Used by | When |
|---|---|---|
| `on_init` | `scene_init()` | after `collision_world_init()`, `lighting_init()` and the declared textures are loaded |
| `on_update` | `scene_update()` | every frame, after each active object's `on_update`, before `camera_update()` and `collision_test_all()` |
| `on_draw` | `scene_draw()` | after the background (the sky, or a clear to `bg_color`) and the Z clear, before each visible object's `on_draw` |
| `on_post_draw` | `scene_draw()` | after all object draws: particles, HUD, menu |
| `on_cleanup` | `scene_cleanup()` | before the declared textures are freed and `object_count` is zeroed |
| `texture_paths`, `texture_slots`, `texture_count` | `scene_init()`, `scene_cleanup()` | loaded before `on_init`, freed after `on_cleanup` (up to 16) |
| `reset_requested` | `scene_manager_update()` | set it to request a soft reset |

A minimal scene (compare `src/scenes/benchmark_scene.c`):

```c
#include "my_scene.h"
#include "../render/mesh_defs.h"
#include "../input/action.h"

static void my_init(Scene *scene) {
    camera_init(&scene->camera, &CAMERA_DEFAULT);   // scene_init() does not reset the camera
    mesh_defs_init();
}

static void my_update(Scene *scene, float dt) {
    (void)dt;
    action_update();                                // polls the joypad: nothing else does
    if (action_pressed(ACTION_CANCEL)) scene->reset_requested = true;
}

static void my_draw(Scene *scene) {
    vec3_t scale = {40.0f, 100.0f, 40.0f}, pos = {0.0f, 0.0f, 0.0f};
    mat4_t model;
    mat4_from_srt(&model, &scale, 0, 0, 0, &pos);
    mesh_draw(mesh_defs_get_pillar(), &model, &scene->camera, &scene->lighting);
}

static void my_cleanup(Scene *scene) {
    (void)scene;
    mesh_defs_cleanup();
}

static Scene my_scene = {
    .name       = "My Scene",
    .bg_color   = {0x20, 0x20, 0x30, 0xFF},
    .on_init    = my_init,
    .on_update  = my_update,
    .on_draw    = my_draw,
    .on_cleanup = my_cleanup,
};

Scene *my_scene_get(void) { return &my_scene; }
```

Steps:

1. In `on_init`, build everything the scene uses and reset every static it keeps: `on_init` runs again after each soft reset and each time the scene is entered.
2. Call `action_update()` at the top of `on_update`. The main loop never polls the joypad; the demo and the benchmark both poll in their update. Start and the Debug shortcuts (D-Up, D-Down) read the same polled state.
3. Draw 3D geometry in `on_draw`, but don't clear the screen or draw the sky: `scene_draw()` already did. Particles, text and the menu go in `on_post_draw`.
4. Free in `on_cleanup` everything `on_init` created: meshes, emitters, hand-loaded textures, the BGM.
5. In `main.c` (at boot, or in `app_frame()`), include the header and switch with `scene_manager_switch(&scene_mgr, my_scene_get(), TRANSITION_FADE_BLACK, 3.0f)` (the speed is fade progress per second; `TRANSITION_CUT` switches at once).
6. To pick the scene from Debug → Scene: add a name to `scene_options[]` in `debug_menu.c`, raise the Scene item's option count in `debug_menu_init()`, and handle the new index where `main.c` calls `debug_consume_scene_request()` (0 = demo, 1 = benchmark today; the `else` branch falls back to the demo). When `main.c` changes scene by itself, call `debug_menu_set_active_scene()` so the item follows, as the benchmark's return to the demo does.
7. Check for leaks with Debug → Reset Soak: one warm-up reset and 10 measured soft resets of the current scene, then a `SOAK,...,delta=` log line that should read 0.

Gotchas:

- **The Start menu belongs to the demo.** `demo_update()` opens and updates it and `demo_post_draw()` draws it. In any other scene Start doesn't open the menu (the benchmark uses it to abort), so the Debug tab (Scene, Reset Soak) is out of reach. Either drive the menu yourself, or give the scene an exit the way `benchmark_scene_finished()` does (`main.c` polls it and returns to the demo). The Settings, Sound, Lighting, Environ and Controls tabs only take effect in the demo, which owns their meaning (D10).

  ```c
  extern Menu start_menu;                          // owned by main.c

  // on_update, after action_update():
  joypad_buttons_t raw_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
  if (raw_pressed.start) {
      if (start_menu.is_open) menu_close(&start_menu, true);
      else                    menu_open(&start_menu);
  }
  if (start_menu.is_open) menu_update(&start_menu);

  // on_post_draw, last:
  menu_draw(&start_menu, &my_menu_view);   // a MenuView of the scene's own (docs/UI.md)
  ```
- **Soft reset:** `scene->reset_requested = true` makes the next `scene_manager_update()` run `scene_cleanup()` and `scene_init()` and skip that frame's update. The menu keeps its values across the reset (the demo's Settings → Reset Scene uses this).
- **Camera dirty flag:** `camera_update()` rebuilds the matrices only when `camera.dirty` is set (every `camera_*` setter sets it) or in follow mode with a target. Code that writes `Camera` fields directly (`azimuth`, `fixed_position`, `follow_offset`) must set `scene->camera.dirty = true`, as `demo_update()` and `bench_update()` do.
- **Global state outlives the scene:** fog and sky (`atmosphere_*`), the particle system, the 16 texture slots, the BGM, the action bindings and `engine_target_fps`. Restore what you change: the benchmark saves fog and sky in `bench_init()` and restores them in `bench_cleanup()`. `texture_cleanup()` frees every slot, not only yours.
- `scene_add_object()` copies the `SceneObject` and returns -1 once the scene holds 32. `SceneObject.data` must outlive the object: the demo keeps static pools (`object_data[]`, `billboard_data[]`). `world_offset` and `collider_handle` are stored but not read by the engine (`collider_handle`: D11).

---

## Add a mesh

**Files:** the scene or module that owns the mesh; for a shared built-in shape, `src/render/mesh_defs.c/.h` and `tests/host/test_mesh.c`. Background: [MESH_SYSTEM.md](MESH_SYSTEM.md).

The builder API is declared in `src/render/mesh.h` and implemented in `mesh_build.c`: `mesh_init` → `mesh_add_material` → for each face group `mesh_begin_group`, `mesh_add_vertex`, `mesh_add_triangle`, `mesh_end_group` → `mesh_finalize`. From `build_platform()` in `mesh_defs.c`:

```c
mesh_init(&platform_mesh);                          // zeroed, backface_cull = true
int mat = mesh_add_material(&platform_mesh, (Material){
    .type = MATERIAL_FLAT_COLOR,
    .texture_slot = -1,
    .base_color = {140, 100, 60}
});
for (int f = 0; f < 6; f++) {
    mesh_begin_group(&platform_mesh, mat);          // one group per flat face
    int base = platform_mesh.vertex_count;          // triangle indices are absolute
    for (int v = 0; v < 4; v++) {
        int vi = faces[f][v];
        mesh_add_vertex(&platform_mesh, (MeshVertex){
            .position = {verts[vi][0], verts[vi][1], verts[vi][2]},
            .normal = {normals[f][0], normals[f][1], normals[f][2]},
            .uv = {0, 0}
        });
    }
    mesh_add_triangle(&platform_mesh, base, base + 1, base + 2);   // counter-clockwise
    mesh_add_triangle(&platform_mesh, base, base + 2, base + 3);   // seen from outside
    mesh_end_group(&platform_mesh);
}
mesh_finalize(&platform_mesh);                      // bounds, group analysis, exact-size geometry
```

Steps:

1. Model around the origin at unit size. The object's transform places it: `mat4_from_srt(&model, &scale, rx, ry, rz, &pos)` (rotation Ry·Rx·Rz), then `mesh_draw(mesh, &model, cam, light)`, as `object_draw()` in `demo_scene.c` does.
2. Wind every triangle counter-clockwise seen from outside, that is, from the side its normals point to.
3. Give each flat face its own group with one shared normal (the pillar has 10 groups: 8 sides and 2 caps).
4. Call `mesh_finalize()` after the last group (bounds, group analysis, and one exact-size geometry block; nothing can be added afterwards), and `mesh_cleanup()` in `on_cleanup`.
5. For a built-in shape: a static `Mesh` and a `build_<shape>()` in `mesh_defs.c`, called from `mesh_defs_init()` and freed in `mesh_defs_cleanup()`, a getter in `mesh_defs.h`, and host checks in `tests/host/test_mesh.c`: `CHECK(count_bad_winding(mesh_defs_get_<shape>()) == 0);` in `test_mesh_winding()`, plus the expected planar-group count in `test_mesh_planar_groups()`.

`mesh_finalize()` (through `mesh_compute_bounds()`) runs `mesh_analyze_group()` on every group, and `mesh_draw()` treats the two kinds differently:

| | Planar group | Curved group |
|---|---|---|
| Condition | every vertex has the same normal and lies on one plane | anything else, e.g. the sphere's latitude bands |
| Back-face cull | once per group: is the camera in front of the group's plane? | per triangle: sign of the screen-space area (`mesh_screen_area2()`; negative = front) |
| Lighting | once per group, one prim colour | per triangle, from the average of its three vertex normals |

Shading is flat either way (Gouraud is planned as P3.2): vertex normals only decide the face or triangle normal.

`backface_cull` is `true` after `mesh_init()`. Set it to `false` for double-sided surfaces (the billboard quad does): both sides are drawn, both are lit with the front normal, and projected shadows use every face.

Gotchas:

- **Winding errors hide on planar groups**, which `mesh_draw()` and projected shadows both test with the group normal. On curved groups both use the screen winding, so a wrong winding shows up there as missing triangles and holes in the shadow. `count_bad_winding()` in `test_mesh.c` catches it on the host either way.
- **Limits fail quietly** (512 vertices, 1024 indices, 8 materials, 16 groups): `mesh_add_vertex`, `mesh_add_material` and `mesh_begin_group` return -1, `mesh_add_triangle` drops the triangle, and after a failed `mesh_begin_group` triangles land in the previous group.
- Forgetting `mesh_finalize()` doesn't crash: the radius stays 0, so the mesh vanishes as soon as its centre leaves the view, every group takes the slower curved path, and the build arrays keep their spare capacity.
- Build arrays start at 16 vertices / 48 indices and double while building; `mesh_finalize()` trims them into one block. Adding vertices or triangles after it fails (assert in debug builds).
- Placement matters: a mesh whose geometry or `Mesh` struct shares D-cache sets with the render stack draws up to ~8 % slower (D26). Bench = Layout measures it.
- UVs are in texels (0–32 for a 32×32 sprite), and the slot of a `MATERIAL_TEXTURED` material must be loaded before the first draw (`texture_upload()` asserts in debug builds).

---

## Add a texture

**Files:** `assets/<name>.png`, plus the scene's `texture_paths` or its `on_init`/`on_cleanup`. Background: [TEXTURES.md](TEXTURES.md).

1. Put `<name>.png` in `assets/` (top level only). The Makefile rule `filesystem/%.sprite: assets/%.png` runs `mksprite --format RGBA16` (`MKSPRITE_FLAGS`), and the sprite is packed into the ROM as `rom:/<name>.sprite`.
2. Keep it inside TMEM (4 KB). A 32×32 RGBA16 texture (all current ones) takes 2 KB, 64×32 takes exactly 4 KB, 64×64 (8 KB) doesn't fit. `texture_load_slot()` asserts `sprite_fits_tmem()` in debug builds.
3. Pick a slot (0–15). The demo uses 0–5 for the cube faces (`TEX_CUBE_*` in `texture.h`) and 6–7 for the billboards (`TEX_BILLBOARD_MARKER`, `TEX_BILLBOARD_TREE` in `demo_scene.c`); the benchmark loads 0–7 itself.
4. Declare it on the scene. `scene_init()` loads it before `on_init` and `scene_cleanup()` frees it after `on_cleanup`. From `demo_scene.c`:

   ```c
   .texture_paths = {
       "rom:/face_front.sprite", "rom:/face_back.sprite", "rom:/face_top.sprite",
       "rom:/face_bottom.sprite", "rom:/face_right.sprite", "rom:/face_left.sprite",
       "rom:/marker.sprite", "rom:/tree.sprite",
   },
   .texture_slots = {
       TEX_CUBE_FRONT, TEX_CUBE_BACK, TEX_CUBE_TOP,
       TEX_CUBE_BOTTOM, TEX_CUBE_RIGHT, TEX_CUBE_LEFT,
       TEX_BILLBOARD_MARKER, TEX_BILLBOARD_TREE,
   },
   .texture_count = 8,
   ```

   Or load it by hand with `texture_load_slot(slot, "rom:/<name>.sprite")` in `on_init` and free it with `texture_free_slot(slot)` in `on_cleanup`.
5. Reference the slot from a `MATERIAL_TEXTURED` material or a `BillboardData`. `mesh_draw()` uploads it to TMEM every frame for the face groups that survive culling, but not again for the next group of the same draw if that uses the same slot and no mode change came in between (`tex_uploads` in the stats).

Gotchas:

- **`scene_cleanup()` frees only declared textures.** A hand-loaded slot that `on_cleanup` doesn't free leaks on every Reset Scene. That was D1: the demo re-loaded its cube faces by hand in `demo_init` and never freed them, 13.5 KB per reset (measured with Reset Soak), fixed in S1 by declaring them.
- A wrong path stops the debug ROM with libdragon's file-not-found assertion when the slot loads.
- Every PNG in `assets/` goes into the ROM whether it's used or not; `grass_tex.png` (128×128, unused, D22) would fail the TMEM assert.
- One format for all sprites (`MKSPRITE_FLAGS`); CI4 and TMEM residency are planned (P3.3). RGBA16 keeps one bit of alpha, which is all `Material.alpha_cutout` needs.
- The engine's quads assume 32×32 textures (UVs 0–32 in `cube.c`, `BB_TEX_SIZE` in `billboard.c`).
- Generated `.sprite` files are git-ignored and tied to the libdragon version: run `libdragon make clean` after a submodule change.

---

## Add a Start menu item

**Files:** `src/main.c` (options and items) and `src/scenes/demo_scene.c` (what the item does). Background: [MENU_SYSTEM.md](MENU_SYSTEM.md).

| Limit (`menu.h`) | Value | In use today |
|---|---|---|
| `MENU_MAX_TABS` | 6 | 6: Settings, Sound, Lighting, Environ, Controls, Debug |
| `MENU_MAX_ITEMS` (per tab) | 12 | 7, 3, 10, 6, 11, 11 |
| `MENU_MAX_OPTIONS` (per item) | 16 | extra options are dropped silently |
| `MENU_VISIBLE_ITEMS` | 7 | longer tabs scroll |

`menu_add_tab()` and `menu_add_item()` return -1 when full, and `menu_get_value()` returns 0 for an item that doesn't exist, so a failed add goes unnoticed. A seventh tab needs a larger `MENU_MAX_TABS`.

1. In `main.c`, add a file-scope options array (the menu keeps the pointers) and a `menu_add_item()` call at the **end** of the tab's list, so existing item indices stay valid:

   ```c
   static const char *shadow_options[] = {"Off", "Blob", "Projected"};
   ...
   menu_add_item(&start_menu, tab_l, "Shadows", shadow_options, 3, 0);   // Default: Off
   ```
2. In `demo_scene.c`, add `#define ITEM_<NAME> <index>` to that tab's block. The `TAB_` and `ITEM_` macros must match the order in `main.c`, and nothing checks it (D10; stage S8 plans a settings module with static asserts). A table that maps options to values (like `fog_near_values[]`) needs one entry per option.
3. Apply the value in `demo_update()` only when it changes (S3, D23), with a cache per item:

   ```c
   int fps_opt = menu_get_value(&start_menu, TAB_SETTINGS, ITEM_FRAME_RATE);
   if (fps_opt != last_fps_option) {
       switch (fps_opt) {
       case 0: engine_target_fps = 30; break;
       case 1: engine_target_fps = 0;  break;  // 60fps: no limiter needed (VSync caps it)
       }
       last_fps_option = fps_opt;
   }
   ```

   Reset the cache in `demo_init()`: Reset Scene re-runs it while the menu keeps its values. A cache reset to -1 forces an apply on the first update (the point lights, the Controls bindings and the Environ disabled states do this); a cache reset to the item's default skips that first apply, so the scene must already be in that state (the other blocks, like the frame rate above, work this way).
4. Optionally grey an item out with `menu_item_set_disabled(&start_menu, tab, item, true)`; the cursor can visit it but its value cannot change. Update disabled states on change too. Set values from code with `menu_set_value()`.
5. Test with Debug → Menu Sweep, which steps every option of every item (except the Controls and Debug tabs and Reset Scene), holding each for 15 frames. Run it with RDP Check on.

Gotchas:

- The demo reads its tabs every frame, so values apply while the menu is still open. B reverts every tab to the snapshot taken by `menu_open()`, and the demo re-applies the old values. Reset Scene is acted on only after the menu closes.
- Controls items are indexed by `GameAction` and their options by `PhysicalButton` (the demo loops over `ACTION_COUNT`). A new action needs a Controls item, and that tab has one slot left.
- Only the demo drives the Start menu (see [Add a scene](#add-a-scene)).

---

## Add a Debug tab item

**Files:** `src/debug/debug_menu.h`, `src/debug/debug_menu.c`, and `src/engine/engine.c` (tooling) or `src/main.c` (game logic, `app_frame()`) if the loop acts on the item. Background: [DEBUGGING.md](DEBUGGING.md).

1. Add `DBG_ITEM_<NAME>` to `DebugMenuItem` in `debug_menu.h`, at the end, before `DBG_ITEM_COUNT`.
2. At the end of `debug_menu_init()`, add a static options array and the `menu_add_item()` call. The order of the calls must match the enum: items are read by enum index and nothing checks it. The tab is full (12 of 12, `MENU_MAX_ITEMS`): a new item needs a slot freed or a second debug tab.
3. Handle the item in `debug_menu_update()`. The engine loop calls it every frame after the scene update, and it returns early while the menu is open, so values take effect when the menu closes with A (B reverts). Actions use the self-resetting `--- / Run!` pattern:

   ```c
   if (item_value(DBG_ITEM_RESET_SOAK) == 1) {
       item_set(DBG_ITEM_RESET_SOAK, 0);
       testbed_request_reset_soak();
       ENGINE_LOG("[debug] reset soak requested\n");
   }
   ```

   When the main loop must do the work (a CSV dump, a scene switch), set a flag and add a `debug_consume_<name>_request()` for the loop to call: tooling in `engine_run()` (like `debug_consume_dump_request()`), game logic in `app_frame()` in `main.c` (like `debug_consume_scene_request()`).
4. For release builds, put debug-only code under `#if ENGINE_DEBUG`, and grey the item out with `menu_item_set_disabled()` in the `#if !ENGINE_DEBUG` block of `debug_menu_init()`, as RDP Check, Profiler, RDP Log and Crash Test are. The item stays in the tab so the enum indices stay valid; `ENGINE_LOG` compiles to nothing.

Gotchas:

- Menu Sweep skips the Debug tab: test a new item by hand, in both variants.
- A handler that reads the joypad depends on the scene having called `action_update()`. The D-Up/D-Down shortcuts do, and fire only while no game action is bound to those buttons.
- Option lists that mirror an enum must stay in step with it: Bench with `BenchKind`, Overlay with `OverlayPage` (the only one guarded by a `_Static_assert`).

---

## Add a profiler slot

**Files:** `src/debug/profiler.h`, `src/debug/profiler.c`, the code you time, and `src/debug/overlay.c` to show it. Background: [PROFILING.md](PROFILING.md).

1. Add `PROF_<NAME>` to `ProfSlot` in `profiler.h`, before `PROF_SLOT_COUNT`, next to its parent.
2. Add its name and depth to `slot_info[]` in `profiler.c`. Depth 1 sits directly under `frame` (like `update`, `draw`), 2 inside `update` or `draw`, 3 inside `objects`. A missing entry leaves the name NULL.
3. Wrap the code (from `demo_post_draw()`):

   ```c
   PROF_BEGIN(PROF_PARTICLE_DRAW);
   particle_draw(&scene->camera);
   PROF_END(PROF_PARTICLE_DRAW);
   ```

   `PROF_BEGIN` declares a local variable, so a slot can be opened once per C scope; add braces to time the same slot twice in one function. Time and calls add up when a slot runs several times per frame, and a parent includes its children.
4. To see it on the overlay's Profiler page, add it to the `rows[]` list in `page_profiler()` in `overlay.c`. The page shows 13 slots, and its panel already ends at y = 192, near the demo HUD at the bottom of the screen; every row adds 10 px (so `dialog` is in the CSV rows only).
5. CSV: `PROF_HDR`, `PROF_AVG` and `PROF_PEAK` list every slot in enum order without further work, so a slot inserted mid-enum shifts the columns after it; read captures by header name. `BENCH_PROF` reports a fixed list of slots (`finish_step()` in `benchmark_scene.c`); append the new slot there and to `BENCH_PROF_HDR` together if the benchmark should report it (S5.3 appended `dialog_us`).
6. Add the slot to the tree in PROFILING.md.

Gotchas:

- Scopes compile out of release builds (`ENGINE_PROFILE=0`) and are skipped while Debug → Profiler is Off. `frame`, `wait_display` and `limiter` are measured either way.
- A scope costs two tick-counter reads, and the whole profiler costs about 0.1 ms per frame on the A3D (BENCHMARKS.md). Keep new scopes out of per-triangle loops.

---

## Add a stats counter

**Files:** `src/debug/stats.h`, `src/debug/stats.c`, the code that produces the count, and `src/debug/overlay.c` to show it. Background: [PROFILING.md](PROFILING.md).

1. Add a `uint32_t` field to `EngineStats` in `stats.h`.
2. Count where the work happens: `STATS_INC(field)` or `STATS_ADD(field, n)` for per-frame totals, `STATS_SET(field, v)` for levels such as `colliders` or `particles_alive`. `stats_frame_begin()` zeroes the counters at the top of every loop iteration, and `stats_get()` returns the last complete frame.
3. CSV: append the column name at the **end** of the `STATS_HDR` string and the value at the end of the `STATS` `debugf()` in `stats_dump_csv()`, so older captures keep their column positions (`tris_backface` was appended last in S2). `-Wall -Werror` checks the `STATS` format against its arguments; nothing checks the header.
4. Overlay: `page_stats()` prints nine lines of at most 26 characters (`TEXT_COLS`). Fit the value into a line, or add a line and raise `rows`.
5. Add the counter to the glossary in PROFILING.md.

Gotchas:

- `stats.h` has no libdragon dependency and `stats.c` is linked into the host tests, so pure-C modules can count and tests can check the result (`test_collision.c` checks `raycasts`).
- Counters stay on in release builds (`ENGINE_STATS` defaults to 1).

---

## Add a benchmark kind

**Files:** `src/scenes/benchmark_scene.h`, `src/scenes/benchmark_scene.c`, `src/debug/debug_menu.c`. Background: [BENCHMARKS.md](BENCHMARKS.md).

1. Add `BENCH_<NAME>` to `BenchKind` before `BENCH_KIND_COUNT`. The Debug tab's Bench option index is cast straight to `BenchKind`, so the enum order is the menu order.
2. In `benchmark_scene.c`:
   - `kind_names[]`: the CSV `kind` column, and half of the key `bench_compare.py` matches steps by.
   - `kind_desc[]`: the on-screen description, with one `%d` for the step parameter.
   - `build_steps()`: add the steps, inside `if (all || which == BENCH_<NAME>)`, or `if (which == BENCH_<NAME>)` to keep them out of All as Overload does. `MAX_STEPS` is 32 and All uses 26; `add_step()` drops extra steps silently.

     ```c
     if (all || which == BENCH_LIGHTS) {
         static const int n[] = {0, 1, 2, 4};
         for (unsigned i = 0; i < sizeof(n) / sizeof(n[0]); i++) add_step(BENCH_LIGHTS, n[i]);
     }
     ```
   - `setup_step()`: a `case` that sets the scene up for one step. Its preamble resets emitters, fill layers, instances, the CPU burn, the floor and the lighting before every step; reset any state you add there too.
   - `bench_update()`, `bench_draw()`, `bench_post_draw()`: the load itself. Keep it deterministic: the camera path is frame-locked and particles update with a fixed 1/60 s step.
   - `bench_cleanup()`: free what the kind allocated.
3. In `debug_menu.c`, add the name to `bench_options[]` at the same index and raise the count in `menu_add_item(menu, tab, "Bench", bench_options, 11, 0)`.
4. Run it: Start → Debug → Bench = the new kind, Scene = Benchmark, A. Every step runs 60 warm-up and 240 measured frames, and the scene fades back to the demo at the end. `libdragon make BENCH=1 BENCH_KIND=<NAME>` builds a ROM that boots straight into it.

| Row | Printed | Contents |
|---|---|---|
| `BENCH_META`, `BENCH_HDR`, `BENCH_PROF_HDR` | at the start | build, date, RDRAM size, kind, step count; column names |
| `BENCH` | every step | kind, step, param, frames, fps, avg and p99 ms, 1 % low, CPU avg and max, RDP busy ms and %, tris, uploads, heap KB |
| `BENCH_PROF` | every step, profiler on | average µs of `update`, `draw`, `objects`, `mesh_cull`, `mesh_light`, `mesh_tris` |
| `BENCH_LAYOUT` | once per run | addresses of `bench_draw()`'s stack frame and of the pillar's vertex and index data (D26) |
| `BENCH,END` / `BENCH,ABORTED` | at the end / on Start | step count and seconds / step index |

Gotchas:

- **`bench_compare.py` matches steps by (kind, param)**, so params must be unique within a kind. The S2 Mesh A/B encoded its variant in the param (16, 1016 and 2016 for three variants at 16 pillars).
- **The `BENCH` row is fixed.** `bench_compare.py` reads only `BENCH` rows with exactly 15 fields and skips any others. Put new data in a new row type, as `BENCH_PROF` and `BENCH_LAYOUT` did.
- `debugf` is compiled out of release builds: `(void)` the values you only log (see `finish_step()`) and capture with the debug ROM.
- A per-step `ParticleEmitterDef` needs static storage. The benchmark copies `bench_particles` into the static `step_particles`; a stack copy produced no particles at all.

---

## Add a host unit test

**Files:** `tests/host/test_<name>.c` (new), `tests/host/test.h`, `tests/host/test_main.c`, `tests/host/Makefile`. Background: [DEBUGGING.md](DEBUGGING.md).

1. Write the test file, including engine headers by their path under `src/`. From `test_particle.c`:

   ```c
   #include "test.h"
   #include "render/particle_internal.h"

   static void test_particle_continuous(void) {
       particle_init();
       int e = particle_emitter_create(&def_stream, (vec3_t){0, 0, 0}, 2);
       particle_emitter_set_active(e, true);
       particle_update(0.35f);                        // 3.5 due, the slice holds 2
       CHECK(particle_alive_count() == 2);
       CHECK(particle_emitters[e].spawn_accum == 0.0f);   // backlog dropped when full
       particle_cleanup();
   }

   void run_particle_tests(void) {
       RUN_TEST(test_particle_continuous);
   }
   ```

   `CHECK(cond)` and `CHECK_NEAR(a, b, eps)` print `FAIL <test> (<file>:<line>)` and let the run continue.
2. Declare `void run_<name>_tests(void);` in `test.h` and call it from `main()` in `test_main.c`.
3. In `tests/host/Makefile`, add the test file to `TEST_SRCS` and the engine source under test to `ENGINE_SRCS` (`$(SRC)/<dir>/<file>.c`).
4. Run `libdragon exec make -C tests/host run`. It prints `<n> checks, <m> failures` (123 checks at Phase 2 S3) and exits 1 on any failure; CI runs it through `tools/ci_build.sh`.

The shim (`tests/host/shim/libdragon.h` and `shim.c`) stands in for `<libdragon.h>`: `color_t` and `RGBA32`, `debugf` (a no-op), `assertf` (plain `assert`), `TICKS_READ()` (always 0), and the joypad API, driven by `shim_joypad_set()`. Rendering, display, RSP/RDP, audio and filesystem calls are deliberately missing: code that needs them isn't host-testable. Move the pure logic into its own file instead, as `mesh_build.c` was split from `mesh.c` and `particle.c` from `particle_draw.c`. `ENGINE_HOT` and `ENGINE_NOINIT` expand to nothing on the host.

Gotchas:

- Compiled with `-std=gnu17 -Wall -Werror`: a warning fails the run.
- Objects go into one flat `tests/host/build/` directory named after the source file, so a new file can't share a basename with any other file in `TEST_SRCS` or `ENGINE_SRCS`.
- `TICKS_READ()` returns 0, so `particle_init()` seeds `rand()` the same way on every run. Still prefer inputs that don't depend on random values (equal min and max).
- Module state is global: initialise it at the start of each test (`particle_init()`, `collision_world_init()`, `action_init()`).

---

## Add per-triangle render code

**Why this needs care.** The VR4300's instruction cache is 16 KB, direct-mapped, with 32-byte lines: two functions whose addresses are equal modulo 16 KB share cache lines and evict each other. When the first S2 triangle loop landed on all 46 lines of libdragon's `rdpq_triangle_rsp`, every triangle paid about 1,100 extra cycles (+38 % CPU, D25), and unrelated edits moved benchmark results by about 6 % as code shifted. So the per-triangle path is linked as one contiguous "hot text" block at the start of `.text`. Details: [HARDWARE.md](HARDWARE.md), section "CPU caches and code placement".

**Files:** your render `.c` file, `src/engine/hot_text.ld`, `tools/hot_text.py`.

1. Include `../engine/hot.h` and mark the loop, and every function it calls per triangle or per group, `ENGINE_HOT`.
2. Mark local scratch arrays that are fully written before they're read `ENGINE_NOINIT`. libdragon compiles with `-ftrivial-auto-var-init=pattern`, which otherwise fills them on every iteration:

   ```c
   float screen[4][3] ENGINE_NOINIT;
   ```
3. Add the object file to `src/engine/hot_text.ld`, which selects code **by object file name** (`*render/<file>.o(.text.engine_hot)`) or, for libdragon, by function section. Unlisted `ENGINE_HOT` code lands in the catch-all at the end. **Order matters: the block is larger than 16 KB, so its tail wraps onto its head.** The head holds the floor, shadow and particle loops and the tail holds `mesh_draw`, which never runs at the same time as them. Renaming or splitting a render file means editing this list; the S3 particle split replaced `particle.o` with `particle_draw.o`.
4. A new drawing loop is a new phase: add its root function(s) to `PHASES` in `tools/hot_text.py`, for example `"decal": ["decal_draw"]`. Each phase is the roots plus the triangle path (`rdpq_triangle`, `rdpq_triangle_rsp`, `floorf`, `floor`) and the per-group rdpq helpers, extended transitively by every callee that lies inside the block (calls through function pointers are declared in `INDIRECT`). The tool fails if one of those functions lies outside the block or two of them need different lines at the same cache index. Callees outside the block are printed as notes, except those in `COLD_OK`: rare paths (buffer switches, asserts, logging) and `texture_upload`, whose libdragon path is too large to fit in the mesh phase.
5. Verify with `libdragon exec bash tools/ci_build.sh`, which checks both ELFs, or directly:

   ```powershell
   libdragon exec python3 tools/hot_text.py build/debug/engine-debug.elf --verbose
   ```

   The first output line gives the block size and a status per phase.
6. Benchmark the change; near the 5 % limit, use a same-ROM A/B (see [Contributing a change](#contributing-a-change)).

Gotchas:

- `ENGINE_NOINIT` on an array that is read before it's written reads garbage.
- Keep setup, logging and rare paths out of `ENGINE_HOT`: every byte moves the wrap point. Texture uploads stay outside the block on purpose: their ~7 KB of libdragon code would push the mesh phase to about 17 KB and onto `rdpq_triangle_rsp`'s lines, so `mesh_draw()` avoids repeat uploads instead.
- The Makefile inserts `hot_text.ld` into libdragon's `n64.ld` after the boot code (as `build/<variant>/engine.ld`) and stops if that anchor is missing, which a libdragon upgrade could cause.

---

## Add a sound

**Files:** `assets/audio/sfx/` or `assets/audio/music/`, `src/audio/sound_bank.h`, `src/audio/sound_bank.c`, and the code that plays the sound. Background: [AUDIO.md](AUDIO.md).

1. Put the WAV in `assets/audio/sfx/` (effects, **mono**) or `assets/audio/music/` (music). The Makefile converts it with `audioconv64` (VADPCM by default: `AUDIOCONV_SFX_FLAGS` / `AUDIOCONV_MUSIC_FLAGS`) to `filesystem/audio/<sfx|music>/<name>.wav64`.
2. Add an id to `SoundId` in `sound_bank.h`: an effect next to the other effects (between `SFX_MENU_OPEN` and `SFX_COLLISION` it joins the Bench = Audio effect load), music next to `BGM_DEMO`.
3. Add its row to `sound_bank[]` in `sound_bank.c`: path, bus, volume (0–1), priority, and for a positional effect the distance at which it starts to fade and the distance at which it is silent (0, 0 otherwise):

   ```c
   [SFX_DOOR_OPEN] = { "rom:/audio/sfx/door_open.wav64", SND_BUS_SFX, 0.80f, PRIO_WORLD, 150.0f, 1200.0f },
   [BGM_TOWN]      = { "rom:/audio/music/town.wav64",    SND_BUS_MUSIC, 0.65f, 0, 0, 0 },
   ```
4. Play it: `snd_play(id)` (UI sounds, centred), `snd_play_at(id, position, gain)` (world sounds, panned and attenuated from the listener), or `snd_music_play(id, fade_s)` (music: loops and crossfades from the current track).
5. Build, then listen with Sound → Master = On: the demo boots muted.

Gotchas:

- A missing file, or a path naming the `.wav` instead of the `.wav64`, asserts in debug builds (at boot for an effect, since `snd_init()` opens every SFX; when it starts for music). Release builds skip the sound.
- Effects must be mono (debug builds assert at boot otherwise; a stereo effect would take the next voice's channel) and share one encoding: a voice's sample ring is reused from effect to effect, and libdragon asserts when a ring moves between encodings after Opus or ULC (D31). Music can mix encodings.
- Any sample rate plays: `snd_init()` raises the channel limits to the fastest sound (D30). Rates above the 22,050 Hz output only cost memory and mixer time, so resample them (`--wav-resample 22050` in the `AUDIOCONV_*` flags) unless the source needs them.
- An effect only takes a voice from a sound of equal or lower priority; when all eight voices hold more important sounds it is dropped (`snd_stats()`). UI sounds use `PRIO_UI`, world sounds `PRIO_WORLD`.
- Positional gains are set when the sound starts; keep the listener current with `snd_set_listener()` every frame (the demo passes its camera).
- Music must be wav64: the Makefile also converts `*.xm` to `.xm64`, but nothing plays it. Opus music needs `SND_OPUS=1` (AUDIO.md).
- Generated audio is git-ignored and tied to the libdragon version: after a submodule change run `libdragon make clean` (a stale file asserts `invalid version`). Changing an `AUDIOCONV_*` flag does not re-encode files that already exist; clean then too.

---

## Add dialog

**Files:** `assets/dialog/<name>.json`, the scene that plays it. Background: [DIALOG.md](DIALOG.md).

1. Write the conversations in `assets/dialog/<name>.json` (format: DIALOG.md "Source format"; `assets/dialog/demo.json` shows every feature). The Makefile compiles each JSON file to `rom:/dialog/<name>.dlg`; there is nothing to register.
2. In the scene: load the bank in `on_init` (`dialog_bank_load()`), `textbox_init()` a `TextBox`, and free both in `on_cleanup` (`textbox_close()`, `dialog_bank_free()`): Reset Soak must still show a zero heap delta.
3. Write the hooks for the events, conditions and variables the JSON names. Log unknown names (`ENGINE_LOG`), so a typo in the JSON shows in the USB log.
4. Start a conversation with `dialog_start()` + `textbox_open()`. While `textbox_active()`: build a `UiInput` from the buttons, call `textbox_update()`, and skip the scene's own input (and Start); call `debug_menu_set_shortcuts(false)` if the box reads the D-pad. Draw it in `on_post_draw` with `textbox_draw(&box, style)` inside a `PROF_DIALOG` scope.
5. Build: a JSON mistake stops `make` with its location. Test in ares, then on the A3D in every UI style.

Gotchas:

- Hooks run inside `dialog_start()`, `dialog_advance()` and `dialog_choose()` (when a line starts), so an event handler must not start another conversation on the same runner.
- The dialog font (`FONT_UI_VAR`) covers ASCII 0x20–0x7E only; the compiler rejects anything else.
- A text box covers the bottom of the screen: hide or move HUD elements under it (the demo hides its bottom band).
- Render any other laid-out paragraph with `text_render_paragraph()`, not `rdpq_paragraph_render()` (UI.md, "Text and the render mode").

---

## Contributing a change

Every change passes the stage gate of [ROADMAP_v2.md §6.4](ROADMAP_v2.md#64-hardening-test-plan) before it is committed, one stage per commit:

1. **CI script.** `libdragon exec bash tools/ci_build.sh` builds both ROMs, runs the host tests, checks the budgets (`rom_budget.py`: release ROM ≤ 4096 KB, text + data + bss ≤ 1024 KB) and checks the hot-text layout. GitHub Actions runs the same script on every push.
2. **ares smoke test.** The ROM boots, the menu works, and the change is visible where it should be.
3. **A3D checklist.** The items of ROADMAP_v2 §11.2 for the areas touched, run on the console (`sc64deployer upload`, reset, `sc64deployer debug` for the log). ares is lenient; the Analogue 3D decides.
4. **Benchmark.** Run the affected kinds (or All) with the debug ROM on the A3D and compare the capture with the previous stage's CSV:

   ```powershell
   sc64deployer debug | Tee-Object capture.log      # then run the benchmark on the console
   libdragon exec python3 tools/bench_compare.py docs/benchmarks/2026-09-23-p2-s2-objects-debug-a3d.csv capture.log
   ```

   `bench_compare.py` reads UTF-8 or UTF-16 files (Windows PowerShell 5.1's `Tee-Object` and `>` write UTF-16) and ignores everything but `BENCH` rows, so the raw log works. A step regresses when its CPU average grows by more than 5 % **and** more than 0.15 ms (`--threshold`, `--noise-ms`), or when it held 60 FPS (≥ 59.5) and no longer does. Exit status 0 = clean, 1 = regression, 2 = bad input. No regression above 5 % unless the stage intends it; paste the output into the commit message.
5. **Record it.** Add a BENCHMARKS.md row with the stage's proof and commit the `BENCH` lines as `docs/benchmarks/<date>-p2-s<N>-debug-a3d.csv`, for example `Select-String '^BENCH' capture.log | % Line | Set-Content -Encoding utf8 <file>`. Each stage's CSV is the next stage's comparison point (a moving baseline); the 2026-09-23 baseline stays the reference for the Phase 2 exit.

**Unattended runs.** `libdragon make BENCH=1` builds `engine-debug-bench.z64` in `build/debug-bench` (a separate directory, because make doesn't track `CFLAGS`); it boots straight into All, or into one kind with `BENCH_KIND=<kind>` (the `BenchKind` name without `BENCH_`, e.g. `AUDIO`), and returns to the demo afterwards. On the A3D the first step right after boot can be slow (D32). `libdragon make BUILD=release BENCH=1` gives `engine-bench.z64` in `build/release-bench`, but release builds print no CSV, so capture with the debug variant.

**When to use a same-ROM A/B.** Timings depend on code and data layout, not only on the code you changed. Separate builds moved by about 6 % from unrelated edits (I-cache placement, D25), and identical code running on two copies of the same mesh data differed by up to 9 % (D-cache aliasing, D26). When a render change lands near the 5 % limit, build the old and the new code into **one** ROM, run them interleaved as extra benchmark steps with distinct params, decide, then remove the scaffolding. The S2 Mesh A/B did this: commit 44203e6 is the measured build, 95e2d40 removed it, and the method and results are in BENCHMARKS.md (Phase 2 · S2) and HARDWARE.md.
