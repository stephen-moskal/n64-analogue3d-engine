# Input

Controllers, players and actions (ROADMAP_v2 S9, D12). Game code asks about **actions** ("was Jump pressed by player 2?"), not buttons. Each player reads one controller port through a stack of **contexts** that bind inputs to actions. Menus, dialogs, the debug shortcuts and the benchmark all go through the same path. For full control, a port's raw state is one call away. The engine polls the controllers at the point of the frame that gives the lowest input lag, and measures that lag.

```
vblank ─► libdragon reads all 4 ports (SI + PIF, ~1.8 ms)
                │
engine loop: display_get() ─► audio mix ─► input_poll()            src/input/input.c
                                  │  waits for this vblank's read (INPUT_SYNC_FRESH)
                                  ▼
                        PadState[4]: buttons + edges, sticks, triggers,
                        controller type, accessory, rumble        src/input/pad.h  ◄── input_pad(port)
                                  │
                        action_update(): per player, contexts     src/input/action.c (pure, host-tested)
                        from the highest priority down
                                  │
          action_pressed / held / released / repeat / value (player, id)    ◄── game code
          action_ui(player) ─► UiInput ─► menu_update(), textbox_update()   ◄── UI
```

## Quick start

A game defines its actions after the engine's, a table of default bindings and a context, then asks about actions:

```c
#include "input/input.h"        // the core (pads, rumble, latency); includes action.h

enum { ACT_JUMP = ACTION_GAME_FIRST, ACT_ATTACK, ACT_SPECIAL, ACT_MOVE_X, ACT_MOVE_Y, ACT_PAUSE };

static const ActionBinding play_defaults[] = {
    BIND(ACT_JUMP, BTN_A),
    BIND(ACT_ATTACK, BTN_B),
    BIND_CHORD(ACT_SPECIAL, BTN_Z, BTN_B),          // Z held, then B (Z+B does not also fire ACT_ATTACK)
    BIND_AXIS(ACT_MOVE_X, AXIS_STICK_X, 1),         // analog
    BIND_NEG(ACT_MOVE_X, BTN_D_LEFT), BIND(ACT_MOVE_X, BTN_D_RIGHT),   // or digital -1 / +1
    BIND_AXIS(ACT_MOVE_Y, AXIS_STICK_Y, 1),
    BIND(ACT_PAUSE, BTN_START),
};

static ActionContext play[ACTION_PLAYERS];          // one per player: each can be remapped alone

void game_init(void) {
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        action_context_init(&play[p], "Play", 0, CTX_CONSUME, play_defaults, ARRAY_LEN(play_defaults));
        action_push_context(p, &play[p]);
    }
}

void game_update(float dt) {                        // the scene's on_update: input is already polled
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        if (!action_connected(p)) continue;
        if (action_pressed(p, ACT_JUMP)) jump(p);
        if (action_pressed(p, ACT_SPECIAL)) special(p);
        move(p, action_value(p, ACT_MOVE_X), action_value(p, ACT_MOVE_Y));   // -1..1
        if (action_held_time(p, ACT_ATTACK) > 0.5f) charge(p);
    }
}
```

Scenes never poll: the engine calls `input_poll()` once per frame before `scene_manager_update()`. **Never call `joypad_poll()` or `joypad_get_*()`**: the input core samples the joypad module from the vblank interrupt as well, so direct calls would race with it. Use `input_pad()` for raw state.

## Pads: full control

`input_pad(port)` returns the frame's `PadState` for a port (0–3):

