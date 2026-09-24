# UI

The UI layer in `src/ui/` draws menus, HUDs and dialog text boxes, so that:
- text costs little per frame, because it is cached;
- the look is data, because it comes from a style;
- the model (what a menu holds) is separate from the view (how it is drawn).

The menu model and its API are in [MENU_SYSTEM.md](MENU_SYSTEM.md), the dialog format and runner in [DIALOG.md](DIALOG.md). This document covers what draws them.

| Module | Role |
|---|---|
| `text.c/h` | Font registry, `text_draw()` / `text_draw_fmt()` (immediate text, one print per call) and `text_render_paragraph()` |
| `ui_style.c/h` | `UiStyle`: fonts, colours, layout metrics and decorations. Built in: `ui_style_debug` |
| `ui_draw.c/h` | Immediate primitives: `ui_rect()`, `ui_vgradient()`, `ui_frame()`, `ui_gauge()` |
| `ui_layer.c/h` | `UiLayer`: retained text slots, cached in an offscreen surface |
| `menu_view.c/h` | `MenuView`: draws a `Menu` in a style through a `UiLayer` |
| `ui_hud.c/h` | `HudPanel`: anchored blocks of HUD text, refreshed at a fixed rate |
| `textbox.c/h` | `TextBox`: shows a dialog conversation (name plate, pages, typewriter reveal, choices); [DIALOG.md](DIALOG.md) |
| `menu.c/h` | The menu model and its joypad input ([MENU_SYSTEM.md](MENU_SYSTEM.md)) |

## Why text is cached

Measured on the Analogue 3D (P1.6), drawing text costs 15–20 µs per glyph. Almost none of that is layout: caching the laid-out paragraph saved only ~0.4 ms of 4.5. The cost is in issuing the glyphs. The old menu re-drew ~20 text elements every frame (2.6 ms average, 6–9 ms with the RDP validator), and that cost was the trigger of the D18 flicker.

A `UiLayer` renders text into an offscreen RGBA16 surface **only when a slot's text or colour changes**. Every frame, it draws the surface with one copy-mode blit (4 pixels per RDP clock; alpha compare skips transparent texels). So a menu that just sits open costs a few rectangles and one blit.

## UiLayer

```c
UiLayer L;
ui_layer_init(&L, 260, 200, true);                          // w, h, cached
int title = ui_layer_add(&L, 0, 38, 260, 17, 50,             // box x, y, w, h; baseline
                         FONT_UI_VAR, ALIGN_CENTER);
ui_layer_set(&L, title, RGBA32(255,255,255,255), "Start Menu");   // no-op if unchanged
ui_layer_setf(&L, title, color, "HP %d", hp);                    // formatted
...
ui_layer_draw(&L, 30, 12, used_h);   // render dirty slots, then blit rows [0, used_h)
```

- **Slots** are boxes, each with a font, an alignment, a colour and up to 39 bytes of text. Re-rendering a slot clears its box, so boxes must not overlap.
  - The box is widened to 4-pixel columns: fill mode on a 16-bit surface needs a 4-pixel-aligned scissor, and the RDP validator reports it otherwise.
  - The text keeps its exact position.
- **Rendering** attaches the surface (a nested `rdpq_attach`), clears and draws each dirty slot under a scissor, then detaches. `rdpq_detach` restores the frame buffer but not the scissor, so the layer resets the scissor to the full display.
- **Immediate mode** (`cached = false`) draws every slot straight to the screen every frame. It needs no surface memory; the UI benchmark uses it for the before/after comparison.
- **Memory:** `w × h × 2` bytes, allocated on the first draw. The Start menu's layer is 264 × 186 px, about 98 KB. `ui_layer_free()` releases it after an `rspq_wait()`.
- **`ui_layer_invalidate()`** re-renders everything on the next draw, for example after a style change.
- **Drop shadow:** `ui_layer_set_shadow(L, color)` draws each slot's text again 1 px down and right in that colour, behind the text. It keeps plain-font text readable over the 3D scene (the plain fonts have no outline). It doubles the cost of a re-render, not of the per-frame blit.
- **Render budget:** `ui_layer_set_budget(L, n)` re-renders at most `n` dirty slots per frame; the rest keep their old text until a later frame. HUDs (2) and the overlay (6) use it, so a refresh that changes many lines spreads over a few frames instead of landing in one. Menus render everything at once (0) so the screen never shows a half-updated panel.
- **Counters:** `ui_renders` (slots rendered this frame) and `ui_blits` (layers drawn), on the Stats page and in the `STATS` CSV row. A static open menu shows 0 renders and 1 blit.

