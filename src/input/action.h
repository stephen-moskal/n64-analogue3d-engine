#ifndef ACTION_H
#define ACTION_H

/*
 * Actions: game code asks "was Jump pressed?", not "was A pressed?".
 *
 * Up to ACTION_PLAYERS players, each reading one controller port. A player
 * has a stack of contexts, and each context binds inputs (buttons, chords,
 * stick directions, analog axes) to action ids. The contexts are evaluated
 * from the highest priority down; a CTX_CONSUME context hides the inputs its
 * active bindings use from the contexts below it, and a CTX_MODAL one hides
 * everything (a menu or a dialog). The engine owns ids below
 * ACTION_GAME_FIRST (UI and debug); the game numbers its own from there.
 *
 *   enum { ACT_JUMP = ACTION_GAME_FIRST, ACT_ATTACK, ACT_MOVE_X };
 *   static const ActionBinding defaults[] = {
 *       BIND(ACT_JUMP, BTN_A), BIND_CHORD(ACT_ATTACK, BTN_Z, BTN_B),
 *       BIND_AXIS(ACT_MOVE_X, AXIS_STICK_X, 1), BIND_NEG(ACT_MOVE_X, BTN_D_LEFT),
 *   };
 *   ActionContext play;
 *   action_context_init(&play, "Play", 0, CTX_CONSUME, defaults, ARRAY_LEN(defaults));
 *   action_push_context(0, &play);
 *   ...
 *   if (action_pressed(0, ACT_JUMP)) ...
 *   float move = action_value(0, ACT_MOVE_X);          // -1..1
 *
 * Pure C: the input core (input.h) feeds action_update() once per frame with
 * the four ports' PadState. Host-tested (tests/host/test_action.c).
 * docs/INPUT.md.
 */

#include <stdint.h>
#include <stdbool.h>
#include "pad.h"
#include "../ui/ui_input.h"

#define ACTION_PLAYERS       4
#define ACTION_MAX          48     // action ids per player: the engine's, then the game's
#define ACTION_MAX_CONTEXTS  6     // on one player's stack
#define ACTION_MAX_BINDINGS 32     // in one context

typedef uint8_t ActionId;

// The engine's actions. The UI context binds the ACTION_UI_* ones, the debug
// context the ACTION_DEBUG_* ones; games number theirs from ACTION_GAME_FIRST.
enum {
    ACTION_UI_UP, ACTION_UI_DOWN, ACTION_UI_LEFT, ACTION_UI_RIGHT,
    ACTION_UI_CONFIRM, ACTION_UI_CANCEL,
    ACTION_UI_PREV_TAB, ACTION_UI_NEXT_TAB,
    ACTION_UI_START,
    ACTION_DEBUG_OVERLAY,           // next overlay page
    ACTION_DEBUG_DUMP,              // CSV dump
    ACTION_ENGINE_COUNT,
    ACTION_GAME_FIRST = 16
};
_Static_assert(ACTION_ENGINE_COUNT <= ACTION_GAME_FIRST, "engine actions overlap the game's");

// ============================================================
// Bindings and contexts
// ============================================================

typedef enum { SRC_NONE, SRC_BUTTON, SRC_STICK, SRC_AXIS } BindingSource;

// Main stick as four digital directions: the dominant axis only (4-way), on
// past STICK_DIR_ON, off below STICK_DIR_OFF (of full deflection)
typedef enum { STICK_UP, STICK_DOWN, STICK_LEFT, STICK_RIGHT } StickDir;
#define STICK_DIR_ON  0.50f
#define STICK_DIR_OFF 0.35f

// Analog inputs, -1..1 after the player's StickConfig (triggers 0..1)
typedef enum {
    AXIS_STICK_X, AXIS_STICK_Y,     // main stick, right and up positive
    AXIS_CSTICK_X, AXIS_CSTICK_Y,   // GameCube C-stick (N64: the C buttons, digital)
    AXIS_TRIGGER_L, AXIS_TRIGGER_R, // GameCube analog triggers (N64: L, R digital)
    AXIS_COUNT
} PadAxis;

typedef struct {
    ActionId action;
    uint8_t  source;        // BindingSource
    uint8_t  code;          // PadButton, StickDir or PadAxis
    int8_t   sign;          // +1 or -1: the value while active (an axis is multiplied by it)
    uint16_t modifiers;     // PAD_BIT buttons that must be held as well: a chord
} ActionBinding;

// Initializers for binding tables
#define BIND(act, btn)            { (act), SRC_BUTTON, (btn), 1, 0 }
#define BIND_NEG(act, btn)        { (act), SRC_BUTTON, (btn), -1, 0 }      // value -1 (an axis action's other side)
#define BIND_CHORD(act, mod, btn) { (act), SRC_BUTTON, (btn), 1, PAD_BIT(mod) }  // mod held, then btn
#define BIND_STICK(act, dir)      { (act), SRC_STICK, (dir), 1, 0 }
#define BIND_AXIS(act, axis, sgn) { (act), SRC_AXIS, (axis), (sgn), 0 }

