#include "test.h"
#include "input/action.h"
#include <string.h>

static joypad_buttons_t none(void) {
    joypad_buttons_t b;
    memset(&b, 0, sizeof(b));
    return b;
}

static void test_default_binding(void) {
    action_init();
    joypad_buttons_t a = none();
    a.a = 1;
    shim_joypad_set(a, a, none(), 0, 0);
    action_update();
    CHECK(action_pressed(ACTION_CONFIRM));
    CHECK(action_held(ACTION_CONFIRM));
    CHECK(!action_pressed(ACTION_CANCEL));
}

static void test_rebinding(void) {
    action_init();
    action_set_binding(ACTION_CONFIRM, BTN_B);
    CHECK(action_get_binding(ACTION_CONFIRM) == BTN_B);

    joypad_buttons_t b = none();
    b.b = 1;
    shim_joypad_set(b, b, none(), 0, 0);
    action_update();
    CHECK(action_pressed(ACTION_CONFIRM));

    joypad_buttons_t a = none();
    a.a = 1;
    shim_joypad_set(a, a, none(), 0, 0);
    action_update();
    CHECK(!action_pressed(ACTION_CONFIRM));             // A no longer confirms
}

static void test_analog_deadzone(void) {
    action_init();
    shim_joypad_set(none(), none(), none(), 3, -4);     // inside the 8-unit deadzone
    action_update();
    CHECK(!action_has_analog());
    shim_joypad_set(none(), none(), none(), 60, 0);
    action_update();
    CHECK(action_has_analog());
    CHECK(action_analog_x() < 0.0f);                     // X is inverted (see action.h)
}

void run_action_tests(void) {
    RUN_TEST(test_default_binding);
    RUN_TEST(test_rebinding);
    RUN_TEST(test_analog_deadzone);
}