### Canvas and partial blits

A cached layer can also hold free-form content instead of slots. The text box renders each dialog page this way:

```c
if (ui_layer_canvas_begin(&L)) {             // allocate if needed, attach, clear to transparent
    text_render_paragraph(p, 0, 0);          // anything: a laid-out paragraph, text_draw, rectangles
    ui_layer_canvas_end(&L);                 // detach, reset the scissor
}
...
ui_layer_draw_part(&L, x, y, sx, sy, sw, sh);   // blit only the source rectangle, at (x + sx, y + sy)
```

`ui_layer_draw_part()` is what makes the typewriter reveal cheap: completed lines and the typed part of the current one are two copy-mode blits of the cached page. Use a canvas layer for canvas content only (no slots).

### Fonts for cached text

libdragon's built-in debug fonts are outlined. Outlined fonts can't use the RDP's alpha test (an RDP bug, per libdragon's source), so they fake transparency by blending with coverage. On screen that works; offscreen it leaves the alpha bit of text pixels at 0, and the blit then skips them.

So cached text uses plain monochrome builds of the same two fonts, which use the alpha test and write full coverage:

| Id | Source | Use |
|---|---|---|
| `FONT_DEBUG_MONO`, `FONT_DEBUG_VAR` | libdragon built-ins (outlined) | direct text: demo HUD, benchmark status line, overlay |
| `FONT_UI_MONO`, `FONT_UI_VAR` | `assets/fonts/monogram.ttf`, `at01.ttf` (CC0; licences beside them) | cached UI text |

The Makefile converts every `assets/fonts/*.ttf` to `filesystem/fonts/*.font64` with `mkfont --monochrome --range 20-7F` (`MKFONT_FLAGS`). A new font must be monochrome and not outlined to be usable in a layer.

### Text and the render mode

`text_draw()` calls `rdpq_set_mode_standard()` before each print.
- The font sets its own render mode inside a pre-recorded rspq block, which libdragon's CPU-side mode tracking doesn't see.
- Without that call, the CPU still believes the previous mode (fill from a panel line, copy from a layer blit) and emits the glyph rectangles for it. The text then comes out invisible, which showed up on the A3D as menu rows vanishing after the menu was reopened.
- rdpq_text's own "anti-alias fix" rectangle (a blended rectangle behind every print) used to set standard mode as a side effect. The display has no VI anti-aliasing (`FILTERS_RESAMPLE`), so that rectangle is disabled (`TEXT_AA_FIX 0` in `text.c`); in a layer it would also draw dark boxes.
- The same applies to a paragraph laid out with `rdpq_paragraph_build()`: render it with `text_render_paragraph()`, never `rdpq_paragraph_render()` directly. In S5.3 a page rendered straight after a canvas's fill-mode clear still showed, but every `text_draw()` into another layer later in the frame wrote nothing (the text box's speaker names vanished; found by reading the layer surface back after `rspq_wait()`).

## UiStyle

A style is a `const UiStyle` with static storage. Widgets keep a pointer to it; `menu_view_set_style()` switches it and re-renders.

