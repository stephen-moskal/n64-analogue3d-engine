#include "test.h"
#include "input/action.h"
#include <string.h>

// The action layer (src/input/action.c): contexts, priorities, consumption,
// modal contexts, chords, stick directions, axes, repeat, players and ports
// (ROADMAP_v2 S9, D12). Pads are fed by hand; want[] is what each port holds
// at the next update, and update() derives the edges like the input core.

#define DT 0.125f                   // exact in binary: repeat timing without rounding

enum { ACT_A = ACTION_GAME_FIRST, ACT_B, ACT_X, ACT_Y, ACT_MOVE, ACT_UP };

static PadState pads[PAD_PORTS];
static uint16_t want[PAD_PORTS];
static ActionContext game, high;

static void reset_all(void) {
    action_init();
    memset(pads, 0, sizeof(pads));
    memset(want, 0, sizeof(want));
    for (int p = 0; p < PAD_PORTS; p++) pads[p].style = PAD_N64;
    action_set_repeat(0.25f, 0.125f);
}

static void update(void) {
    for (int p = 0; p < PAD_PORTS; p++) {
        pads[p].pressed  = want[p] & (uint16_t)~pads[p].held;
        pads[p].released = pads[p].held & (uint16_t)~want[p];
        pads[p].held     = want[p];
    }
    action_update(pads, DT);
}

static void game_context(const ActionBinding *b, int n) {
    action_context_init(&game, "Game", 0, CTX_CONSUME, b, n);
    action_push_context(0, &game);
}

// Pressed on the first update, held after, released once
static void test_edges(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A) };
    game_context(b, 1);
    want[0] = PAD_BIT(BTN_A);
    update();
    CHECK(action_pressed(0, ACT_A) && action_held(0, ACT_A) && !action_released(0, ACT_A));
    CHECK_NEAR(action_value(0, ACT_A), 1.0, 1e-6);
    update();
    CHECK(!action_pressed(0, ACT_A) && action_held(0, ACT_A));
    CHECK_NEAR(action_held_time(0, ACT_A), DT, 1e-6);
    want[0] = 0;
    update();
    CHECK(!action_held(0, ACT_A) && action_released(0, ACT_A));
    CHECK_NEAR(action_value(0, ACT_A), 0.0, 1e-6);
    update();
    CHECK(!action_released(0, ACT_A));
    CHECK(!action_pressed(0, ACT_B));                  // unbound
    CHECK(!action_pressed(5, ACT_A) && !action_pressed(0, ACTION_MAX));   // out of range
}

// A press and release between two updates (a tap below 60 FPS) still counts
static void test_tap(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A) };
    game_context(b, 1);
    pads[0].pressed = pads[0].released = PAD_BIT(BTN_A);
    action_update(pads, DT);
    CHECK(action_pressed(0, ACT_A) && action_released(0, ACT_A) && !action_held(0, ACT_A));
    CHECK(action_repeat(0, ACT_A));                    // a menu moves once
}

// Higher priority first; a consuming context hides the buttons it uses
static void test_priority_consume(void) {
    reset_all();
    static const ActionBinding low_b[]  = { BIND(ACT_A, BTN_A), BIND(ACT_B, BTN_B) };
    static const ActionBinding high_b[] = { BIND(ACT_X, BTN_A) };
    game_context(low_b, 2);
    action_context_init(&high, "High", 10, CTX_CONSUME, high_b, 1);
    action_push_context(0, &high);
    CHECK(action_context_at(0, 0) == &high && action_context_at(0, 1) == &game);
    want[0] = PAD_BIT(BTN_A) | PAD_BIT(BTN_B);
    update();
    CHECK(action_pressed(0, ACT_X));
    CHECK(!action_held(0, ACT_A));                     // consumed above
    CHECK(action_pressed(0, ACT_B));                   // B passes through

    // Not consuming: both see A
    high.flags = 0;
    want[0] = 0;
    update();
    want[0] = PAD_BIT(BTN_A);
    update();
    CHECK(action_pressed(0, ACT_X) && action_pressed(0, ACT_A));

    // Pushing again does not duplicate; popping restores the game context
    CHECK(action_push_context(0, &high) && action_context_count(0) == 2);
    action_pop_context(0, &high);
    CHECK(action_context_count(0) == 1 && !action_has_context(0, &high));
}

