#include <libdragon.h>

#include "render/cube.h"
#include "render/lighting.h"
#include "render/texture.h"
#include "input/input.h"
#include "ui/text.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

#define AUTO_ROTATE_SPEED 0.01f

static LightConfig light_config;

static const TextBoxConfig title_text = {
    .x       = 20.0f,
    .y       = 20.0f,
    .width   = 280,
    .font_id = FONT_DEBUG_VAR,
    .color   = RGBA32(0xFF, 0xFF, 0xFF, 0xFF),
    .align   = ALIGN_CENTER,
};

static const TextBoxConfig fps_text = {
    .x       = 4.0f,
    .y       = 232.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0x00, 0xFF, 0x00, 0xFF),
};

static const TextBoxConfig stats_text = {
    .x       = 4.0f,
    .y       = 220.0f,
    .font_id = FONT_DEBUG_MONO,
    .color   = RGBA32(0xFF, 0xFF, 0x00, 0xFF),
};

int main(void) {
    // Initialize debug output
    debug_init_isviewer();
    debug_init_usblog();

    // Initialize display (320x240, 16-bit color, triple buffered)
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE);

    // Initialize RDP command queue
    rdpq_init();
    // rdpq_debug_start();  // Uncomment for RDP validation debugging

    // Initialize DFS (required before sprite_load)
    dfs_init(DFS_DEFAULT_LOCATION);

    // Initialize subsystems
    input_init();
    lighting_init(&light_config);
    texture_init();
    cube_init();
    text_init();

    debugf("Hello Cube - N64 Engine Demo (Textured)\n");
    debugf("Use analog stick or D-pad to rotate\n");

    InputState input_state;
    float fps = 0.0f;
    unsigned long last_ticks = 0;

    // Main game loop
    while (1) {
        // FPS calculation
        unsigned long now = TICKS_READ();
        if (last_ticks != 0) {
            float delta_s = (float)TICKS_DISTANCE(last_ticks, now) / (float)TICKS_PER_SECOND;
            if (delta_s > 0.001f) fps = 1.0f / delta_s;
        }
        last_ticks = now;

        // Reset per-frame texture stats
        texture_stats_reset();

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

        // Draw text overlays (after 3D geometry, before detach)
        text_draw(&title_text, "N64 Dev Engine");

        const TextureStats *ts = texture_stats_get();
        text_draw_fmt(&stats_text, "T:%d U:%d TMEM:%dB",
            ts->triangle_count, ts->upload_count, ts->tmem_bytes_used);
        text_draw_fmt(&fps_text, "FPS: %.0f", fps);

        // End frame
        rdpq_detach_show();
    }

    // Cleanup (unreachable in normal operation)
    text_cleanup();
    texture_cleanup();
    cube_cleanup();

    return 0;
}
