#include "libdragon.h"
#include <string.h>

static joypad_buttons_t s_pressed, s_held, s_released;
static joypad_inputs_t  s_inputs;
static joypad_buttons_t n_pressed, n_held, n_released;
static int n_sx, n_sy;

void shim_joypad_set(joypad_buttons_t pressed, joypad_buttons_t held,
                     joypad_buttons_t released, int stick_x, int stick_y) {
    n_pressed = pressed; n_held = held; n_released = released;
    n_sx = stick_x; n_sy = stick_y;
}

void joypad_init(void) {
    memset(&s_pressed, 0, sizeof(s_pressed));
    memset(&s_held, 0, sizeof(s_held));
    memset(&s_released, 0, sizeof(s_released));
    memset(&s_inputs, 0, sizeof(s_inputs));
    memset(&n_pressed, 0, sizeof(n_pressed));
    memset(&n_held, 0, sizeof(n_held));
    memset(&n_released, 0, sizeof(n_released));
    n_sx = n_sy = 0;
}

void joypad_poll(void) {
    s_pressed = n_pressed; s_held = n_held; s_released = n_released;
    s_inputs.btn = n_held;
    s_inputs.stick_x = (int8_t)n_sx;
    s_inputs.stick_y = (int8_t)n_sy;
}

joypad_buttons_t joypad_get_buttons_pressed(joypad_port_t port)  { (void)port; return s_pressed; }
joypad_buttons_t joypad_get_buttons_held(joypad_port_t port)     { (void)port; return s_held; }
joypad_buttons_t joypad_get_buttons_released(joypad_port_t port) { (void)port; return s_released; }
joypad_inputs_t  joypad_get_inputs(joypad_port_t port)           { (void)port; return s_inputs; }