// A modal context (the engine UI) hides every input, bound or not
static void test_modal(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A), BIND(ACT_B, BTN_C_UP) };
    game_context(b, 2);
    action_push_context(0, &action_ctx_ui);
    want[0] = PAD_BIT(BTN_A) | PAD_BIT(BTN_C_UP);
    update();
    CHECK(action_pressed(0, ACTION_UI_CONFIRM));
    CHECK(!action_held(0, ACT_A) && !action_held(0, ACT_B));   // C-Up is not the UI's, still hidden
    UiInput ui = action_ui(0);
    CHECK(ui.confirm && !ui.cancel && !ui.up);
}

// A game action held when a modal context arrives reads released
static void test_released_when_blocked(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_C_UP) };
    game_context(b, 1);
    want[0] = PAD_BIT(BTN_C_UP);
    update();
    CHECK(action_held(0, ACT_A));
    action_push_context(0, &action_ctx_ui);
    update();
    CHECK(!action_held(0, ACT_A) && action_released(0, ACT_A));
}

// Held when a context above goes away: held, but no press and no repeats
// (closing a menu with B must not also fire the game's B)
static void test_revealed_hold(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_B, BTN_B) };
    game_context(b, 1);
    action_push_context(0, &action_ctx_ui);
    want[0] = PAD_BIT(BTN_B);
    update();
    CHECK(action_pressed(0, ACTION_UI_CANCEL) && !action_held(0, ACT_B));
    action_pop_context(0, &action_ctx_ui);
    for (int i = 0; i < 8; i++) {
        update();
        CHECK(action_held(0, ACT_B) && !action_pressed(0, ACT_B) && !action_repeat(0, ACT_B));
    }
}

// The debug context sits below the game: a game binding on its button wins
// (the S8 rule that a shortcut only works on an unbound button)
static void test_debug_below_game(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A) };
    game_context(b, 1);
    action_push_context(0, &action_ctx_debug);
    CHECK(action_context_at(0, 1) == &action_ctx_debug);
    want[0] = PAD_BIT(BTN_D_UP);
    update();
    CHECK(action_pressed(0, ACTION_DEBUG_OVERLAY));
    want[0] = 0;
    update();

    CHECK(action_context_set_button(&game, ACT_A, 0, BTN_D_UP));   // remap onto the shortcut
    CHECK(action_context_button(&game, ACT_A, 0) == BTN_D_UP);
    want[0] = PAD_BIT(BTN_D_UP);
    update();
    CHECK(action_pressed(0, ACT_A) && !action_pressed(0, ACTION_DEBUG_OVERLAY));
}

// Chords: modifier held, then the trigger; the chord takes the trigger from
// the same context's plain binding; action_chord_pressed on two actions
static void test_chords(void) {
    reset_all();
    static const ActionBinding b[] = {
        BIND(ACT_A, BTN_A), BIND(ACT_Y, BTN_Z), BIND_CHORD(ACT_B, BTN_Z, BTN_A),
    };
    game_context(b, 3);
    want[0] = PAD_BIT(BTN_A);                          // A alone
    update();
    CHECK(action_pressed(0, ACT_A) && !action_held(0, ACT_B));
    want[0] = 0;
    update();

    want[0] = PAD_BIT(BTN_Z);                          // Z, then A
    update();
    CHECK(action_pressed(0, ACT_Y));
    want[0] = PAD_BIT(BTN_Z) | PAD_BIT(BTN_A);
    update();
    CHECK(action_pressed(0, ACT_B) && !action_held(0, ACT_A));
    CHECK(action_held(0, ACT_Y));                      // the modifier's own action stays held
    CHECK(action_chord_pressed(0, ACT_Y, ACT_B));

    want[0] = PAD_BIT(BTN_A);                          // Z let go: the chord ends
    update();
    CHECK(action_released(0, ACT_B) && !action_pressed(0, ACT_A));
}

// A consuming context's active chord also takes its modifier from below
static void test_chord_consumes_modifier(void) {
    reset_all();
    static const ActionBinding low_b[]  = { BIND(ACT_A, BTN_L) };
    static const ActionBinding high_b[] = { BIND_CHORD(ACT_X, BTN_L, BTN_D_UP) };
    game_context(low_b, 1);
    action_context_init(&high, "High", 10, CTX_CONSUME, high_b, 1);
    action_push_context(0, &high);
    want[0] = PAD_BIT(BTN_L);
    update();
    CHECK(action_pressed(0, ACT_A));                   // the chord is not active yet
    want[0] = PAD_BIT(BTN_L) | PAD_BIT(BTN_D_UP);
    update();
    CHECK(action_pressed(0, ACT_X) && !action_held(0, ACT_A));
}

