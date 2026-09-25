#include "action.h"
#include <string.h>
#include <math.h>
#include "../engine/util.h"
#include "../engine/hot.h"

// Held without a press (a context above was popped while the button was
// down): the action reads held, but it never auto-repeats
#define REPEAT_DISARMED 1.0e9f

// Full deflection of the C-stick and the triggers (libdragon's ranges)
#define CSTICK_RANGE   76.0f
#define TRIGGER_RANGE 200.0f

// A player's actions are bit masks (bit = ActionId); the per-action numbers
// are kept only for actions that are held or pressed, and read as 0 through
// the held bit otherwise. An update touches a few cache lines, not one per
// action (48 lines per player per frame cost ~300 us of D-cache misses on
// the first version).
typedef struct {
    int8_t         port;
    bool           connected;
    uint8_t        ctx_count;
    uint8_t        dirs;                    // StickDir bits on (with hysteresis)
    ActionContext *ctx[ACTION_MAX_CONTEXTS];
    StickConfig    stick;
    float          axis[AXIS_COUNT];        // processed, this update
    float          axis_prev[AXIS_COUNT];
    uint64_t       held, pressed, released, repeat;
    float          value[ACTION_MAX];       // valid while held
    float          held_time[ACTION_MAX];   // valid while held
    float          repeat_in[ACTION_MAX];   // seconds to the next repeat, valid while held
} Player;

// One update's raw signals, before the per-action edge and repeat logic
typedef struct {
    uint64_t held, pressed, released;       // bit per action
    float    value[ACTION_MAX];             // valid where held is set
} Signals;
_Static_assert(ACTION_MAX <= 64, "Signals keeps one bit per action");

static Player players[ACTION_PLAYERS];
static float  repeat_delay    = 0.30f;
static float  repeat_interval = 0.10f;
static const char *names[ACTION_MAX];
static const PadState empty_pad;            // PAD_NONE, nothing held

ActionContext action_ctx_ui;
ActionContext action_ctx_debug;

static const ActionBinding ui_defaults[] = {
    BIND(ACTION_UI_UP, BTN_D_UP),          BIND_STICK(ACTION_UI_UP, STICK_UP),
    BIND(ACTION_UI_DOWN, BTN_D_DOWN),      BIND_STICK(ACTION_UI_DOWN, STICK_DOWN),
    BIND(ACTION_UI_LEFT, BTN_D_LEFT),      BIND_STICK(ACTION_UI_LEFT, STICK_LEFT),
    BIND(ACTION_UI_RIGHT, BTN_D_RIGHT),    BIND_STICK(ACTION_UI_RIGHT, STICK_RIGHT),
    BIND(ACTION_UI_CONFIRM, BTN_A),
    BIND(ACTION_UI_CANCEL, BTN_B),
    BIND(ACTION_UI_PREV_TAB, BTN_L),
    BIND(ACTION_UI_NEXT_TAB, BTN_R),
    BIND(ACTION_UI_START, BTN_START),
};

static const ActionBinding debug_defaults[] = {
    BIND(ACTION_DEBUG_OVERLAY, BTN_D_UP),
    BIND(ACTION_DEBUG_DUMP, BTN_D_DOWN),
};

static const char *const engine_names[ACTION_ENGINE_COUNT] = {
    [ACTION_UI_UP]         = "Up",
    [ACTION_UI_DOWN]       = "Down",
    [ACTION_UI_LEFT]       = "Left",
    [ACTION_UI_RIGHT]      = "Right",
    [ACTION_UI_CONFIRM]    = "Confirm",
    [ACTION_UI_CANCEL]     = "Cancel",
    [ACTION_UI_PREV_TAB]   = "Prev Tab",
    [ACTION_UI_NEXT_TAB]   = "Next Tab",
    [ACTION_UI_START]      = "Start",
    [ACTION_DEBUG_OVERLAY] = "Overlay",
    [ACTION_DEBUG_DUMP]    = "Dump",
};

static const StickConfig stick_default = { .deadzone = 0.10f, .outer = 0.0f, .curve = 1.0f, .axial = false };

static bool valid_player(int p) { return p >= 0 && p < ACTION_PLAYERS; }

// ============================================================
// Contexts
// ============================================================

