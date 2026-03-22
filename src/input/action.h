#ifndef ACTION_H
#define ACTION_H

#include <libdragon.h>
#include <stdbool.h>

// ============================================================
// Physical buttons — order matches Controls menu option indices
// ============================================================

typedef enum {
    BTN_A, BTN_B, BTN_Z, BTN_L, BTN_R,
    BTN_D_UP, BTN_D_DOWN, BTN_D_LEFT, BTN_D_RIGHT,
    BTN_C_UP, BTN_C_DOWN, BTN_C_LEFT, BTN_C_RIGHT,
    BTN_COUNT,
    BTN_NONE = -1
} PhysicalButton;

// ============================================================
// Game actions — remappable via Controls menu or context switch
// ============================================================

typedef enum {
    ACTION_CONFIRM,          // A — enter/cycle/interact
    ACTION_CANCEL,           // B — back/exit/secondary action
    ACTION_SELECT_MODE,      // Z — toggle object selection
    ACTION_CAM_MODE_NEXT,    // R — cycle camera forward
    ACTION_CAM_MODE_PREV,    // L — cycle camera backward
    ACTION_CYCLE_NEXT,       // D-Right — cycle objects/items
    ACTION_CYCLE_PREV,       // D-Left  — cycle objects/items
    ACTION_ZOOM_IN,          // C-Up    — zoom in / move Y (held)
    ACTION_ZOOM_OUT,         // C-Down  — zoom out / move Y (held)
    ACTION_SHIFT_UP,         // C-Right — shift view up (held)
    ACTION_SHIFT_DOWN,       // C-Left  — shift view down (held)
    ACTION_COUNT
} GameAction;

// ============================================================
// Action context — a named set of bindings for a game mode
// ============================================================

typedef struct {
    const char *name;                        // "Exploration", "Combat", etc.
    PhysicalButton bindings[ACTION_COUNT];   // Which button triggers each action
    float analog_deadzone;                   // Raw stick threshold (default 8)
    float analog_sensitivity;                // Stick-to-radians scale (default 0.002)
} ActionContext;

// Built-in contexts
extern const ActionContext ACTION_CTX_EXPLORATION;

// ============================================================
// Lifecycle
// ============================================================

void action_init(void);    // Calls joypad_init(), sets default context
void action_update(void);  // Calls joypad_poll(), maps buttons to actions

// ============================================================
// Query action states
// ============================================================

bool  action_pressed(GameAction action);   // Just pressed this frame
bool  action_held(GameAction action);      // Currently held down
bool  action_released(GameAction action);  // Just released this frame

// ============================================================
// Analog stick (filtered by active context deadzone/sensitivity)
// ============================================================

float action_analog_x(void);    // Horizontal (-left, +right)
float action_analog_y(void);    // Vertical (-down, +up)
bool  action_has_analog(void);  // True if stick past deadzone

// ============================================================
// Context management
// ============================================================

void action_set_context(const ActionContext *ctx);  // Switch context (copies bindings)
const char *action_context_name(void);              // Name of active context

// ============================================================
// Remapping (for Controls menu)
// ============================================================

void action_set_binding(GameAction action, PhysicalButton button);
PhysicalButton action_get_binding(GameAction action);

// ============================================================
// Utility
// ============================================================

const char *action_button_name(PhysicalButton btn);  // "A", "C-Up", etc.
const char *action_name(GameAction action);           // "Confirm", "Cancel", etc.

#endif
