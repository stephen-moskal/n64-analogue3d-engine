# Menu System

A lightweight, reusable menu system for in-game settings, pause menus, and option screens. Designed to be data-driven: define items and options, the system handles navigation, rendering, and cancel/revert.

## Quick Start

```c
#include "ui/menu.h"

// Define options
static const char *difficulty[] = {"Easy", "Normal", "Hard"};
static const char *sound[] = {"On", "Off"};

// Create menu
static Menu my_menu;
menu_init(&my_menu, "Options");
menu_add_item(&my_menu, "Difficulty", difficulty, 3, 1);  // default: Normal
menu_add_item(&my_menu, "Sound", sound, 2, 0);            // default: On

// In game loop:
if (start_pressed) menu_open(&my_menu);

if (my_menu.is_open) {
    menu_update(&my_menu);    // Handles all input
    menu_draw(&my_menu);      // Renders overlay
}

// Read current values:
int diff = menu_get_value(&my_menu, 0);   // 0=Easy, 1=Normal, 2=Hard
int snd  = menu_get_value(&my_menu, 1);   // 0=On, 1=Off
```

## API Reference

### `menu_init(Menu *menu, const char *title)`

Initialize a menu with a title. Zeroes all state.

### `menu_add_item(Menu *menu, const char *label, const char **options, int count, int default_idx)`

Add an item with a label and an array of option strings. Returns the item index (0-based) or -1 if full. The `default_idx` sets the initial selected option.

**Limits:** `MENU_MAX_ITEMS` (8) items, `MENU_MAX_OPTIONS` (8) options per item.

### `menu_open(Menu *menu)`

Opens the menu. Takes a snapshot of all current values for cancel/revert. Resets cursor to first item.

### `menu_close(Menu *menu, bool apply)`

Closes the menu. If `apply` is `false`, all item selections revert to the snapshot taken when the menu was opened.

### `menu_update(Menu *menu)`

Reads controller input and handles navigation. Call once per frame when menu is open. **Must be called after `joypad_poll()`** (which `input_update()` calls).

### `menu_draw(const Menu *menu)`

Renders the menu overlay. Call after all 3D geometry and other UI elements.

### `menu_get_value(const Menu *menu, int item_index)`

Returns the currently selected option index for the given item. Safe to call whether the menu is open or closed.

## Data Structures

```c
typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];    // Cycle-able choices
    int option_count;
    int selected;                            // Current option index
} MenuItem;

typedef struct {
    const char *title;
    MenuItem items[MENU_MAX_ITEMS];
    int item_count;
    int cursor;                              // Highlighted row
    bool is_open;
    int snapshot[MENU_MAX_ITEMS];            // For cancel/revert
    int analog_cooldown;                     // Analog stick repeat timer
} Menu;
```

## Controls

| Input | Action |
|-------|--------|
| Start | Toggle menu open/close (handled in game code, not menu_update) |
| D-pad Up/Down | Move cursor between items (wraps) |
| D-pad Left/Right | Cycle selected option for current item (wraps) |
| Analog Stick Left/Right | Same as D-pad left/right (with repeat cooldown) |
| A Button | Confirm — close menu, keep all changes |
| B Button | Cancel — close menu, revert to values from when menu was opened |

## Visual Layout

```
┌──────────────────────────────┐
│          Start Menu          │  Title (white, centered)
│──────────────────────────────│  Separator line
│                              │
│  BG Color      < Dark Blue > │  Cursor row (yellow)
│  Debug Text    <     On    > │  Other rows (gray)
│                              │
│       A:OK     B:Cancel      │  Footer hint (dim gray)
└──────────────────────────────┘
```

- Background: Semi-transparent black overlay (alpha-blended triangles)
- Menu width: 260px centered on 320px screen
- Row height: 20px
- Left column (label): x=44
- Right column (value with arrows): x=170, centered in remaining width

## Rendering Details

The menu background uses alpha blending to create a semi-transparent overlay:

```c
rdpq_set_mode_standard();
rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
rdpq_set_prim_color(RGBA32(0, 0, 0, 160));
// Two TRIFMT_FILL triangles form the background rectangle
```

This uses 1-cycle mode (not fill mode), which is safe on real hardware.

Text is rendered using the existing text system (`text_draw` / `text_draw_fmt`) with `TextBoxConfig` structs for each element.

## Integration Pattern

```c
// In main.c game loop:

input_update(&input_state);  // Polls joypad

// Check Start button (edge-triggered)
joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
if (pressed.start) {
    if (menu.is_open) menu_close(&menu, true);
    else menu_open(&menu);
}

if (menu.is_open) {
    menu_update(&menu);          // Menu consumes D-pad/A/B input
} else {
    // Normal game input (camera, movement, etc.)
}

// Apply settings from menu values regardless of open state:
color_t bg = colors[menu_get_value(&menu, 0)];
bool show_hud = (menu_get_value(&menu, 1) == 0);

// Render:
cube_draw(...);
if (show_hud) text_draw(...);
if (menu.is_open) menu_draw(&menu);  // Always last before detach
rdpq_detach_show();
```

## Extending the Menu

### Adding a new item

```c
static const char *options[] = {"Option A", "Option B", "Option C"};
#define MENU_ITEM_MY_SETTING 2  // Next available index
menu_add_item(&menu, "My Setting", options, 3, 0);
```

### Multiple menus

Create separate `Menu` structs for different screens:

```c
static Menu pause_menu;
static Menu options_menu;
static Menu inventory_menu;
```

Each menu is independent — open/close/update/draw one at a time.

### Mapping values to game state

The menu stores option indices. Your game code maps indices to actual values:

```c
// Example: map menu index to game speed
static const float speeds[] = {0.5f, 1.0f, 2.0f};
float game_speed = speeds[menu_get_value(&menu, MENU_ITEM_SPEED)];
```

## Source Files

| File | Purpose |
|------|---------|
| [src/ui/menu.h](../src/ui/menu.h) | Data structures and API declarations |
| [src/ui/menu.c](../src/ui/menu.c) | Input handling, rendering, state management |
| [src/ui/text.h](../src/ui/text.h) | Text rendering (used by menu) |