void action_context_init(ActionContext *ctx, const char *name, int priority, unsigned flags,
                         const ActionBinding *bindings, int count) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->name = name;
    ctx->priority = (int8_t)(priority > 127 ? 127 : priority < -128 ? -128 : priority);
    ctx->flags = (uint8_t)flags;
    for (int i = 0; i < count; i++) action_context_add(ctx, bindings[i]);
}

bool action_context_add(ActionContext *ctx, ActionBinding binding) {
    if (ctx->count >= ACTION_MAX_BINDINGS) return false;
    if (binding.sign == 0) binding.sign = 1;
    ctx->bindings[ctx->count++] = binding;
    return true;
}

void action_context_remove(ActionContext *ctx, ActionId action) {
    int n = 0;
    for (int i = 0; i < ctx->count; i++)
        if (ctx->bindings[i].action != action) ctx->bindings[n++] = ctx->bindings[i];
    ctx->count = (uint8_t)n;
}

// Index of an action's n-th button binding, or -1
static int nth_button(const ActionContext *ctx, ActionId action, int n) {
    for (int i = 0; i < ctx->count; i++) {
        const ActionBinding *b = &ctx->bindings[i];
        if (b->action == action && b->source == SRC_BUTTON && n-- == 0) return i;
    }
    return -1;
}

PadButton action_context_button(const ActionContext *ctx, ActionId action, int n) {
    int i = nth_button(ctx, action, n);
    return i >= 0 ? (PadButton)ctx->bindings[i].code : BTN_NONE;
}

bool action_context_set_button(ActionContext *ctx, ActionId action, int n, PadButton btn) {
    if (btn < 0 || btn >= BTN_COUNT) return false;
    int i = nth_button(ctx, action, n);
    if (i >= 0) {
        ctx->bindings[i].code = (uint8_t)btn;
        return true;
    }
    return action_context_add(ctx, (ActionBinding)BIND(action, btn));
}

// ============================================================
// Players
// ============================================================

void action_init(void) {
    memset(players, 0, sizeof(players));
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        players[p].port = (int8_t)p;
        players[p].stick = stick_default;
    }
    action_context_init(&action_ctx_ui, "UI", 100, CTX_CONSUME | CTX_MODAL,
                        ui_defaults, ARRAY_LEN(ui_defaults));
    action_context_init(&action_ctx_debug, "Debug", -100, CTX_CONSUME,
                        debug_defaults, ARRAY_LEN(debug_defaults));
    memset(names, 0, sizeof(names));
    repeat_delay = 0.30f;
    repeat_interval = 0.10f;
}

void action_set_port(int player, int port) {
    if (!valid_player(player)) return;
    players[player].port = (int8_t)(port >= 0 && port < PAD_PORTS ? port : -1);
}

int action_port(int player) {
    return valid_player(player) ? players[player].port : -1;
}

bool action_connected(int player) {
    return valid_player(player) && players[player].connected;
}

bool action_push_context(int player, ActionContext *ctx) {
    if (!valid_player(player) || !ctx) return false;
    if (action_has_context(player, ctx)) return true;
    Player *pl = &players[player];
    if (pl->ctx_count >= ACTION_MAX_CONTEXTS) return false;
    // Before the first context of the same or a lower priority
    int i = 0;
    while (i < pl->ctx_count && pl->ctx[i]->priority > ctx->priority) i++;
    memmove(&pl->ctx[i + 1], &pl->ctx[i], (size_t)(pl->ctx_count - i) * sizeof(pl->ctx[0]));
    pl->ctx[i] = ctx;
    pl->ctx_count++;
    return true;
}

void action_pop_context(int player, const ActionContext *ctx) {
    if (!valid_player(player)) return;
    Player *pl = &players[player];
    for (int i = 0; i < pl->ctx_count; i++) {
        if (pl->ctx[i] != ctx) continue;
        memmove(&pl->ctx[i], &pl->ctx[i + 1], (size_t)(pl->ctx_count - i - 1) * sizeof(pl->ctx[0]));
        pl->ctx_count--;
        return;
    }
}

void action_clear_contexts(int player) {
    if (valid_player(player)) players[player].ctx_count = 0;
}

bool action_has_context(int player, const ActionContext *ctx) {
    if (!valid_player(player)) return false;
    for (int i = 0; i < players[player].ctx_count; i++)
        if (players[player].ctx[i] == ctx) return true;
    return false;
}