| Field | Meaning |
|---|---|
| `style` | `PAD_NONE` (empty port), `PAD_N64`, `PAD_GCN`, `PAD_MOUSE` |
| `accessory` | N64 controller slot: `PAD_ACC_RUMBLE_PAK`, `PAD_ACC_CONTROLLER_PAK`, `PAD_ACC_TRANSFER_PAK`, ... |
| `rumble` | has a motor (Rumble Pak, GameCube controller); detection is asynchronous, a few frames after plugging |
| `connected`, `disconnected` | hot-plug edges this frame |
| `held`, `pressed`, `released` | `PAD_BIT(btn)` masks. `pressed` and `released` cover every read since the last frame, so a tap between two frames shows in both |
| `stick_x`, `stick_y` | raw main stick (N64 about ±80, GameCube ±100) |
| `cstick_x`, `cstick_y` | GameCube C-stick (N64: ±76 from the C buttons) |
| `trigger_l`, `trigger_r` | GameCube analog triggers 0–200 (N64: 0 or 200 from L/R) |

Buttons (`PadButton`): `BTN_A, BTN_B, BTN_Z, BTN_START, BTN_D_UP/DOWN/LEFT/RIGHT, BTN_L, BTN_R, BTN_C_UP/DOWN/LEFT/RIGHT, BTN_X, BTN_Y` (X and Y: GameCube only). `pad_button_name()` gives "A", "C-Up", ...

`input_port_pressed(BTN_START)` returns the first port that pressed a button this frame, for a "press Start to join" screen.

## Actions

An `ActionId` is a small number: the engine owns 0–15 (`ACTION_UI_*`, `ACTION_DEBUG_*`), and games number theirs from `ACTION_GAME_FIRST` (16) up to `ACTION_MAX` (48). `action_set_names()` registers names for menus and the Input overlay page.

| Query | Answer |
|---|---|
| `action_pressed(p, a)` | a bound input went down this frame (a tap counts) |
| `action_held(p, a)` | an active binding is down |
| `action_released(p, a)` | it was held and is not any more, or a tap ended |
| `action_repeat(p, a)` | on the press, then every 0.10 s after 0.30 s held (`action_set_repeat`) |
| `action_value(p, a)` | -1..1: the strongest active binding (digital: its sign; axis: the processed value), 0 when idle |
| `action_held_time(p, a)` | seconds since the press, 0 when idle |
| `action_chord_pressed(p, a, b)` | both held, one of them pressed this frame |
| `action_any_pressed(a, &p)` | any player; `p` gets the first |

Several bindings can drive one action; any active one holds it. A new press of a second binding while the first is held counts as a press.

### Bindings

| Initializer | Input | Value while active |
|---|---|---|
| `BIND(act, btn)` | a button | +1 |
| `BIND_NEG(act, btn)` | a button | -1 (the other side of an axis action) |
| `BIND_CHORD(act, mod, btn)` | `btn` pressed while `mod` is held | +1 |
| `BIND_STICK(act, dir)` | the main stick as a direction: `STICK_UP/DOWN/LEFT/RIGHT` | +1 |
| `BIND_AXIS(act, axis, sign)` | `AXIS_STICK_X/Y`, `AXIS_CSTICK_X/Y`, `AXIS_TRIGGER_L/R` | the processed axis × sign |

These are brace initializers for tables. At run time use `action_context_add(ctx, (ActionBinding)BIND(...))`. A chord can have several modifiers: set `ActionBinding.modifiers` to a `PAD_BIT` mask.

**Chords.** The modifier must be held when the trigger goes down. Inside one context, an active chord takes its trigger button from that context's plain bindings: Z+B fires `ACT_SPECIAL`, not also `ACT_ATTACK`. The modifier's own binding (say Z → lock-on) still fires, because it went down first. Below a consuming context, an active chord's modifier and trigger are both hidden.

**Stick directions** are 4-way (the dominant axis): on past 0.5 of full deflection, off again below 0.35, so they do not flicker at the threshold.

## Contexts

An `ActionContext` is a named binding table the game owns and can change at any time (remapping). Each player has a stack of up to 6. They are evaluated from the highest `priority` down, and among equal priorities the newest first:

| Flag | Effect on the contexts below |
|---|---|
| `CTX_CONSUME` | the inputs this context's **active** bindings use are hidden (a button nothing here uses passes through) |
| `CTX_MODAL` | every input is hidden, bound or not |
| (none) | nothing is hidden: both contexts see the input |

