#include "libdragon.h"

// The shim is declarations and macros only (libdragon.h). Until S9 this file
// backed a fake joypad for the action layer; the action layer now takes
// PadState snapshots, which the tests fill in directly (test_action.c).
