#ifndef PAD_H
#define PAD_H

/*
 * Controllers as the engine sees them: one PadState per port, a snapshot the
 * input core (input.h) takes once per frame. Pure C, no libdragon: the
 * action layer (action.h) and the host tests use it as is. docs/INPUT.md.
 */

#include <stdint.h>
#include <stdbool.h>

#define PAD_PORTS 4

// Buttons, as bit numbers in the PadState masks (PAD_BIT). N64 and GameCube
// controllers share them (libdragon maps both); X and Y exist only on a
// GameCube controller.
typedef enum {
    BTN_A, BTN_B, BTN_Z, BTN_START,
    BTN_D_UP, BTN_D_DOWN, BTN_D_LEFT, BTN_D_RIGHT,
    BTN_L, BTN_R,
    BTN_C_UP, BTN_C_DOWN, BTN_C_LEFT, BTN_C_RIGHT,
    BTN_X, BTN_Y,
    BTN_COUNT,
    BTN_NONE = -1
} PadButton;

#define PAD_BIT(btn) ((uint16_t)(1u << (btn)))

typedef enum { PAD_NONE, PAD_N64, PAD_GCN, PAD_MOUSE } PadStyle;

// What sits in an N64 controller's accessory slot
typedef enum {
    PAD_ACC_NONE, PAD_ACC_UNKNOWN, PAD_ACC_CONTROLLER_PAK, PAD_ACC_RUMBLE_PAK,
    PAD_ACC_TRANSFER_PAK, PAD_ACC_BIO_SENSOR, PAD_ACC_SNAP_STATION,
} PadAccessory;

typedef struct {
    PadStyle     style;              // PAD_NONE: nothing plugged into the port
    PadAccessory accessory;
    bool         rumble;             // has a motor (Rumble Pak, GameCube controller)
    bool         connected;          // plugged in since the last frame
    bool         disconnected;       // unplugged since the last frame
    uint16_t     held;               // buttons down at the newest read (PAD_BIT masks)
    uint16_t     pressed;            // went down since the last frame, taps between two frames too
    uint16_t     released;           // went up since the last frame
    int8_t       stick_x, stick_y;   // raw: an N64 stick reaches about +-80, a GameCube one +-100
    int8_t       cstick_x, cstick_y; // GameCube C-stick (N64: +-76 from the C buttons)
    uint8_t      trigger_l, trigger_r; // GameCube analog triggers 0-200 (N64: 0 or 200 from L, R)
} PadState;

const char *pad_button_name(PadButton btn);   // "A", "C-Up", ...; "?" if out of range
const char *pad_style_name(PadStyle style);   // "N64", "GCN", "Mouse", "--"

#endif