| Function | |
|---|---|
| `action_context_init(ctx, name, priority, flags, table, count)` | fill from a table |
| `action_context_add` / `action_context_remove` | one binding / every binding of an action |
| `action_context_button(ctx, a, n)` / `action_context_set_button(ctx, a, n, btn)` | read / remap an action's n-th button binding (adds one past the end) |
| `action_push_context(p, ctx)` / `action_pop_context(p, ctx)` | onto / off a player's stack (pushing twice is harmless) |
| `action_clear_contexts(p)`, `action_has_context`, `action_context_count`, `action_context_at(p, i)` | stack management, inspection (0 = top) |

A context change takes effect at the next frame's update. When a context above hides an action that was held, the action reads released. When a context above goes away while a button is still down, the action below reads held but not pressed, and it does not repeat. Closing a menu with B therefore does not also fire the game's B.

The engine's contexts:

| Context | Priority | Flags | Bindings | Who pushes it |
|---|---|---|---|---|
| `action_ctx_ui` | 100 | modal | D-pad and stick move (repeating), A confirm, B cancel, L/R tabs, Start | the game, for the player driving a menu or dialog; popped when it closes |
| `action_ctx_debug` | -100 | consume | D-Up next overlay page, D-Down CSV dump | the Debug tab, for player 1 at boot |

Because the debug context sits below every game context, a game binding on D-Up or D-Down wins and the shortcut stays quiet. That is the rule the old `button_is_free()` implemented, and it now falls out of the stack.

## Players and ports

`ACTION_PLAYERS` (4) players read ports 1–4 by default. `action_set_port(p, port)` maps any player to any port (-1: none; two players may share one). `action_connected(p)` says whether the player's port has a controller. A player on an empty port reads every action idle. Hot-plug shows as `PadState.connected` / `disconnected` for one frame.

Patterns:
- **Per-player remapping**: one context per player, built from the same table (above).
- **Whoever pauses drives the menu**: `action_any_pressed(ACT_PAUSE, &p)`, push `action_ctx_ui` for `p`, read `action_ui(p)`, pop it when the menu closes. The demo does this with its Start menu.
- **Join screen**: `input_port_pressed(BTN_START)`, then `action_set_port(next_player, port)`.

## Analog

Sticks are processed per player (sticks wear differently) with a `StickConfig` (`action_set_stick`):

| Field | Default | |
|---|---|---|
| `deadzone` | 0.10 | fraction of full deflection that reads as centred |
| `outer` | 0 = by controller | raw value counted as full deflection: N64 80, GameCube 90 |
| `curve` | 1 | response exponent (2: finer near the centre) |
| `axial` | false | a deadzone per axis (a cross) instead of radial (a circle) |

