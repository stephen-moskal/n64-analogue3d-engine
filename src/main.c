#include <libdragon.h>

#include "cube.h"
#include "lighting.h"
#include "input.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

#define AUTO_ROTATE_SPEED 0.01f

static LightConfig light_config;

int main(void) {
    // Initialize debug output
    debug_init_isviewer();
    debug_init_usblog();

    // Initialize display (320x240, 16-bit color, triple buffered)
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Initialize RDP command queue
    rdpq_init();

    // Initialize subsystems
    input_init();
    lighting_init(&light_config);
    cube_init();

    debugf("Hello Cube - N64 Engine Demo\n");
    debugf("Use analog stick or D-pad to rotate\n");

    InputState input_state;

    // Main game loop
    while (1) {
        // Update input
        input_update(&input_state);

        // Update cube rotation
        if (input_state.has_input) {
            cube_update(input_state.rotation_x, input_state.rotation_y);
        } else {
            // Auto-rotate when no input
            cube_update(AUTO_ROTATE_SPEED * 0.5f, AUTO_ROTATE_SPEED);
        }

        // Begin frame - get framebuffer
        surface_t *fb = display_get();

        // Attach RDP to framebuffer (no auto-clear, we clear manually)
        rdpq_attach(fb, NULL);

        // Clear to dark blue background (fill mode works with rectangles)
        rdpq_set_mode_fill(RGBA32(0x10, 0x10, 0x30, 0xFF));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

        // Draw the cube
        cube_draw(&light_config);

        // End frame
        rdpq_detach_show();
    }

    // Cleanup (unreachable in normal operation)
    cube_cleanup();

    return 0;
}
