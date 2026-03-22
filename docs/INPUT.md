# Input & Action Mapping System

Remappable input abstraction for the N64 joypad. Game logic queries named actions (Confirm, Cancel, Zoom In, etc.) instead of raw buttons. Controls are rebindable at runtime via the in-game menu and switchable per-scene via action contexts.

## Controller Layout

```
          L                R
    ┌─────────────────────────┐
    │     [C-Up]              │
    │ [C-L] [C-R]    [Start]  │
    │     [C-Dn]              │
    │                         │
    │  [D-pad]    [A]  [B]    │
    │             ↑           │
    │         [Analog]        │
    └─────────────────────────┘
```

## Architecture

```
joypad_poll()  →  action_update()  →  action_pressed/held/released()
                      ↓                         ↑
              PhysicalButton → GameAction    scene logic queries
              (via ActionContext bindings)    actions, not buttons

action_analog_x/y()  →  input_update()  →  InputState (camera adapter)
```

### Fixed Inputs (Not Remappable)

| Input | Purpose | Why Fixed |
|-------|---------|-----------|
| Start | Toggle menu | System-level, always available |
| D-pad (in menu) | Navigate menu items | Standard UI convention |
| A/B (in menu) | Confirm/Cancel menu | Standard UI convention |
| L/R (in menu) | Switch menu tabs | Standard UI convention |

### Remappable Game Actions

| GameAction | Default Button | Description |
|------------|---------------|-------------|
| `ACTION_CONFIRM` | A | Enter, interact, confirm |
| `ACTION_CANCEL` | B | Back, exit, secondary action |
| `ACTION_SELECT_MODE` | Z | Toggle object selection |
| `ACTION_CAM_MODE_NEXT` | R | Cycle camera mode forward |
| `ACTION_CAM_MODE_PREV` | L | Cycle camera mode backward |
| `ACTION_CYCLE_NEXT` | D-Right | Cycle objects/items forward |
| `ACTION_CYCLE_PREV` | D-Left | Cycle objects/items backward |
| `ACTION_ZOOM_IN` | C-Up | Camera zoom in (held) |
| `ACTION_ZOOM_OUT` | C-Down | Camera zoom out (held) |
| `ACTION_SHIFT_UP` | C-Right | Shift camera target up (held) |
| `ACTION_SHIFT_DOWN` | C-Left | Shift camera target down (held) |

## Action Contexts

An `ActionContext` is a named set of bindings that defines which physical button triggers each game action. Contexts are defined as compile-time data:

```c
static const ActionContext combat_controls = {
    .name = "Combat",
    .bindings = {
        [ACTION_CONFIRM]       = BTN_A,      // Attack
        [ACTION_CANCEL]        = BTN_B,      // Dodge
        [ACTION_SELECT_MODE]   = BTN_Z,      // Lock-on
        [ACTION_CAM_MODE_NEXT] = BTN_R,      // Cycle target
        [ACTION_CAM_MODE_PREV] = BTN_L,      // Block
        [ACTION_CYCLE_NEXT]    = BTN_D_RIGHT,
        [ACTION_CYCLE_PREV]    = BTN_D_LEFT,
        [ACTION_ZOOM_IN]       = BTN_C_UP,
        [ACTION_ZOOM_OUT]      = BTN_C_DOWN,
        [ACTION_SHIFT_UP]      = BTN_C_RIGHT,
        [ACTION_SHIFT_DOWN]    = BTN_C_LEFT,
    },
    .analog_deadzone = 10.0f,    // Tighter for combat movement
    .analog_sensitivity = 0.003f, // Faster response
};

// Switch on scene init
action_set_context(&combat_controls);
```

### Built-in Context

`ACTION_CTX_EXPLORATION` — the default context loaded by `action_init()`. Standard N64 control layout with deadzone=8 and sensitivity=0.002.

### Context Switching

`action_set_context()` copies the context's bindings into the active binding array. Individual bindings can then be overridden via `action_set_binding()` without mutating the source context. This allows runtime remapping (Controls menu) on top of a base context.

## Runtime Remapping (Controls Menu)

The Controls tab (tab 4) in the start menu lists all 11 game actions. Each action shows its currently assigned button and can be cycled through all 13 physical buttons.

Menu option indices match the `PhysicalButton` enum order (A=0, B=1, Z=2, ..., C-Right=12), so remapping is a direct cast:

```c
for (int i = 0; i < ACTION_COUNT; i++) {
    int btn_idx = menu_get_value(&start_menu, TAB_CONTROLS, i);
    action_set_binding((GameAction)i, (PhysicalButton)btn_idx);
}
```