The radial deadzone keeps the direction and rescales the magnitude to 0..1 past the deadzone, capped at 1 (an N64 gate's corners reach ~1.2 before the cap). `action_axis(p, axis)` returns a processed axis whatever the contexts say, for tools and custom schemes. Actions bound with `BIND_AXIS` are gated by the contexts like any other.

## UI input

Menus, text boxes and dialogs take a `UiInput` (`src/ui/ui_input.h`): `up/down/left/right` (on the press, then repeating), `confirm`, `cancel`, `prev_tab`, `next_tab`, `start`. `action_ui(p)` fills it from the engine's UI actions. Scripted input, the benchmark and the tests fill it by hand. `menu_update(menu, &ui)` moves the cursor, changes values, switches tabs, applies (confirm) or reverts (cancel). `textbox_update(tb, &ui, dt)` advances, shows the whole page, or picks a choice.

## Rumble

| Function | |
|---|---|
| `input_rumble(port, on)` | on until turned off |
| `input_rumble_pulse(port, seconds)` | on for a while; a longer pulse wins |
| `input_rumble_player(p, seconds)` | a pulse on the player's port |
| `input_rumble_stop_all()`, `input_rumble_enable(on)` | stop everything; master switch (the demo's Settings → Rumble) |

Changes reach the pads at the end of the frame (`input_end_frame`), while the SI is idle, and only when the motor state changes: a Rumble Pak write is a joybus message, and libdragon's queue holds 8. A Rumble Pak motor is on or off (no strength). A new controller starts with its motor off.

## Latency

| Setting (demo: Settings → Latency) | Engine calls | Lag, light scene |
|---|---|---|
| Classic: the engine before S9 | `input_set_sync(INPUT_SYNC_LATEST)`, `engine_set_pacing(ENGINE_PACING_THROUGHPUT)` | 3 vblanks |
| Low (default) | `INPUT_SYNC_FRESH`, `ENGINE_PACING_THROUGHPUT` | 2 vblanks |
| Lowest | `INPUT_SYNC_FRESH`, `ENGINE_PACING_LOW_LATENCY` | 1 vblank, while frames fit the budget |

Lag is measured from the controller read a frame used to the vblank it reaches the screen. [ENGINE.md, "Input and latency"](ENGINE.md#input-and-latency-s9) explains the mechanism, and BENCHMARKS.md has the A3D figures (`Bench = Latency`). Low costs nothing when the CPU is late: the read is already in and the frame does not wait. Lowest trades frame rate for lag: a frame that misses the vblank waits for the next one.

## The demo's controls

`src/scenes/demo_controls.c` defines the demo's actions and default bindings; every player gets a context built from them.

| Action | Default | Notes |
|---|---|---|
| Confirm / Cancel / Select | A / B / Z | object selection and transform modes; B launches the ball (any player) |
| Cam Next / Cam Prev | R / L | camera mode |
| Cycle Next / Cycle Prev | D-Right / D-Left | selected object |
| Zoom In / Zoom Out / Shift Up / Shift Down | C-Up / C-Down / C-Right / C-Left | held |
| Menu | Start | any player opens the Start menu and drives it (not remappable) |
| Look X / Look Y | main stick (axis) | camera orbit, object moves |

The Controls tab remaps player 1's first eleven (the choices are the 13 N64 buttons besides Start). Players 2–4 can launch the ball and open the menu. The player who launched the ball feels its bounces on a Rumble Pak.

## Debugging

- **Input overlay page** (Debug → Overlay → Input, or D-Up): the sync and pacing in use, `Lag` (average, min–max, ms), `SI` read arrival and the wait (average / worst). Per port: type, accessory, raw stick, a square per button lit while held (redrawn every frame) and the stick's position. Per player: the port and the context stack. Last, player 1's held actions by name.
- **Profiler slots**: `input` (input_poll's work), `wait_input` (the fresh-read wait, idle), `pace` (low-latency pacing, idle).
- **CSV**: `FTP` rows with the dump; per benchmark step `BENCH_PRESENT` (lag columns), `BENCH_PROF` (`input_us`, `wait_input_us`), `BENCH_INPUT`.

## Tests

`tests/host/test_action.c` feeds `PadState` snapshots to the action layer and checks edges, taps, priorities, consume and modal contexts, held-when-blocked, revealed holds, the debug context below the game, chords, stick directions with hysteresis, axes and deadzones, repeat timing, players and ports, the UI input and context editing. `test_menu.c` drives the menu with `UiInput`; `test_frametime.c` checks the lag statistics.

## Source files

| File | Purpose |
|---|---|
| [src/input/pad.h](../src/input/pad.h), [pad.c](../src/input/pad.c) | `PadState`, buttons, names (pure) |
| [src/input/action.h](../src/input/action.h), [action.c](../src/input/action.c) | players, contexts, bindings, action state, analog processing, the engine's contexts (pure, host-tested) |
| [src/input/input.h](../src/input/input.h), [input.c](../src/input/input.c) | the input core: joypad, vblank sampling, fresh-read wait, pads, rumble, timing |
| [src/ui/ui_input.h](../src/ui/ui_input.h) | `UiInput` |
| [src/scenes/demo_controls.h](../src/scenes/demo_controls.h), [.c](../src/scenes/demo_controls.c) | the demo's actions, bindings and names |