int action_context_count(int player) {
    return valid_player(player) ? players[player].ctx_count : 0;
}

const ActionContext *action_context_at(int player, int i) {
    if (!valid_player(player) || i < 0 || i >= players[player].ctx_count) return NULL;
    return players[player].ctx[i];
}

// ============================================================
// Analog processing
// ============================================================

// A magnitude past the deadzone, rescaled to 0..1 and shaped by the curve
static float shape(float m, const StickConfig *c) {
    if (m <= c->deadzone) return 0.0f;
    float s = (m - c->deadzone) / (1.0f - c->deadzone);
    if (s > 1.0f) s = 1.0f;
    if (c->curve != 1.0f) s = powf(s, c->curve);
    return s;
}

// A stick (-1..1 per axis, before the deadzone) to its processed value: the
// direction kept, the magnitude shaped, at most 1 (an octagonal gate's
// corners reach about 1.2 before it)
static void process_2d(float x, float y, const StickConfig *c, float *ox, float *oy) {
    if (x * x + y * y <= c->deadzone * c->deadzone) {     // centred: the usual case, no sqrt
        *ox = *oy = 0.0f;
        return;
    }
    if (c->axial) {
        *ox = copysignf(shape(fabsf(x), c), x);
        *oy = copysignf(shape(fabsf(y), c), y);
        return;
    }
    float m = sqrtf(x * x + y * y);
    float s = shape(m, c);
    *ox = s > 0.0f ? x / m * s : 0.0f;
    *oy = s > 0.0f ? y / m * s : 0.0f;
}

// The processed axes; returns a bit per axis that is off centre now or was
// at the last update (the only ones a binding can see change)
static uint8_t process_axes(Player *pl, const PadState *pad) {
    const StickConfig *c = &pl->stick;
    float inv = 1.0f / (c->outer > 0.0f ? c->outer : (pad->style == PAD_GCN ? 90.0f : 80.0f));
    memcpy(pl->axis_prev, pl->axis, sizeof(pl->axis));
    process_2d(pad->stick_x * inv, pad->stick_y * inv, c,
               &pl->axis[AXIS_STICK_X], &pl->axis[AXIS_STICK_Y]);
    process_2d(pad->cstick_x * (1.0f / CSTICK_RANGE), pad->cstick_y * (1.0f / CSTICK_RANGE), c,
               &pl->axis[AXIS_CSTICK_X], &pl->axis[AXIS_CSTICK_Y]);
    pl->axis[AXIS_TRIGGER_L] = shape(pad->trigger_l * (1.0f / TRIGGER_RANGE), c);
    pl->axis[AXIS_TRIGGER_R] = shape(pad->trigger_r * (1.0f / TRIGGER_RANGE), c);
    uint8_t live = 0;
    for (int a = 0; a < AXIS_COUNT; a++)
        if (pl->axis[a] != 0.0f || pl->axis_prev[a] != 0.0f) live |= (uint8_t)(1u << a);
    return live;
}

// The main stick as one of four directions (the dominant axis): on past
// STICK_DIR_ON, kept until it falls under STICK_DIR_OFF
static uint8_t stick_dirs(float x, float y, uint8_t cur) {
    float ax = fabsf(x), ay = fabsf(y);
    int dom = ay > ax ? (y > 0.0f ? STICK_UP : STICK_DOWN) : (x > 0.0f ? STICK_RIGHT : STICK_LEFT);
    uint8_t bit = (uint8_t)(1u << dom);
    float need = (cur & bit) ? STICK_DIR_OFF : STICK_DIR_ON;
    return (ay > ax ? ay : ax) > need ? bit : 0;
}

// ============================================================
// Update
// ============================================================