// Stick directions: 4-way, on past 0.5, off below 0.35 (after the deadzone)
static void test_stick_dirs(void) {
    reset_all();
    static const ActionBinding b[] = { BIND_STICK(ACT_UP, STICK_UP) };
    game_context(b, 1);
    pads[0].stick_y = 40;                              // 0.5 raw -> 0.44: not yet
    update();
    CHECK(!action_held(0, ACT_UP));
    pads[0].stick_y = 48;                              // 0.6 -> 0.56
    update();
    CHECK(action_pressed(0, ACT_UP));
    pads[0].stick_y = 36;                              // 0.39: kept (hysteresis)
    update();
    CHECK(action_held(0, ACT_UP) && !action_pressed(0, ACT_UP));
    pads[0].stick_y = 30;                              // 0.31: off
    update();
    CHECK(action_released(0, ACT_UP));
    pads[0].stick_x = 70;                              // right dominates: not up
    pads[0].stick_y = 50;
    update();
    CHECK(!action_held(0, ACT_UP));
}

// Axis actions: the processed stick (radial deadzone, rescaled), a digital
// binding as -1, the strongest active binding wins
static void test_axis(void) {
    reset_all();
    static const ActionBinding b[] = {
        BIND_AXIS(ACT_MOVE, AXIS_STICK_X, 1), BIND_NEG(ACT_MOVE, BTN_D_LEFT),
    };
    game_context(b, 2);
    pads[0].stick_x = 7;                               // inside the deadzone (8 of 80): centred
    update();
    CHECK(!action_held(0, ACT_MOVE));
    CHECK_NEAR(action_value(0, ACT_MOVE), 0.0, 1e-6);
    pads[0].stick_x = 44;                              // 0.55 -> (0.55 - 0.1) / 0.9 = 0.5
    update();
    CHECK(action_pressed(0, ACT_MOVE));
    CHECK_NEAR(action_value(0, ACT_MOVE), 0.5, 1e-5);
    pads[0].stick_x = 127;                             // past full deflection: 1
    update();
    CHECK_NEAR(action_value(0, ACT_MOVE), 1.0, 1e-6);
    pads[0].stick_x = 57;                              // diagonal: the magnitude is kept, not each axis
    pads[0].stick_y = 57;
    update();
    CHECK_NEAR(action_value(0, ACT_MOVE), 0.7071, 1e-3);
    pads[0].stick_x = pads[0].stick_y = 0;
    want[0] = PAD_BIT(BTN_D_LEFT);
    update();
    CHECK_NEAR(action_value(0, ACT_MOVE), -1.0, 1e-6);
    CHECK_NEAR(action_axis(0, AXIS_STICK_X), 0.0, 1e-6);

    // Axial deadzone and a curve
    StickConfig c = { .deadzone = 0.2f, .outer = 100.0f, .curve = 2.0f, .axial = true };
    action_set_stick(0, &c);
    want[0] = 0;
    pads[0].stick_x = 60;                              // 0.6 -> 0.5 -> 0.25
    pads[0].stick_y = 10;                              // inside its own deadzone
    update();
    CHECK_NEAR(action_axis(0, AXIS_STICK_X), 0.25, 1e-5);
    CHECK_NEAR(action_axis(0, AXIS_STICK_Y), 0.0, 1e-6);
}

// Repeat: on the press, after the delay, then every interval
static void test_repeat(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_D_DOWN) };
    game_context(b, 1);
    want[0] = PAD_BIT(BTN_D_DOWN);
    int fired[6];
    for (int i = 0; i < 6; i++) {
        update();
        fired[i] = action_repeat(0, ACT_A);
    }
    // delay 0.25 = 2 updates, interval 0.125 = every update after
    CHECK(fired[0] && !fired[1] && fired[2] && fired[3] && fired[4] && fired[5]);
    action_set_repeat(0.25f, 0.25f);
    want[0] = 0;
    update();
    want[0] = PAD_BIT(BTN_D_DOWN);
    for (int i = 0; i < 6; i++) {
        update();
        fired[i] = action_repeat(0, ACT_A);
    }
    CHECK(fired[0] && !fired[1] && fired[2] && !fired[3] && fired[4] && !fired[5]);
}

