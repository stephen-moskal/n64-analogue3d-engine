# Dialog

Conversations are written as JSON, compiled into a small binary bank at build time, and played by a runner that a text box draws in the current UI style.

```
assets/dialog/demo.json ──tools/dialog_build.py──▶ filesystem/dialog/demo.dlg ──▶ rom:/dialog/demo.dlg
                                                                                     │ dialog_bank_load()
                    game hooks (events, conditions, variables) ◀── DialogRunner ◀────┘
                                                                        │
                                                          TextBox (src/ui/textbox.h) in a UiStyle
```

| Part | Where | Role |
|---|---|---|
| Source | `assets/dialog/*.json` | what writers edit |
| Compiler | `tools/dialog_build.py` | checks every reference, compiles markup, writes the `.dlg` bank; the Makefile runs it for every JSON file |
| Runtime | `src/dialog/dialog.c/h` | loads a bank; `DialogRunner` walks one conversation. Pure C with no rendering, so it is host-tested (`tests/host/test_dialog.c`) |
| Text box | `src/ui/textbox.c/h` | shows a runner: name plate, word-wrapped pages, typewriter reveal, advance prompt, choice list |

The split keeps the story data out of the code and the look out of the story: the same JSON plays in any `UiStyle`, and a game with its own dialog UI can use the runner alone.

## How games usually define text

Most engines keep dialog outside the code, in one of three shapes:
- **String tables** (CSV or spreadsheets keyed by id): simple and translator-friendly, but branching lives in code.
- **Scripting languages** (Ink, Yarn Spinner): text with inline branching, conditions and commands; powerful, but a language to learn and a runtime to port.
- **Node graphs in an editor** (Articy, Dialogue System), exported as JSON or XML.

This engine uses the middle ground that fits a small cartridge: a JSON file of named conversations, each a list of lines with optional choices, jumps, events and conditions. Branching and flags stay in the data, and the game answers them through three hooks. The compiler turns it into a compact bank so the ROM carries no JSON parser.

## Quick start

1. Write `assets/dialog/<name>.json`:
   ```json
   {
     "speakers": {
       "guide": { "name": "Guide", "color": "accent" },
       "you":   { "name": "{player}", "color": "hilite" }
     },
     "conversations": {
       "intro": [
         { "speaker": "guide", "text": "Hello, {player}! [c=title]Welcome[/c].[p=300] Ready?" },
         { "id": "ask", "text": "What now?", "choices": [
             { "text": "Open the gate", "next": "gate", "if": "!gate_open" },
             { "text": "Leave", "end": true } ] },
         { "id": "gate", "text": "There you go.", "event": "open_gate" },
         { "speaker": "you", "text": "Thanks!", "next": "ask" }
       ]
     }
   }
   ```
