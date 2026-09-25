#ifndef UI_INPUT_H
#define UI_INPUT_H

// One frame of UI input: what menus, text boxes and dialogs read, however
// the game maps its controls. action_ui() fills it from the engine's UI
// actions (src/input/action.h); scripted input and tests fill it by hand.

#include <stdbool.h>

typedef struct {
    bool up, down, left, right;     // move: on the press, then repeating while held
    bool confirm;                   // A: apply, advance, pick
    bool cancel;                    // B: back, revert, show the whole page
    bool prev_tab, next_tab;        // L / R
    bool start;
} UiInput;

#endif