// Players read their own port; any port can be any player's
static void test_players(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A) };
    ActionContext per[ACTION_PLAYERS];
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        action_context_init(&per[p], "Game", 0, CTX_CONSUME, b, 1);
        action_push_context(p, &per[p]);
    }
    want[2] = PAD_BIT(BTN_A);                          // port 3
    update();
    CHECK(!action_pressed(0, ACT_A) && action_pressed(2, ACT_A));
    int who = -1;
    CHECK(action_any_pressed(ACT_A, &who) && who == 2);

    action_set_port(0, 2);                             // player 1 on port 3
    want[2] = 0;
    update();
    want[2] = PAD_BIT(BTN_A);
    update();
    CHECK(action_pressed(0, ACT_A) && action_pressed(2, ACT_A));

    // A player's remap is its own
    CHECK(action_context_set_button(&per[1], ACT_A, 0, BTN_B));
    want[1] = PAD_BIT(BTN_B);
    update();
    CHECK(action_pressed(1, ACT_A));

    pads[3].style = PAD_NONE;                          // nothing on port 4
    update();
    CHECK(!action_connected(3) && action_connected(1));
    action_set_port(3, -1);
    CHECK(action_port(3) == -1);
    update();
    CHECK(!action_connected(3));
}

// The engine UI context as UiInput: D-pad or stick, repeating
static void test_ui_input(void) {
    reset_all();
    action_push_context(0, &action_ctx_ui);
    want[0] = PAD_BIT(BTN_D_DOWN) | PAD_BIT(BTN_R);
    update();
    UiInput ui = action_ui(0);
    CHECK(ui.down && ui.next_tab && !ui.up && !ui.confirm);
    update();
    ui = action_ui(0);
    CHECK(!ui.down && !ui.next_tab);                   // until the repeat delay
    update();
    CHECK(action_ui(0).down);                          // the repeat
    want[0] = 0;
    pads[0].stick_y = 70;
    update();
    CHECK(action_ui(0).up);
    CHECK(!action_ui(3).up);                           // another player
    CHECK(strcmp(action_name(ACTION_UI_CONFIRM), "Confirm") == 0);
}

// Context editing and names
static void test_context_data(void) {
    reset_all();
    static const ActionBinding b[] = { BIND(ACT_A, BTN_A), BIND(ACT_A, BTN_C_DOWN), BIND(ACT_B, BTN_B) };
    action_context_init(&game, "Game", 0, CTX_CONSUME, b, 3);
    CHECK(game.count == 3);
    CHECK(action_context_button(&game, ACT_A, 1) == BTN_C_DOWN);
    CHECK(action_context_button(&game, ACT_A, 2) == BTN_NONE);
    CHECK(action_context_set_button(&game, ACT_A, 2, BTN_Z) && game.count == 4);   // past the end: added
    CHECK(!action_context_set_button(&game, ACT_A, 0, BTN_NONE));
    action_context_remove(&game, ACT_A);
    CHECK(game.count == 1 && game.bindings[0].action == ACT_B);

    static const char *const names[] = { "Jump", "Attack" };
    action_set_names(ACT_A, names, 2);
    CHECK(strcmp(action_name(ACT_B), "Attack") == 0);
    CHECK(strcmp(action_name(ACT_X), "?") == 0);

    for (int i = 0; i < ACTION_MAX_CONTEXTS; i++) {
        static ActionContext many[ACTION_MAX_CONTEXTS];
        action_context_init(&many[i], "Many", i, 0, NULL, 0);
        CHECK(action_push_context(1, &many[i]));
    }
    CHECK(!action_push_context(1, &game));             // the stack is full
    CHECK(action_context_at(1, 0)->priority == ACTION_MAX_CONTEXTS - 1);   // highest first
    action_clear_contexts(1);
    CHECK(action_context_count(1) == 0);
}

void run_action_tests(void) {
    RUN_TEST(test_edges);
    RUN_TEST(test_tap);
    RUN_TEST(test_priority_consume);
    RUN_TEST(test_modal);
    RUN_TEST(test_released_when_blocked);
    RUN_TEST(test_revealed_hold);
    RUN_TEST(test_debug_below_game);
    RUN_TEST(test_chords);
    RUN_TEST(test_chord_consumes_modifier);
    RUN_TEST(test_stick_dirs);
    RUN_TEST(test_axis);
    RUN_TEST(test_repeat);
    RUN_TEST(test_players);
    RUN_TEST(test_ui_input);
    RUN_TEST(test_context_data);
}