#define CTX_CONSUME 0x01    // inputs its active bindings use are hidden from the contexts below
#define CTX_MODAL   0x02    // every input is hidden from the contexts below

typedef struct {
    const char   *name;
    int8_t        priority;         // higher first: engine UI 100, engine debug -100, games around 0
    uint8_t       flags;            // CTX_*
    uint8_t       count;            // bindings in use
    ActionBinding bindings[ACTION_MAX_BINDINGS];
} ActionContext;

// A context is plain data the game owns: fill it from a table, then change
// it at run time (remapping). Players that remap separately need one each.
void action_context_init(ActionContext *ctx, const char *name, int priority, unsigned flags,
                         const ActionBinding *bindings, int count);
bool action_context_add(ActionContext *ctx, ActionBinding binding);     // false when full
void action_context_remove(ActionContext *ctx, ActionId action);        // every binding of it

// Remapping: an action's n-th button binding (n from 0, chords included)
PadButton action_context_button(const ActionContext *ctx, ActionId action, int n);
bool      action_context_set_button(ActionContext *ctx, ActionId action, int n, PadButton btn);

// The engine's contexts. UI (priority 100, modal): D-pad and stick move
// (4-way, repeating), A confirm, B cancel, L/R tabs, Start. Push it for the
// player who drives a menu or dialog, pop it when that closes. Debug
// (priority -100): D-Up next overlay page, D-Down CSV dump; the Debug tab
// pushes it for player 1, below every game context, so a game binding on
// the same button wins.
extern ActionContext action_ctx_ui;
extern ActionContext action_ctx_debug;

// ============================================================
// Players
// ============================================================

// Players 1-4 read ports 1-4 and have no contexts; the engine contexts
// get their default bindings. Called by input_init().
void action_init(void);

// Map this frame's pads to every player's actions (the input core, once per frame)
void action_update(const PadState pads[PAD_PORTS], float dt);

// Which port a player reads (-1: none). Two players may share a port.
void action_set_port(int player, int port);
int  action_port(int player);
bool action_connected(int player);          // its port had a controller at the last update

// The context stack, highest priority first; among equals the newest first.
// A context changes what the player's actions report from the next update.
bool action_push_context(int player, ActionContext *ctx);   // false: stack full (already there: true)
void action_pop_context(int player, const ActionContext *ctx);
void action_clear_contexts(int player);
bool action_has_context(int player, const ActionContext *ctx);
int  action_context_count(int player);
const ActionContext *action_context_at(int player, int i);   // 0 = the top

// ============================================================
// Action state (as of the last update)
// ============================================================

bool  action_pressed(int player, ActionId a);    // a bound input went down (a tap counts)
bool  action_held(int player, ActionId a);       // an active binding is down
bool  action_released(int player, ActionId a);   // not held any more (or a tap ended)
bool  action_repeat(int player, ActionId a);     // on the press, then every interval after the delay
float action_value(int player, ActionId a);      // -1..1: the strongest active binding (0 when idle)
float action_held_time(int player, ActionId a);  // seconds since the press (0 when idle)

// Both held and one of them pressed this frame: two actions as one gesture
bool  action_chord_pressed(int player, ActionId a, ActionId b);

// Any player: pressed this frame; *player (may be NULL) gets the first one
bool  action_any_pressed(ActionId a, int *player);

// The engine's UI actions as a UiInput (directions repeat)
UiInput action_ui(int player);

// Repeat timing for action_repeat (default 0.30 s, then every 0.10 s)
void action_set_repeat(float delay_s, float interval_s);

// ============================================================
// Analog processing (per player: sticks wear differently)
// ============================================================

typedef struct {
    float deadzone;     // 0..1 of full deflection that reads as centred (default 0.10)
    float outer;        // raw value that counts as full deflection; 0: by controller (N64 80, GameCube 90)
    float curve;        // response exponent: 1 linear (default), 2 finer control near the centre
    bool  axial;        // a deadzone per axis (a cross) instead of radial (a circle)
} StickConfig;

void action_set_stick(int player, const StickConfig *cfg);
const StickConfig *action_stick(int player);
// The processed axis, whatever the contexts say (for tools and custom schemes)
float action_axis(int player, PadAxis axis);

// ============================================================
// Names (menus, the Input overlay page)
// ============================================================

void action_set_names(ActionId first, const char *const *names, int count);
const char *action_name(ActionId a);     // the engine's own, or registered, or "?"

#endif
