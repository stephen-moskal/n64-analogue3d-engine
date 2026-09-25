#include "pad.h"

static const char *const button_names[BTN_COUNT] = {
    [BTN_A]       = "A",
    [BTN_B]       = "B",
    [BTN_Z]       = "Z",
    [BTN_START]   = "Start",
    [BTN_D_UP]    = "D-Up",
    [BTN_D_DOWN]  = "D-Down",
    [BTN_D_LEFT]  = "D-Left",
    [BTN_D_RIGHT] = "D-Right",
    [BTN_L]       = "L",
    [BTN_R]       = "R",
    [BTN_C_UP]    = "C-Up",
    [BTN_C_DOWN]  = "C-Down",
    [BTN_C_LEFT]  = "C-Left",
    [BTN_C_RIGHT] = "C-Right",
    [BTN_X]       = "X",
    [BTN_Y]       = "Y",
};

const char *pad_button_name(PadButton btn) {
    if (btn < 0 || btn >= BTN_COUNT) return "?";
    return button_names[btn];
}

const char *pad_style_name(PadStyle style) {
    switch (style) {
    case PAD_N64:   return "N64";
    case PAD_GCN:   return "GCN";
    case PAD_MOUSE: return "Mouse";
    default:        return "--";
    }
}
