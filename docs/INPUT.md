# Input System

Controller input handling for the N64 joypad. Provides a clean abstraction over libdragon's joypad API, mapping physical inputs to game actions.

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

## Current Mappings

### Scene Mode (menu closed)

| Input | Action | Details |
|-------|--------|---------|
| Analog Stick X | Camera orbit horizontal | Scale: -stick_x * 0.002, deadzone: 8 |
| Analog Stick Y | Camera orbit vertical | Scale: stick_y * 0.002, deadzone: 8 |
| C-Up | Zoom in | -5.0 units/frame (held) |
| C-Down | Zoom out | +5.0 units/frame (held) |
| C-Left | Shift target down | -2.0 units/frame (held) |
| C-Right | Shift target up | +2.0 units/frame (held) |
| Start | Open menu | Edge-triggered (press, not hold) |

### Menu Mode (menu open)

| Input | Action | Details |
|-------|--------|---------|
| D-pad Up/Down | Navigate items | Edge-triggered, wraps around |
| D-pad Left/Right | Cycle option value | Edge-triggered, wraps around |
| Analog Stick L/R | Cycle option value | Threshold > 40, 10-frame cooldown |
| A Button | Confirm & close | Keeps current selections |
| B Button | Cancel & close | Reverts to values when menu opened |
| Start | Close menu | Edge-triggered |

## InputState Structure

```c
typedef struct {
    float orbit_azimuth;     // Horizontal camera orbit delta
    float orbit_elevation;   // Vertical camera orbit delta
    float zoom_delta;        // Camera zoom delta
    float target_y_delta;    // Camera target Y shift
    bool has_input;          // True if any input active this frame
} InputState;
```

## API

### `input_init(void)`

Initializes the joypad subsystem. Must be called once at startup.

### `input_update(InputState *state)`

Polls the controller and fills the `InputState` struct. Call once per frame at the start of the update phase.

Internally calls `joypad_poll()`, which latches the current button states. After this call, `joypad_get_buttons_pressed()` and other joypad functions return data from this frame's poll.

## libdragon Joypad API

The engine uses these libdragon functions (called after `joypad_poll()`):

| Function | Returns | Use |
|----------|---------|-----|
| `joypad_get_inputs(port)` | `joypad_inputs_t` | Analog stick values (stick_x, stick_y: -128 to 127) |
| `joypad_get_buttons_held(port)` | `joypad_buttons_t` | Buttons currently held down |
| `joypad_get_buttons_pressed(port)` | `joypad_buttons_t` | Buttons pressed this frame (edge-triggered) |

**Held vs Pressed:** `held` is true every frame the button is down (good for continuous actions like zoom). `pressed` is true only on the frame the button transitions from up to down (good for discrete actions like menu navigation).

## Input Flow

```
joypad_poll()                    [called by input_update]
    ↓
input_update(&state)             [fills InputState for camera]
    ↓
joypad_get_buttons_pressed()     [checked in main.c for Start]
    ↓
menu_update() OR camera controls [depending on menu state]
```

When the menu is open, camera controls from `InputState` are ignored. The menu reads the joypad directly via `joypad_get_buttons_pressed()`.

## Analog Stick

- Raw range: -128 to 127 (8-bit signed)
- Deadzone: 8 (values between -8 and 8 are treated as zero)
- Scale factor: 0.002 (maps full deflection to ~0.24 radians/frame orbit speed)

## Adding New Input Mappings

To add a new action:

1. Add a field to `InputState` in `input.h`
2. Map the desired button/stick to that field in `input_update()` in `input.c`
3. Read the field in your game code

For buttons used in UI (edge-triggered), use `joypad_get_buttons_pressed()` directly rather than adding to `InputState`.

## Source Files

| File | Purpose |
|------|---------|
| [src/input/input.h](../src/input/input.h) | InputState struct, API declarations |
| [src/input/input.c](../src/input/input.c) | Joypad polling, analog/button mapping |
