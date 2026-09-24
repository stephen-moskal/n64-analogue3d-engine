# Menu System

A lightweight, reusable tabbed menu system for in-game settings, pause menus, and option screens. Designed to be data-driven: define tabs, items, and options. The system handles navigation, scrolling, disabled items, and cancel/revert.

The menu is split into a **model** (`menu.c`: tabs, items, values, cursor, input; host-tested in `tests/host/test_menu.c`) and a **view** (`menu_view.c`: drawing in a `UiStyle`, with cached text). This document covers the model and how to use both; the drawing side is in [UI.md](UI.md).

## Quick Start

```c
#include "ui/menu.h"
#include "ui/menu_view.h"

// Define options (the menu keeps the pointers: use static storage)
static const char *difficulty[] = {"Easy", "Normal", "Hard"};
static const char *sound[] = {"On", "Off"};

// Create menu with tabs
static Menu my_menu;
menu_init(&my_menu, "Options");

int tab_game = menu_add_tab(&my_menu, "Game");
menu_add_item(&my_menu, tab_game, "Difficulty", difficulty, 3, 1);  // default: Normal
menu_add_item(&my_menu, tab_game, "Sound", sound, 2, 0);            // default: On

// In the update (after the joypad was polled this frame):
if (start_pressed) menu_open(&my_menu);
if (my_menu.is_open) menu_update(&my_menu);   // D-pad, stick, L/R, A/B

// Once: a view (style + cached text; ~98 KB surface, allocated on first draw)
static MenuView my_view;
menu_view_init(&my_view, &ui_style_debug, true);

// In the draw, after the 3D scene (draws nothing while the menu is closed):
menu_draw(&my_menu, &my_view);

// Read current values (tab index, item index):
int diff = menu_get_value(&my_menu, tab_game, 0);
int snd  = menu_get_value(&my_menu, tab_game, 1);
```

## API Reference

### `menu_init(Menu *menu, const char *title)`

Initialize a menu with a title. Zeroes all state.

### `menu_add_tab(Menu *menu, const char *label)`

Add a tab. Returns the tab index (0-based), or -1 when `MENU_MAX_TABS` (6) tabs exist.

### `menu_add_item(Menu *menu, int tab, const char *label, const char **options, int count, int default_idx)`

Add an item to a tab. Returns the item index (0-based within the tab), or -1 if the tab is invalid or already holds `MENU_MAX_ITEMS` (12) items. `count` is clamped to `MENU_MAX_OPTIONS` (16); the option string pointers are copied, not the strings. Tabs with more than `MENU_VISIBLE_ITEMS` (7) items scroll.

The -1 returns are silent: check them when adding to a nearly full menu.

### `menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled)`

Greys an item out. Its value cannot be changed, but the cursor can move onto it (the label lights up, the value stays grey without arrows), so a long list scrolls to disabled items and shows them. `menu_open()` starts each tab's cursor on its first enabled item.

### `menu_open(Menu *menu)`

Opens the menu. Takes a snapshot of every item's value (all tabs) for cancel/revert, resets each tab's cursor and scroll, and shows the first tab.

### `menu_close(Menu *menu, bool apply)`

Closes the menu. If `apply` is `false`, all item selections revert to the snapshot taken when the menu was opened.

### `menu_update(Menu *menu)`

Handles navigation; call once per frame while the menu is open. It maps the joypad to the model operations below. It reads the joypad state directly (`joypad_get_buttons_pressed()` / `joypad_get_inputs()`, port 1), so the joypad must already have been polled this frame — `action_update()` does that ([INPUT.md](INPUT.md)). Start is not handled here: the caller opens and closes the menu.

### Model operations

`menu_move_cursor(menu, dir)` (wraps, keeps the cursor in the visible window), `menu_change_value(menu, dir)` (the cursor item; wraps; ignored on disabled items), `menu_switch_tab(menu, dir)` (wraps). `menu_update()` uses them; scripted input (the UI benchmark) and tests call them directly.

### `menu_set_value(Menu *menu, int tab, int item, int value)`

Sets an item's option index (out-of-range values are ignored). Use it instead of writing `items[i].selected` so a later change (S8) can hook it; the view notices direct writes too.

### `menu_draw(const Menu *menu, MenuView *view)`

Renders the menu when it is open, in the view's style, re-rendering only the text that changed ([UI.md](UI.md)). Call after all 3D geometry and other UI, while the framebuffer is attached. `menu_view_init(view, style, cached)` once; `menu_view_set_style()` restyles; `menu_view_free()` releases the surface.

### `menu_get_value(const Menu *menu, int tab, int item_index)`

Returns the selected option index of an item (0 for an invalid tab or item). Safe to call whether the menu is open or closed.

## Data Structures

```c
#define MENU_MAX_TABS        6
#define MENU_MAX_ITEMS      12
#define MENU_MAX_OPTIONS    16
#define MENU_VISIBLE_ITEMS   7

typedef struct {
    const char *label;                       // Left column text
    const char *options[MENU_MAX_OPTIONS];   // Cycle-able choices
    int option_count;
    int selected;                            // Current option index
    bool disabled;                           // Greyed out, cursor skips over
} MenuItem;

typedef struct {
    const char *label;                       // Tab header text
    MenuItem items[MENU_MAX_ITEMS];
    int item_count;
    int cursor;                              // Highlighted row within tab
    int scroll_offset;                       // First visible item index
} MenuTab;

typedef struct {
    const char *title;
    MenuTab tabs[MENU_MAX_TABS];
    int tab_count;
    int active_tab;                          // Currently visible tab
    bool is_open;
    int snapshot[MENU_MAX_TABS][MENU_MAX_ITEMS]; // For cancel/revert (all tabs)
    int analog_cooldown;                     // Analog stick repeat timer
} Menu;
```

