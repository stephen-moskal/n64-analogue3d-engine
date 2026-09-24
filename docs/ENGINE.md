# Engine Core

`src/engine/` holds what every game built on the engine shares: the build constants, hardware bring-up and the frame loop. The game (today the demo, in `src/main.c`) only builds its menus and scenes and hands them over.

| File | Role |
|---|---|
| `engine_config.h` | build constants: screen size, framebuffer count, max dt, guard band |
| `engine.c/h` | `engine_init()` (hardware and subsystems), `engine_run()` (the frame loop), frame-rate cap, shared Z-buffer, presented-frame tracking |
| `hot.h`, `hot_text.ld` | I-cache placement of the render hot path ([HARDWARE.md](HARDWARE.md)) |
| `hot_data.ld` | D-cache placement of the render path's static data: fixed colours away from the stack (D34) |
| `engine_ld.awk` | builds `build/<variant>/engine.ld` from libdragon's `n64.ld` with both fragments |
| `layout_pad.c` | `make LAYOUT_PAD=<bytes>`: unused code that shifts all data, for layout-stability tests |

## Using it

```c
int main(void) {
    engine_init();                       // display, rdpq, DFS, input, text, audio, atmosphere, Z-buffer

    // the game's own setup: the Start menu, the scene manager, the first scene
    menu_init(&start_menu, "Start Menu");
    ...
    scene_manager_init(&scene_mgr);
    scene_manager_switch(&scene_mgr, demo_scene_get(), TRANSITION_CUT, 0);

    engine_run(&(EngineApp){ .scenes = &scene_mgr, .menu = &start_menu, .on_frame = app_frame });
}
```

`EngineApp`:

| Field | Meaning |
|---|---|
| `scenes` | the scene manager the loop updates and draws |
| `menu` | the Start menu; the debug overlay hides while it is open (NULL: never hidden) |
| `on_frame(dt)` | the game's per-frame logic outside scenes, called after the scene update and the debug tooling, before rendering. The demo switches scenes there (Debug → Scene, and back to the demo when a benchmark ends) |

## Build constants (`engine_config.h`)

| Constant | Value | Used by |
|---|---|---|
| `ENGINE_SCREEN_W`, `ENGINE_SCREEN_H` | 320, 240 | display, Z-buffer, viewport maps in mesh/floor/shadow/particle, sky strips, camera aspect, full-screen fades |
| `ENGINE_FB_COUNT` | 3 | display (triple buffering), memory stats |
| `ENGINE_MAX_DT` | 0.1 s | the loop caps dt so a stall never becomes one huge physics step |
| `ENGINE_GUARD_MARGIN` and `ENGINE_GUARD_X/Y_MIN/MAX` | 1024 px beyond each screen edge | triangles with a vertex outside are dropped or clipped: further out the RDP's fixed-point edge maths overflows |

They are compile-time constants, so the render loops fold them exactly like the literals they replaced (S6.1 left the hot-text block byte-for-byte the same size). Before S6.1 (defect D9) they were copied as literals into nine files.

## The frame

```
engine_run():
  dt = display_get_delta_time() (capped at ENGINE_MAX_DT)
  publish last frame's profiler / frame-time / stats, start this frame's
  scene_manager_update(dt)                  scene on_update: input, logic, camera, collision
  debug_menu_update(), testbed_update()     Debug tab, Reset Soak, Menu Sweep
  CSV dump if requested
  app->on_frame(dt)                         game logic (scene switches)
  reset peaks if requested
  audio (poll point: before display_get)
  display_get()                             wait for a free framebuffer (profiler: wait_display)
  audio (default poll point: right after display_get; AUDIO.md)
  rdpq_attach(fb, zbuf); scene_manager_draw(); overlay; testbed status
  rdpq_detach_show()
  audio (poll point: after present)
```

## Pacing and time (S6.2)

- **Frame-rate cap.** `engine_set_fps_limit(30)` (or 0 for the display's 60) calls libdragon's `display_set_fps_limit()`: the display module shows a new frame only every other vblank and `display_get()` waits for a free buffer, so the CPU sleeps in `wait_display` instead of spinning. The demo's Settings → Frame Rate sets it; `engine_frame_budget_ms()` gives 16.67 or 33.33 for gauges, CSV dumps and the benchmark. The `limiter` profiler slot (the old busy-wait) stays in the CSV columns and reads 0.
- **dt** is `display_get_delta_time()`: libdragon's filtered time between presented frames, a whole number of vblanks. With triple buffering the loop runs up to two frames ahead of the screen, and its own iterations alternate ~12.5 / ~21 ms at a steady 60 FPS (D19); the old dt copied that jitter into every animation and physics step. The loop's wall time still feeds the profiler and the frame-time window.
- **Z-buffer** from `display_get_zbuf()`: allocated from the top of RDRAM, in a different memory bank from the framebuffers (libdragon notes a speed gain for the RDP).
- **Presented frames.** A vblank handler (`on_vblank`, installed after the display's own) watches `VI_ORIGIN` and reports how many vblanks each frame stayed on screen (`frametime_record_present`). At a steady 60 every frame shows for 1 vblank; a frame shown longer than the target (1 at 60, 2 at 30) is **late**, a hitch the player can see. The handler also reads the VI's current half-line: a flip after the start of the active picture (`VI_V_VIDEO`) is **torn**, the lines below it from the new frame (D18: only with the RDP validator on, see DEBUGGING.md). Frame overlay page: "Shown late N of M" and the 1/2/3+ vblank counts; CSV: `FTP` rows with the dump, `BENCH_PRESENT` rows per benchmark step.
- **Boot log** (D32): debug builds print a `BOOT` row per second for the first 20 s (frame, wait_display, update, draw, audio and RDP busy ms, averaged over that second). It showed that on the A3D every reset is followed by ~7.5 s of slower CPU, RSP and RDP (HARDWARE.md), so boot-to-benchmark ROMs wait 15 s before their first step.

The four display APIs above are still marked preview in libdragon; `engine.c` is the only file that uses them, with the deprecation warning silenced there.


## Source files

| File | Purpose |
|---|---|
| [src/engine/engine_config.h](../src/engine/engine_config.h) | build constants |
| [src/engine/engine.h](../src/engine/engine.h), [src/engine/engine.c](../src/engine/engine.c) | init and the frame loop |
| [src/main.c](../src/main.c) | the demo game: Start menu, scenes, scene switches |