2. `libdragon make`: the Makefile compiles it to `filesystem/dialog/<name>.dlg` (packed as `rom:/dialog/<name>.dlg`). A mistake stops the build with its location (see [Errors](#errors)).
3. Play it:
   ```c
   static DialogBank  *bank;
   static DialogRunner runner;
   static TextBox      box;

   bank = dialog_bank_load("rom:/dialog/demo.dlg");       // scene init
   textbox_init(&box);

   DialogHooks hooks = { on_event, on_check, on_variable, scene };
   if (dialog_start(&runner, bank, "intro", &hooks))       // e.g. when the player talks
       textbox_open(&box, &runner);

   // update: while textbox_active(&box), feed it input and skip game input
   UiInput in = { .confirm = a_pressed, .cancel = b_pressed, .up = up, .down = down };
   textbox_update(&box, &in, dt);

   // post-draw, after the 3D scene
   if (textbox_active(&box)) textbox_draw(&box, style);

   textbox_close(&box); dialog_bank_free(bank);            // scene cleanup
   ```

The demo does exactly this (`src/scenes/demo_scene.c`, "Dialog" section): **Debug → Dialog → Talk!** starts `assets/dialog/demo.json`.

## Source format

### Top level

| Key | Required | Value |
|---|---|---|
| `speakers` | no | object: speaker id → `"Display Name"` or `{ "name": ..., "color": ... }` |
| `conversations` | yes | object: conversation id → non-empty list of lines |

Ids (speakers, conversations, lines) are letters, digits and `_`. Speaker names may contain variables (`"{player}"`); the colour is a palette name (default `title`).

### Lines

A line is a string (text only, same speaker as the line before) or an object:

| Key | Meaning |
|---|---|
| `text` | required. The line, with [markup](#markup) |
| `speaker` | speaker id. Omitted: the previous line's speaker (none at the start of a conversation) |
| `id` | a name other lines and choices can jump to |
| `next` | a [target](#targets) to continue with instead of the following line |
| `end` | `true`: the conversation ends after this line |
| `event` | a string handed to the game's `event` hook when the line starts (`"spawn_ball"`, `"sound:select"`) |
| `choices` | 1–4 choices shown after the line (then `next` and `end` are not allowed) |

Flow: lines run in order. A line with `next` jumps; the last line of a conversation (without `next`) ends it.

### Choices

| Key | Meaning |
|---|---|
| `text` | required. Plain text, no markup (variables allowed), up to 36 bytes |
| `next` | a [target](#targets) |
| `end` | `true`: picking it ends the conversation (instead of `next`) |
| `if` | a condition string for the game's `check` hook; the choice is hidden when it returns false |

The text box shows about 24 characters per choice row at the default layout.

### Targets

| Form | Goes to |
|---|---|
| `"ask"` | the line with `"id": "ask"` in the same conversation |
| `"@howto"` | the first line of conversation `howto` |
| `"intro.ask"` | line `ask` of conversation `intro` |

### Markup

| Markup | Effect |
|---|---|
| `[c=accent]...[/c]` | colour from the style palette: `text`, `accent`, `hilite`, `title`, `dim`. An unclosed colour ends at the end of the line |
| `[p=400]` | pause the typewriter 400 ms (20 ms steps, up to 5.1 s) |
| `[page]` | start a new page here |
| `{name}` | a variable, filled by the game's `variable` hook (unknown names stay as written) |
| `[[`, `]]`, `{{` | literal `[`, `]`, `{` |
| `\n` in the JSON string | line break |

Text is printable ASCII (the UI fonts cover 0x20–0x7E); `^` and `$` are fine (escaped for rdpq_text). A line compiles to at most 480 bytes; long lines are split into pages automatically.

### Palette

The palette names map to the `UiStyle` colours, so every style recolours all dialog:

| Name | Style field | Debug style |
|---|---|---|
| `text` | `dialog_text` | light grey |
| `accent` | `hud_accent` | green |
| `hilite` | `hilite` | yellow |
| `title` | `title` | white |
| `dim` | `disabled` | grey |

## Game hooks

```c
typedef struct {
    void (*event)(const char *event, void *ctx);                        // a line started
    bool (*check)(const char *cond, void *ctx);                          // show this choice?
    bool (*variable)(const char *name, char *out, int outlen, void *ctx); // fill {name}
    void *ctx;
} DialogHooks;
```

The strings are whatever the JSON says; their meaning belongs to the game. Any hook may be NULL (no events, every choice shown, variables left as written). The demo's hooks:

| Kind | Name | Demo meaning |
|---|---|---|
| event | `spawn_ball` | drop the physics ball, or relaunch it |
| event | `burst` | particle burst on both pillars |
| event | `sound:<name>` | play a sound: `select`, `open`, `close` |
| condition | `ball_spawned`, `!ball_spawned` | the demo's convention: a leading `!` negates |
| variable | `player`, `fps`, `style` | "Traveller", the current FPS, the UI style name |

Variables are filled when a line starts (the runner holds the filled text), so a value shown on screen doesn't change mid-line.

## The text box

| Behaviour | Detail |
|---|---|
| Layout | from the style: box `tb_x0..tb_x1`, `tb_y0..tb_y1` (default 16,166–304,224), padding `tb_pad`, font `font_dialog` (`FONT_UI_VAR`); name plate above the box on the left, choice list above it on the right |
| Pages | text is word-wrapped to the box; what doesn't fit continues on the next page. `[page]` forces one |
| Reveal | a typewriter at `tb_cps` glyphs per second (45), stopping at `[p=ms]` pauses. A (confirm) or B (cancel) shows the whole page at once |
| Advance | a blinking wedge at the bottom right when a page is complete; A or B continues |
| Choices | appear once the question's last page is shown; up/down move, A picks. B does not pick (no accidental choices) |
| Talk sound | `on_blip(ctx)` every `blip_every` revealed glyphs (3); the demo plays the menu tick |
| Style change | the box lays itself out again and keeps its place; a page already shown stays shown |

**Input.** `textbox_update()` takes a `UiInput` (confirm, cancel, up, down), so the game decides the buttons. The demo maps A/B through the action map (remappable) and the D-pad or stick for choices. While the box is open it skips object interaction, camera control and Start, pauses the Debug D-pad shortcuts (`debug_menu_set_shortcuts()`), and hides the bottom HUD band that the box covers.

**Cost.** A page is laid out (`rdpq_paragraph_build`) and rendered once into a cached layer ([UI.md](UI.md), canvas). While it types, each frame blits the completed lines and a growing part of the current one, cut at the next glyph's pen position. So a revealing box costs a few rectangles and two or three blits; a new page costs one text render. On the A3D (Bench = UI steps 50 and 51, `dialog_us`): **0.30 ms per frame while reading, 0.53 ms when skipping** through pages; a page change frame costs up to ~5.5 ms ([BENCHMARKS.md](BENCHMARKS.md), "Phase 2 · S5.3").

## Errors

The compiler checks everything the runtime would trip on, and names the place:

```
assets/dialog/demo.json:12:31: JSON error: Expecting ',' delimiter
error: assets/dialog/demo.json: intro[3].choices[1]: unknown target 'tour' (ids in 'intro': ['ask', 'bye'])
error: assets/dialog/demo.json: intro[0]: unknown colour 'pink' (use one of text, accent, hilite, title, dim)
error: assets/dialog/demo.json: howto[2]: unknown speaker 'mage' (declare it under 'speakers')
```

`intro[3]` is the fourth line of conversation `intro`. Other checks: duplicate ids, unknown keys (typos such as `"nxt"`), `next` together with `choices`, 1–4 choices, text length, characters outside the fonts, unclosed `[` or `{`, bad variable names.

The runtime validates a bank again when it loads (`dialog_bank_parse`: magic, version, sizes, every index in range), so a corrupt or stale file fails with a message instead of reading out of bounds. `python3 tools/dialog_build.py --selftest` tests the compiler; the host tests compile `tests/host/data/test_dialog.json` with it and play it.

## Binary format (`.dlg`, version 1)

Big-endian, as the N64 reads it.

| Part | Layout |
|---|---|
| Header, 20 bytes | `"DLG1"`, u16 version, u16 flags, u16 speakers, u16 conversations, u16 nodes (lines), u16 choices, u32 string bytes |
| Speaker, 8 bytes | u32 name, u8 colour, 3 pad |
| Conversation, 8 bytes | u32 id, u16 first node, 2 pad |
| Node, 16 bytes | u32 text, u32 event, u16 speaker, u16 next, u16 first choice, u8 choice count, u8 flags |
| Choice, 12 bytes | u32 text, u32 condition, u16 next, 2 pad |
| Strings | NUL-terminated; fields hold byte offsets (`0xFFFFFFFF` = none; `0xFFFF` = no speaker / no next) |

Compiled text is rdpq_text-ready: colours are `^NN` style switches (ids 2–6, bound per style by the text box), `^`/`$` are doubled, a pause is byte `0x01` followed by its length in 20 ms units (1–255), a page break is `0x02`. `DialogRunner` fills `{variables}` into its own buffer (512 bytes).

## Extending

| To add | Change |
|---|---|
| A markup tag | `compile_text()` in the compiler (emit a control byte below 0x20), `prepare_line()` in `textbox.c` (strip it and record what it means); document it here |
| A palette colour | `PALETTE` in the compiler, the `DIALOG_COL_*` enum, `bind_palette()` and `palette()` in `textbox.c`, a style field if it needs one |
| Portraits | a `portrait` key on speakers → a u16 texture slot in the speaker record (bump the version) → the text box draws it beside the name plate |
| Per-speaker talk sounds | a `voice` key on speakers → `on_blip` gets the speaker index |
| Localisation | one bank per language (`demo.en.json`, `demo.fr.json`) with the same conversation and line ids; load the one for the chosen language. The fonts cover ASCII only: accented text needs a font with a wider `--range` |
| More than 4 choices | `MAX_CHOICES` in the compiler, `DIALOG_CHOICE_MAX` in `dialog.h`; the text box's choice list grows upwards (`UI_H`) |
| Another look | a `UiStyle` (colours, fonts, `tb_*` layout); the text box needs no code changes |
| A different UI | use `DialogRunner` alone: `dialog_speaker()`, `dialog_text()` (strip `^NN` and the control bytes), `choice_text[]`, `dialog_advance()`, `dialog_choose()` |

Not yet: saving the game's flags (conditions ask the game, which owns its state), and variables that change while a line is on screen.

## Source files

| File | Purpose |
|---|---|
| [tools/dialog_build.py](../tools/dialog_build.py) | JSON → `.dlg` compiler (`--selftest`) |
| [src/dialog/dialog.h](../src/dialog/dialog.h), [src/dialog/dialog.c](../src/dialog/dialog.c) | bank loading and validation, `DialogRunner` |
| [src/ui/textbox.h](../src/ui/textbox.h), [src/ui/textbox.c](../src/ui/textbox.c) | the text box |
| [assets/dialog/demo.json](../assets/dialog/demo.json) | the demo conversation (a tour of the features) |
| [tests/host/test_dialog.c](../tests/host/test_dialog.c), [tests/host/data/test_dialog.json](../tests/host/data/test_dialog.json) | host tests |