// Every context from the top down. A binding is active when its input is
// down or changed this update; inside a context, an active chord takes its
// trigger button from the context's plain bindings (Z+A does not also fire
// A); below a consuming context, the inputs its active bindings used are
// gone, and below a modal one everything is.
static void evaluate(const Player *pl, const PadState *pad, uint8_t dir_pr, uint8_t dir_rl, Signals *sg) {
    uint16_t av_held = pad->held, av_pr = pad->pressed, av_rl = pad->released;
    uint8_t  av_dirs = 0x0F;
    uint8_t  av_axes = (uint8_t)((1u << AXIS_COUNT) - 1);

    for (int c = 0; c < pl->ctx_count; c++) {
        const ActionContext *ctx = pl->ctx[c];
        uint16_t used_btn = 0, chord_btn = 0;
        uint8_t  used_dirs = 0, used_axes = 0;

        for (int pass = 0; pass < 2; pass++) {           // chords first, then the rest
            for (int i = 0; i < ctx->count; i++) {
                const ActionBinding *b = &ctx->bindings[i];
                if ((b->modifiers != 0) != (pass == 0) || b->action >= ACTION_MAX) continue;
                bool down = false, pr = false, rl = false;
                float v = 0.0f;
                switch (b->source) {
                case SRC_BUTTON: {
                    if (b->code >= BTN_COUNT) continue;
                    uint16_t bit = PAD_BIT(b->code);
                    if ((chord_btn & bit) || (av_held & b->modifiers) != b->modifiers) continue;
                    down = av_held & bit;
                    pr = av_pr & bit;
                    rl = av_rl & bit;
                    if (!(down || pr || rl)) continue;
                    used_btn |= bit | b->modifiers;
                    if (b->modifiers && (down || pr)) chord_btn |= bit;
                    v = b->sign;
                    break;
                }
                case SRC_STICK: {
                    uint8_t d = (uint8_t)(1u << (b->code & 3));
                    if (b->code > STICK_RIGHT || !(av_dirs & d)) continue;
                    down = pl->dirs & d;
                    pr = dir_pr & d;
                    rl = dir_rl & d;
                    if (!(down || pr || rl)) continue;
                    used_dirs |= d;
                    v = b->sign;
                    break;
                }
                case SRC_AXIS: {
                    if (b->code >= AXIS_COUNT || !(av_axes & (1u << b->code))) continue;
                    float now = pl->axis[b->code], before = pl->axis_prev[b->code];
                    down = now != 0.0f;
                    pr = down && before == 0.0f;
                    rl = !down && before != 0.0f;
                    if (!(down || rl)) continue;
                    used_axes |= (uint8_t)(1u << b->code);
                    v = now * b->sign;
                    break;
                }
                default:
                    continue;
                }
                uint64_t abit = 1ull << b->action;
                if (down) {
                    if (!(sg->held & abit)) {
                        sg->held |= abit;
                        sg->value[b->action] = v;
                    } else if (fabsf(v) > fabsf(sg->value[b->action])) {
                        sg->value[b->action] = v;
                    }
                }
                if (pr) sg->pressed |= abit;
                if (rl) sg->released |= abit;
            }
        }

        if (ctx->flags & CTX_MODAL) return;
        if (ctx->flags & CTX_CONSUME) {
            av_held &= (uint16_t)~used_btn;
            av_pr   &= (uint16_t)~used_btn;
            av_rl   &= (uint16_t)~used_btn;
            av_dirs &= (uint8_t)~used_dirs;
            av_axes &= (uint8_t)~used_axes;
        }
    }
}

// Signals to action state: edges, then held time, repeats and values for
// the actions held or pressed (the rest are idle and read 0)
static void finalize(Player *pl, const Signals *sg, float dt) {
    uint64_t was = pl->held, now = sg->held, pr = sg->pressed;
    uint64_t rep = 0;
    uint64_t m = now | pr;
    for (int a = 0; m; a++, m >>= 1) {
        if (!(m & 1)) continue;
        uint64_t bit = 1ull << a;
        if (pr & bit) {
            pl->held_time[a] = 0.0f;
            pl->repeat_in[a] = repeat_delay;
            rep |= bit;
        } else {
            if (!(was & bit)) pl->repeat_in[a] = REPEAT_DISARMED;
            pl->held_time[a] += dt;
            pl->repeat_in[a] -= dt;
            if (pl->repeat_in[a] <= 0.0f) {
                rep |= bit;
                pl->repeat_in[a] += repeat_interval;
                if (pl->repeat_in[a] <= 0.0f) pl->repeat_in[a] = repeat_interval;   // a long frame: one repeat
            }
        }
        pl->value[a] = (now & bit) ? sg->value[a] : 0.0f;
    }
    pl->released = (was & ~now) | (pr & ~now);     // let go, or a tap between updates
    pl->pressed = pr;
    pl->held = now;
    pl->repeat = rep;
}

