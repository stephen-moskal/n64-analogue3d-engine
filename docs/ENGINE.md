# Engine Core

`src/engine/` holds what every game built on the engine shares: the build constants, hardware bring-up and the frame loop. The game (today the demo, in `src/main.c`) only builds its menus and scenes and hands them over.

| File | Role |
|---|---|
| `engine_config.h` | build constants: screen size, framebuffer count, max dt, guard band |
| `engine.c/h` | `engine_init()` (hardware and subsystems), `engine_run()` (the frame loop), frame-rate cap, shared Z-buffer |
| `hot.h`, `hot_text.ld` | I-cache placement of the render hot path ([HARDWARE.md](HARDWARE.md)) |

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
  dt = time since the previous iteration (capped at ENGINE_MAX_DT)
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
  30 FPS cap: busy-wait to 33.3 ms (profiler: limiter)
```

`engine_target_fps` (0 = the display's 60, or 30) is set by the game (the demo's Settings → Frame Rate); `engine_frame_budget_ms()` gives 16.67 or 33.33 for gauges and CSV dumps.

## Planned (Phase 2 S6)

- S6.2 frame pacing: `display_set_fps_limit()` instead of the busy-wait, dt from `display_get_delta_time()`, the Z-buffer from `display_get_zbuf()`; loop-time jitter (D19) and the slow first seconds after boot (D32).
- S6.4: frame overruns under the RDP validator (D18).

## Source files

| File | Purpose |
|---|---|
| [src/engine/engine_config.h](../src/engine/engine_config.h) | build constants |
| [src/engine/engine.h](../src/engine/engine.h), [src/engine/engine.c](../src/engine/engine.c) | init and the frame loop |
| [src/main.c](../src/main.c) | the demo game: Start menu, scenes, scene switches |