## Controls

| Input | Action |
|-------|--------|
| Start | Toggle menu open/close (handled by the caller, not `menu_update`) |
| D-pad Up/Down | Move the cursor (wraps; disabled items can be visited but not changed), auto-scrolls when >7 items |
| D-pad Left/Right | Cycle selected option for current item (wraps) |
| Analog Stick Left/Right | Same as D-pad left/right (threshold 40, repeats every 10 frames) |
| L/R Shoulder | Switch between tabs (wraps) |
| A Button | Confirm — close menu, keep all changes (all tabs) |
| B Button | Cancel — close menu, revert to values from when menu was opened (all tabs) |

## Visual Layout

```
┌──────────────────────────────┐
│          Start Menu          │  Title (white, variable-width font)
│      < [Settings] 1/6 >      │  Active tab and position (yellow)
│──────────────────────────────│  Separator line
│  BG Color    < Light Blue > ▐│  Cursor row (yellow); ▐ scroll bar on tabs with >7 items
│  Debug Text     <  On  >    ▐│  Other rows (grey), disabled rows dark grey without arrows
│  Camera       < Orbital >   ││
│   L/R:Tab  A:OK  B:Cancel    │  Footer hint
└──────────────────────────────┘
```

- Background: semi-transparent black (alpha-blended triangles), x 30–290 (260 px wide, centred on 320)
- Title at y 50, tab header at y 64, items from y 86
- Row height: 18px, up to 7 rows visible
- Left column (label): x=44
- Right column (value with arrows): starts at x=170, centred in the remaining width
- With a single tab the header is `[Label]` and the footer `A:OK  B:Cancel`
- Scroll bar: a 3 px track beside the rows at the right edge, with a thumb whose height is the visible share (7 / item count) and whose position follows the scroll offset
- All of these values come from the style (`ui_style_debug`); another style can move, recolour or restyle every element ([UI.md](UI.md))

## Rendering Details

Every frame the view draws the panel as a translucent rectangle (standard mode, flat combiner, blender; `rdpq_fill_rectangle` is hardware-safe in any mode), the separator and the scroll bar as rectangles, and the text as one copy-mode blit of its cached layer. Text is re-rendered into the layer only for the slots that changed: a cursor move re-renders two rows, a tab switch the whole panel. The demo times `menu_draw()` in the `menu` profiler slot; Bench = UI measures it ([BENCHMARKS.md](BENCHMARKS.md)).

## Integration in This Engine

The engine has one global menu, `Menu start_menu`, defined and built in `src/main.c` with six tabs: Settings, Sound, Lighting, Environ, Controls and Debug. The Debug tab's items are added by `debug_menu_init()` ([DEBUGGING.md](DEBUGGING.md)).

Driving it is the scene's job, and only the demo scene does it (`demo_scene.c`):

- `demo_update()` polls input with `action_update()`, toggles the menu on Start (raw joypad), calls `menu_update()` while it is open, and after it closes reads values with `menu_get_value()` and applies the ones that changed (it caches the last applied value of each item).
- `demo_post_draw()` calls `menu_draw(&start_menu, &start_menu_view)` last, after the HUD. The view is created on first use and kept across scene resets.
- `debug_menu_update()`, called from the main loop in `main.c`, applies the Debug tab while the menu is closed. The debug overlay page is hidden while the menu is open.

The benchmark scene has no menu (Start aborts the run). A new scene that wants the menu must poll input, toggle, update and draw it the same way.

Items are addressed by position: the `TAB_*` / `ITEM_*` defines in `demo_scene.c` and the `DebugMenuItem` enum in `debug_menu.h` must match the order in which `main.c` and `debug_menu_init()` add the items. Values live in the global menu, so they survive scene switches and Reset Scene.

**Capacity:** all 6 tabs are used, and the Controls and Debug tabs hold 11 of their 12 items (Settings 6, Sound 3, Lighting 10, Environ 6). A seventh tab needs `MENU_MAX_TABS` raised.

## Extending the Menu

Adding an item means an option array, a `menu_add_item()` call in the right position, a matching index define, and code that applies the value when it changes. The recipes for a menu item and a Debug tab item are in [EXTENDING.md](EXTENDING.md).

```c
static const char *speed_options[] = {"Slow", "Normal", "Fast"};
menu_add_item(&start_menu, tab_s, "Game Speed", speed_options, 3, 1);
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
float game_speed = speeds[menu_get_value(&start_menu, TAB_SETTINGS, ITEM_GAME_SPEED)];
```

## Source Files

| File | Purpose |
|------|---------|
| [src/ui/menu.h](../src/ui/menu.h) | Data structures and API declarations |
| [src/ui/menu.c](../src/ui/menu.c) | Model: building, open/close, cursor, values, joypad input |
| [src/ui/menu_view.h](../src/ui/menu_view.h), [src/ui/menu_view.c](../src/ui/menu_view.c) | View: drawing in a style with cached text ([UI.md](UI.md)) |
| [tests/host/test_menu.c](../tests/host/test_menu.c) | Host tests of the model |
| [src/main.c](../src/main.c) | Builds the global start menu (option arrays, tab and item order) |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | Opens, updates, draws the menu and applies its values |
| [src/debug/debug_menu.c](../src/debug/debug_menu.c) | Debug tab items and how they apply |
