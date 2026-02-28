#include <libdragon.h>

#include "render/camera.h"
#include "render/cube.h"
#include "render/lighting.h"
#include "render/texture.h"
#include "input/input.h"
#include "ui/text.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FB_COUNT 3

static Camera camera;
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

    // Initialize camera with default config
    camera_init(&camera, &CAMERA_DEFAULT);

    // Allocate Z-buffer (16-bit depth, same resolution as framebuffer)
    surface_t zbuf = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);

    debugf("SMozN64 Dev Engine (Camera + Z-Buffer)\n");
    debugf("Analog stick: orbit | C-up/down: zoom | C-left/right: shift Y\n");

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

        // Apply input to camera
        if (input_state.has_input) {
            camera_orbit(&camera, input_state.orbit_azimuth,
                                  input_state.orbit_elevation);
            camera_zoom(&camera, input_state.zoom_delta);
            camera_shift_target_y(&camera, input_state.target_y_delta);
        }
        camera_update(&camera);

        // Auto-rotate cube
        cube_update();

        // Begin frame - get framebuffer
        surface_t *fb = display_get();

        // Attach RDP to both color and depth buffers
        rdpq_attach(fb, &zbuf);

        // Clear to dark blue background + reset Z-buffer
        rdpq_clear(RGBA32(0x10, 0x10, 0x30, 0xFF));
        rdpq_clear_z(ZBUF_MAX);

        // Draw the cube
        cube_draw(&camera, &light_config);

        // Draw text overlays (after 3D geometry, before detach)
        text_draw(&title_text, "SMozN64 Dev Engine");

        const TextureStats *ts = texture_stats_get();
        text_draw_fmt(&stats_text, "T:%d U:%d TMEM:%dB",
            ts->triangle_count, ts->upload_count, ts->tmem_bytes_used);
        text_draw_fmt(&fps_text, "FPS: %.0f", fps);

        // End frame
        rdpq_detach_show();
    }

    // Cleanup (unreachable in normal operation)
    surface_free(&zbuf);
    text_cleanup();
    texture_cleanup();
    cube_cleanup();

    return 0;
}