| Group | Fields |
|---|---|
| Fonts | `font_title`, `font_body`, `font_dialog` |
| Text colours | `title`, `header`, `text`, `hilite` (cursor row), `disabled`, `footer` |
| Panel | `panel` (alpha < 255 blends), `border` + `border_w`, `separator`, `cursor_bar` (a bar behind the cursor row, alpha 0 = none) |
| Scroll bar | `scroll_track`, `scroll_thumb`, `scroll_w` (0 = none) |
| Menu layout (screen px, text y = baselines) | `menu_x0/x1`, `pad`, `title_y`, `tab_y`, `sep_y`, `items_y`, `row_h`, `label_x`, `value_x`, `footer_pad` |
| Decorations (printf formats) | `value_fmt` (`"< %s >"`), `tab_fmt`, `tab_fmt_single`, `footer_multi`, `footer_single`, `more` (text scroll markers, `""` = none) |

Styles also carry the HUD colours (`hud_title`, `hud_text`, `hud_accent`, `hud_shadow`, `hud_backdrop`), the gauge colours (`gauge_bg`, `gauge_fill`, `gauge_warn`), `panel2` for a vertical gradient, and the text box: `dialog_text` (the dialog palette's `text` colour; the others reuse `hud_accent`, `hilite`, `title`, `disabled`), the box `tb_x0/y0/x1/y1`, `tb_pad`, `tb_lines` and the typewriter speed `tb_cps` (`TEXTBOX_LAYOUT` holds the shared values).

| Built-in style | Look |
|---|---|
| `ui_style_debug` | the engine's original look: translucent black box, yellow cursor row, grey rows, thin scroll bar; HUD text with a black shadow |
| `ui_style_classic` | classic RPG window: blue vertical gradient, 2 px white frame, white text, gold highlights, a translucent white cursor bar; HUD panels on dark blue backdrops |
| `ui_style_minimal` | dark and quiet: near-black panel, 1 px gold frame and separator, cream text, gold cursor bar, values without arrows; HUD text with a shadow, no backdrop |

`ui_styles[UI_STYLE_COUNT]` lists them for a picker. The demo's **Settings → UI Style** item switches the menu and the HUD live, even while the menu is open (`menu_view_set_style()` re-lays out and re-renders the menu).

To add a style: copy one of the definitions in `ui_style.c` (`MENU_LAYOUT` holds the shared metrics), change what you need, add it to `ui_styles[]` and raise `UI_STYLE_COUNT`, and add its name to `ui_style_options[]` in `main.c`. Keep text slots apart when changing metrics: slot boxes must not overlap.

## Primitives (`ui_draw.h`)

Drawn every frame as RDP rectangles, never triangles:
- **Opaque** colours use fill mode.
- **Translucent** colours use standard mode with a flat combiner and the blender. `rdpq_fill_rectangle` works in either mode.

| Function | Draws |
|---|---|
| `ui_rect(color, x0, y0, x1, y1)` | a filled rectangle (skipped when alpha is 0) |
| `ui_vgradient(top, bottom, x0, y0, x1, y1)` | a vertical gradient as 8 horizontal bands (a flat rectangle if the colours match) |
| `ui_frame(color, t, x0, y0, x1, y1)` | a frame `t` px thick inside the rectangle |
| `ui_gauge(bg, fill, fraction, x0, y0, x1, y1)` | a horizontal bar filled to `fraction` |

## MenuView

```c
static MenuView view;
menu_view_init(&view, &ui_style_debug, true);   // once; cached
...
menu_draw(&menu, &view);                         // every frame (draws only when open)
```

Each frame, `menu_draw()` compares what the view last drew with the model: tab, scroll offset, cursor, item count, and each visible row's value and disabled state. Only if something differs does it refresh the slot text, and the layer then re-renders only the slots whose strings or colours changed. A cursor move re-renders 4 slots (two rows × label and value); a tab switch re-renders the whole panel.

Because the view compares against the model, values written by `menu_set_value()` or directly into the `Menu` show up the same way.

What is drawn every frame, as rectangles:
- the panel, frame and separator;
- the cursor bar, if the style has one;
- the scroll bar. On tabs longer than the 7 visible rows, a track runs beside the rows; the thumb's height is the visible share and its position is the scroll offset.

The panel height follows the tab's row count, and a change of row count re-lays out the slots.

## HudPanel

```c
static HudPanel hud;
hud_panel_init(&hud, 0, 186, 320, 38, true, 10, 2);   // x, y, w, h, cached, refresh frames, budget
int fps = hud_panel_line(&hud, 4, 220, 172, FONT_UI_MONO, ALIGN_LEFT);   // left, baseline, width
...
if (hud_panel_due(&hud))                               // true every 10th frame
    hud_panel_setf(&hud, fps, style->hud_accent, "FPS: %.0f", fps_value);
hud_panel_draw(&hud, style);                           // backdrop, shadow and the cached text
```

A panel is a `UiLayer` at a screen position with a refresh rate. Content is gathered and formatted only when the panel is due, and a line re-renders only if its text changed. The demo HUD is two panels, both refreshing every 10 frames (~6 Hz) with a budget of 2 lines per frame:
- a title panel (20, 10, 280 × 14);
- a bottom band (0, 186, 320 × 38) with three readouts on the left and three right-aligned.

The camera raycast and the visible-object count behind the readouts run only on refresh frames. A CPU-budget gauge (`ui_gauge`, red above 90 %) sits under the FPS line.

A line's box runs from 8 px above its baseline (10 for `FONT_UI_VAR`) to 4 px below. The title panel is sized so the at01 glyphs (rows 13–20 for baseline 20) sit centred in a style's backdrop.

## Debug overlay

The overlay pages (`src/debug/overlay.c`) use a cached `UiLayer` with one slot per text row (20 rows, 10 px pitch, `FONT_UI_MONO`, budget 6 rows per frame). Rows are recomputed every 15 frames. Unchanged rows (labels, idle values) never re-render, and the page is drawn every frame as one blit plus its panel and bars. The layer (~120 KB) is freed when the overlay is Off.

## Measured cost

See [BENCHMARKS.md](BENCHMARKS.md), "Phase 2 · S5.1". Bench = UI draws a copy of the Start menu over the floor and 16 pillars:
- directly (mode 0) or cached (mode 1);
- with no input, a cursor move every 8 frames, a value change every 2 frames, a tab switch every 30 frames, or a close and reopen every 100 frames.

Steps 40/41 draw a demo-like HUD direct or cached; steps 50/51 play the demo conversation in the text box at reading pace or skipping (a new page every few frames). `BENCH_PROF` rows carry `menu_us`, `hud_us` and `dialog_us`.

## Limits

- Cached text needs plain monochrome fonts, and RGBA16 layers keep 1-bit alpha: anti-aliased fonts would lose their smooth edges.
- A change frame still pays the full text cost of what changed. With the RDP validator on, text costs ~4× more, so cursor moves can overrun the frame (D18, taken up in S6).
- 24 slots per layer, 39 bytes of text per slot.

## Source files

| File | Purpose |
|---|---|
| [src/ui/text.h](../src/ui/text.h), [src/ui/text.c](../src/ui/text.c) | fonts, `text_draw` |
| [src/ui/ui_style.h](../src/ui/ui_style.h), [src/ui/ui_style.c](../src/ui/ui_style.c) | `UiStyle`, `ui_style_debug` |
| [src/ui/ui_draw.h](../src/ui/ui_draw.h), [src/ui/ui_draw.c](../src/ui/ui_draw.c) | rectangles, frames, gauges |
| [src/ui/ui_layer.h](../src/ui/ui_layer.h), [src/ui/ui_layer.c](../src/ui/ui_layer.c) | cached text slots |
| [src/ui/menu_view.h](../src/ui/menu_view.h), [src/ui/menu_view.c](../src/ui/menu_view.c) | menu drawing |
| [src/ui/ui_hud.h](../src/ui/ui_hud.h), [src/ui/ui_hud.c](../src/ui/ui_hud.c) | HUD panels |
| [src/ui/textbox.h](../src/ui/textbox.h), [src/ui/textbox.c](../src/ui/textbox.c) | dialog text box |
| [assets/fonts/](../assets/fonts/) | TTF sources for the UI fonts, with their licences |