Pressing B to cancel the menu reverts all bindings to their pre-menu-open values via the menu's snapshot system.

## Analog Stick

- Raw range: -128 to 127 (8-bit signed)
- Deadzone: configurable per context (default 8)
- Sensitivity: configurable per context (default 0.002, maps full deflection to ~0.24 radians/frame)
- Processed by `action_analog_x/y()` which apply the active context's deadzone and sensitivity

## InputState (Camera Adapter)

`InputState` is a thin adapter that translates action API calls into camera-specific values. Camera code reads `InputState` fields without knowing about the action system:

```c
typedef struct {
    float orbit_azimuth;     // from action_analog_x()
    float orbit_elevation;   // from action_analog_y()
    float zoom_delta;        // from action_held(ACTION_ZOOM_IN/OUT)
    float target_y_delta;    // from action_held(ACTION_SHIFT_UP/DOWN)
    bool has_input;          // from action_has_analog() or any held action
} InputState;
```

## Input Flow

```
action_init()                        [once at startup — calls joypad_init()]
    ↓
action_update()                      [once per frame — calls joypad_poll()]
    ↓
action_pressed/held/released()       [queried by game logic]
action_analog_x/y()                  [queried by input_update for camera]
    ↓
input_update(&state)                 [fills InputState for camera]
    ↓
joypad_get_buttons_pressed().start   [checked directly for menu toggle]
    ↓
menu_update() OR game controls       [depending on menu state]
```

## API Reference

### Lifecycle

| Function | Description |
|----------|-------------|
| `action_init()` | Initialize joypad, set default exploration context |
| `action_update()` | Poll joypad, map physical buttons to action states |

### Query Actions

| Function | Description |
|----------|-------------|
| `action_pressed(action)` | True on the frame the action's button is first pressed |
| `action_held(action)` | True every frame the action's button is held down |
| `action_released(action)` | True on the frame the action's button is released |

### Analog Stick

| Function | Description |
|----------|-------------|
| `action_analog_x()` | Horizontal stick value, filtered by deadzone and sensitivity |
| `action_analog_y()` | Vertical stick value, filtered by deadzone and sensitivity |
| `action_has_analog()` | True if stick is past the deadzone threshold |

### Context & Remapping

| Function | Description |
|----------|-------------|
| `action_set_context(ctx)` | Switch to a new context (copies bindings) |
| `action_context_name()` | Name of the active context |
| `action_set_binding(action, btn)` | Override a single binding in the active context |
| `action_get_binding(action)` | Get the current button for an action |

### Utility

| Function | Description |
|----------|-------------|
| `action_button_name(btn)` | Human-readable button name ("A", "C-Up", etc.) |
| `action_name(action)` | Human-readable action name ("Confirm", "Cancel", etc.) |

### Camera Adapter

| Function | Description |
|----------|-------------|
| `input_update(state)` | Fill InputState from action API (camera-specific) |

## Migrating Game Code

Replace raw button checks with action queries:

| Before | After |
|--------|-------|
| `pressed.a` | `action_pressed(ACTION_CONFIRM)` |
| `pressed.b` | `action_pressed(ACTION_CANCEL)` |
| `pressed.z` | `action_pressed(ACTION_SELECT_MODE)` |
| `pressed.l` | `action_pressed(ACTION_CAM_MODE_PREV)` |
| `pressed.r` | `action_pressed(ACTION_CAM_MODE_NEXT)` |
| `pressed.d_left` | `action_pressed(ACTION_CYCLE_PREV)` |
| `pressed.d_right` | `action_pressed(ACTION_CYCLE_NEXT)` |

For held actions (continuous input like zoom): use `action_held()` instead of `action_pressed()`.

## Future Work

- **File-based control schemes**: Load `ActionContext` bindings from DFS text or binary files, enabling user-created control profiles without recompilation
- **Multiple simultaneous contexts**: Stack-based context system for nested game modes (e.g., exploration → combat → inventory)
- **Multiplayer support**: Per-player action contexts reading from joypad ports 1-4, enabling split-screen or hot-seat multiplayer with independent bindings per player

## Source Files

| File | Purpose |
|------|---------|
| [src/input/action.h](../src/input/action.h) | Enums, ActionContext struct, full API declarations |
| [src/input/action.c](../src/input/action.c) | Action system implementation, default exploration context |
| [src/input/input.h](../src/input/input.h) | InputState struct (camera adapter) |
| [src/input/input.c](../src/input/input.c) | Camera input adapter using action API |