void action_update(const PadState pads[PAD_PORTS], float dt) {
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        Player *pl = &players[p];
        const PadState *pad = (pads && pl->port >= 0) ? &pads[pl->port] : &empty_pad;
        pl->connected = pad->style != PAD_NONE;

        uint8_t live_axes = process_axes(pl, pad);
        uint8_t prev = pl->dirs;
        pl->dirs = stick_dirs(pl->axis[AXIS_STICK_X], pl->axis[AXIS_STICK_Y], prev);

        Signals sg ENGINE_NOINIT;             // value[] is written before it is read
        sg.held = sg.pressed = sg.released = 0;
        // Nothing down, changed or off centre: no binding can be active
        if ((pad->held | pad->pressed | pad->released) || live_axes || pl->dirs || prev)
            evaluate(pl, pad, (uint8_t)(pl->dirs & ~prev), (uint8_t)(prev & ~pl->dirs), &sg);
        finalize(pl, &sg, dt);
    }
}

// ============================================================
// Queries
// ============================================================

static bool bit_of(uint64_t mask, ActionId a) {
    return a < ACTION_MAX && ((mask >> a) & 1);
}

bool action_pressed(int player, ActionId a)  { return valid_player(player) && bit_of(players[player].pressed, a); }
bool action_held(int player, ActionId a)     { return valid_player(player) && bit_of(players[player].held, a); }
bool action_released(int player, ActionId a) { return valid_player(player) && bit_of(players[player].released, a); }
bool action_repeat(int player, ActionId a)   { return valid_player(player) && bit_of(players[player].repeat, a); }

float action_value(int player, ActionId a) {
    return action_held(player, a) ? players[player].value[a] : 0.0f;
}

float action_held_time(int player, ActionId a) {
    return action_held(player, a) ? players[player].held_time[a] : 0.0f;
}

bool action_chord_pressed(int player, ActionId a, ActionId b) {
    return action_held(player, a) && action_held(player, b) &&
           (action_pressed(player, a) || action_pressed(player, b));
}

bool action_any_pressed(ActionId a, int *player) {
    for (int p = 0; p < ACTION_PLAYERS; p++) {
        if (!action_pressed(p, a)) continue;
        if (player) *player = p;
        return true;
    }
    return false;
}

UiInput action_ui(int player) {
    return (UiInput){
        .up       = action_repeat(player, ACTION_UI_UP),
        .down     = action_repeat(player, ACTION_UI_DOWN),
        .left     = action_repeat(player, ACTION_UI_LEFT),
        .right    = action_repeat(player, ACTION_UI_RIGHT),
        .confirm  = action_pressed(player, ACTION_UI_CONFIRM),
        .cancel   = action_pressed(player, ACTION_UI_CANCEL),
        .prev_tab = action_pressed(player, ACTION_UI_PREV_TAB),
        .next_tab = action_pressed(player, ACTION_UI_NEXT_TAB),
        .start    = action_pressed(player, ACTION_UI_START),
    };
}

void action_set_repeat(float delay_s, float interval_s) {
    repeat_delay = delay_s > 0.0f ? delay_s : 0.0f;
    repeat_interval = interval_s > 0.01f ? interval_s : 0.01f;
}

// ============================================================
// Analog configuration
// ============================================================

void action_set_stick(int player, const StickConfig *cfg) {
    if (!valid_player(player) || !cfg) return;
    StickConfig c = *cfg;
    if (c.deadzone < 0.0f) c.deadzone = 0.0f;
    if (c.deadzone > 0.95f) c.deadzone = 0.95f;
    if (c.outer < 0.0f) c.outer = 0.0f;
    if (c.curve <= 0.0f) c.curve = 1.0f;
    players[player].stick = c;
}

const StickConfig *action_stick(int player) {
    return valid_player(player) ? &players[player].stick : &stick_default;
}

float action_axis(int player, PadAxis axis) {
    if (!valid_player(player) || axis < 0 || axis >= AXIS_COUNT) return 0.0f;
    return players[player].axis[axis];
}

// ============================================================
// Names
// ============================================================

void action_set_names(ActionId first, const char *const *list, int count) {
    for (int i = 0; i < count && first + i < ACTION_MAX; i++) names[first + i] = list[i];
}

const char *action_name(ActionId a) {
    if (a < ACTION_ENGINE_COUNT) return engine_names[a];
    if (a < ACTION_MAX && names[a]) return names[a];
    return "?";
}
