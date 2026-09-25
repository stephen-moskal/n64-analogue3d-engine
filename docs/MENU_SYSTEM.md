# Menu System

A lightweight, reusable tabbed menu system for in-game settings, pause menus, and option screens. Designed to be data-driven: define tabs, items, and options. The system handles navigation, scrolling, disabled items, and cancel/revert.

The menu is split into a **model** (`menu.c`: tabs, items, values, cursor, and what a frame of `UiInput` does to them; host-tested in `tests/host/test_menu.c`) and a **view** (`menu_view.c`: drawing in a `UiStyle`, with cached text). This document covers the model and how to use both; the drawing side is in [UI.md](UI.md).

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

// In the update (the engine polled the controllers before it):
if (!my_menu.is_open && action_pressed(0, ACT_PAUSE)) {
    menu_open(&my_menu);
    action_push_context(0, &action_ctx_ui);     // the menu takes the pad (modal)
}
if (my_menu.is_open) {
    UiInput ui = action_ui(0);                  // D-pad / stick (repeating), L/R, A/B, Start
    menu_update(&my_menu, &ui);
    if (!my_menu.is_open) action_pop_context(0, &action_ctx_ui);
}

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

### `menu_add_item(Menu *menu, int tab, const char *label, const char *const *options, int count, int default_idx)`

Add an item to a tab. Returns the item index (0-based within the tab), or -1 if the tab is invalid or already holds `MENU_MAX_ITEMS` (12) items. `count` is clamped to `MENU_MAX_OPTIONS` (16); the option string pointers are copied, not the strings. Tabs with more than `MENU_VISIBLE_ITEMS` (7) items scroll.

The -1 returns are silent: check them when adding to a nearly full menu.

### `menu_item_set_disabled(Menu *menu, int tab, int item, bool disabled)`

Greys an item out. Its value cannot be changed, but the cursor can move onto it (the label lights up, the value stays grey without arrows), so a long list scrolls to disabled items and shows them. `menu_open()` starts each tab's cursor on its first enabled item.

### `menu_open(Menu *menu)`

Opens the menu. Takes a snapshot of every item's value (all tabs) for cancel/revert, resets each tab's cursor and scroll, and shows the first tab.

### `menu_close(Menu *menu, bool apply)`

Closes the menu. If `apply` is `false`, all item selections revert to the snapshot taken when the menu was opened.

### `menu_update(Menu *menu, const UiInput *in)`

Applies one frame of UI input to the model; call once per frame while the menu is open. `up`/`down` move the cursor, `left`/`right` change the value, `prev_tab`/`next_tab` switch tabs, `confirm` closes and applies, `cancel` closes and reverts. `start` is left to the caller (the demo closes and applies). The menu knows no buttons: `action_ui(player)` fills the `UiInput` from the engine's UI context, whose directions repeat while held ([INPUT.md](INPUT.md)), and scripted input or tests fill it by hand. Since S9 (D12) this replaces reading the joypad directly.

### Model operations

`menu_move_cursor(menu, dir)` (wraps, keeps the cursor in the visible window), `menu_change_value(menu, dir)` (the cursor item; wraps; ignored on disabled items), `menu_switch_tab(menu, dir)` (wraps). `menu_update()` uses them; scripted input (the UI benchmark), the Menu Sweep and tests can call them directly.

### `menu_set_value(Menu *menu, int tab, int item, int value)`

Sets an item's option index (out-of-range values are ignored); it only checks the range and writes. The game's options go through the settings module instead (`settings_set_choice()`, [Settings](#settings)), which tracks changes. The view notices direct writes too.

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
    bool disabled;                           // Greyed out: the cursor stops on it, the value can't change
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
} Menu;
```

## Controls

The engine's UI context (`action_ctx_ui`, [INPUT.md](INPUT.md)) maps them; a game can rebind it.

| Input | Action |
|-------|--------|
| Start | Toggle menu open/close (handled by the caller, not `menu_update`) |
| D-pad or stick Up/Down | Move the cursor (wraps; disabled items can be visited but not changed), auto-scrolls when >7 items |
| D-pad or stick Left/Right | Cycle selected option for current item (wraps) |
| (held) | Repeats after 0.3 s, then every 0.1 s |
| L/R Shoulder | Switch between tabs (wraps) |
| A Button | Confirm — close menu, keep all changes (all tabs) |
| B Button | Cancel — close menu, revert to values from when menu was opened (all tabs) |

The stick counts as a direction past half deflection, one direction at a time (4-way). Before S9 the D-pad did not repeat and the stick changed values only.

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

- Background: semi-transparent black (translucent rectangles: standard mode with the blender, `ui_rect()`), x 30–290 (260 px wide, centred on 320)
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

The engine has one global menu, `Menu start_menu` in `src/main.c`, with six tabs. `settings_init()` adds the first five, the game's options (Settings, Sound, Lighting, Environ, Controls; `src/ui/settings.c`, see [Settings](#settings) below), and `main.c` adds the Debug tab, whose items `debug_menu_init()` fills ([DEBUGGING.md](DEBUGGING.md)).

Driving it is the scene's job, and only the demo scene does it (`demo_scene.c`):

- `demo_update()` opens the menu when any player presses Start (the demo's `ACT_MENU` action) and pushes the engine's UI context on that player's stack, so only that player drives it. While it is open it calls `menu_update(&start_menu, &ui)` with that player's `action_ui()`, closes it on Start, and pops the context when it closes. It applies each group of options when one of them changes (`settings_take()`), live while the menu is open: Cancel reverts the menu and so the scene.
- `demo_post_draw()` calls `menu_draw(&start_menu, &start_menu_view)` last, after the HUD. The view is created on first use and kept across scene resets.
- `debug_menu_update()`, called from the engine loop (`engine.c`), applies the Debug tab while the menu is closed. The debug overlay page is hidden while the menu is open.

The benchmark scene has no menu (Start aborts the run). A new scene that wants the menu must open, update and draw it the same way.

Game code addresses options by `SettingId` (`settings.h`), never by tab and item number; the Debug tab by the `DebugMenuItem` enum (`debug_menu.h`), whose order `debug_menu_init()` follows. Values live in the global menu, so they survive scene switches and Reset Scene.

**Capacity:** all 6 tabs are used; Settings 9, Sound 3, Lighting 10, Environ 6, Controls 11 (one per remappable demo action) and Debug 12 of 12 items. The settings module asserts the limits at compile time. A seventh tab needs `MENU_MAX_TABS` raised.

## Settings

`src/ui/settings.c/h` holds the game's options (D10: the strings used to live in `main.c` and their meanings in `demo_scene.c`, matched by raw index). Each option is one row of a table: its tab, label, choices, default, and the value each choice stands for, declared side by side:

```c
static const char *const shadow_dark_names[] = {"Light", "Medium", "Dark"};
static const float shadow_dark_values[] = {0.3f, 0.6f, 0.9f};
CHOICE_TABLE(shadow_dark_names, shadow_dark_values);   // same length, <= MENU_MAX_OPTIONS

[SETTING_SHADOW_DARKNESS] = { L, "Shadow Dark", CHOICES(shadow_dark_names), 1,
                              VALUES(SETTING_TYPE_FLOAT, shadow_dark_values) },   // Medium
```

`SettingId` lists the options tab by tab in menu order, so the table, the menu and the code can't drift apart. Static asserts check every choice/value pair, each tab's length and the tab count; `tests/host/test_settings.c` checks the rest (defaults in range, the menu built from the table, the values, change tracking).

- **Values:** `settings_bool()`, `settings_int()`, `settings_float()`, `settings_vec3()`, `settings_color()` return what the selected choice stands for (each asserts the option's type); `settings_choice()` and `settings_choice_name()` give the choice itself. Toggles read the same whatever their choice order ("On/Off" or "Off/On").
- **Changes:** `settings_take(id)` answers whether the choice differs from the one last taken, and takes it; `settings_take_range(first, last)` does that for a group, so one apply covers several options. `settings_invalidate()` marks everything changed: the demo calls it in `demo_init()`, so after boot and after every Reset Scene each option is applied again and the scene matches the menu (D27: the scene used to keep `lighting_init()`'s ambient 0.15 under a menu showing 20 %).
- **From code:** `settings_set_choice()`, `settings_step_choice()` (wraps; the L/R camera shortcuts), `settings_set_bool()`, `settings_set_disabled()`; `settings_trigger()` fires an action option ("---" / "Reset!") once and puts it back.
- **Generated rows:** the Controls tab is built from the demo's actions (`src/scenes/demo_controls.c`): one option per remappable action (`DEMO_REMAP_FIRST` on), labelled with its name, its choices the 13 N64 buttons other than Start (`pad_button_name()`), its value the `PadButton`, its default the action's default button. The demo remaps player 1's context when an option changes:

```c
for (int a = DEMO_REMAP_FIRST; a < DEMO_REMAP_FIRST + DEMO_REMAP_COUNT; a++) {
    if (settings_take(SETTING_BINDING(a)))
        action_context_set_button(&demo_ctx[0], (ActionId)a, 0, (PadButton)settings_int(SETTING_BINDING(a)));
}
```

- **Settings tab, S9:** Latency (Classic / Low / Lowest: input sync and frame pacing, [ENGINE.md](ENGINE.md#input-and-latency-s9)) and Rumble (On / Off).

## Extending the Menu

A new game option is a new `SettingId`, its choices and values, and a table row in `settings.c`, then code that applies it when it changes. The Debug tab has its own recipe. Both are in [EXTENDING.md](EXTENDING.md).

### Multiple menus

Create separate `Menu` structs for different screens:

```c
static Menu pause_menu;
static Menu options_menu;
static Menu inventory_menu;
```

Each menu is independent: open, close, update and draw one at a time. `settings_init()` fills the first menu it is given; another menu is built with `menu_add_tab()` / `menu_add_item()` and read with `menu_get_value()`, which returns the selected choice's index.

## Source Files

| File | Purpose |
|------|---------|
| [src/ui/menu.h](../src/ui/menu.h) | Data structures and API declarations |
| [src/ui/menu.c](../src/ui/menu.c) | Model: building, open/close, cursor, values, UI input |
| [src/ui/ui_input.h](../src/ui/ui_input.h) | `UiInput`: one frame of UI input |
| [src/ui/settings.h](../src/ui/settings.h), [src/ui/settings.c](../src/ui/settings.c) | The game's options: the table, typed values, change tracking |
| [src/ui/menu_view.h](../src/ui/menu_view.h), [src/ui/menu_view.c](../src/ui/menu_view.c) | View: drawing in a style with cached text ([UI.md](UI.md)) |
| [tests/host/test_menu.c](../tests/host/test_menu.c) | Host tests of the model |
| [src/main.c](../src/main.c) | Creates the global start menu: `settings_init()`, then the Debug tab |
| [src/scenes/demo_scene.c](../src/scenes/demo_scene.c) | Opens, updates, draws the menu and applies its values |
| [src/debug/debug_menu.c](../src/debug/debug_menu.c) | Debug tab items and how they apply |
