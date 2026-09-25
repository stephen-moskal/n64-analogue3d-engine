#include "scene.h"
#include "../debug/stats.h"
#include "../debug/profiler.h"
#include "../render/texture.h"
#include "../render/mesh.h"
#include "../render/atmosphere.h"
#include "../engine/engine_config.h"
#include "../engine/frame_queue.h"
#include "../engine/hot.h"
#include <string.h>

// The camera and lighting the frame is drawn with (scene_view_camera/light),
// linked at a fixed D-cache colour by src/engine/hot_data.ld. mesh_draw reads
// the camera for every object and the lighting for every face group, the
// floor and shadows per vertex; inside a Scene both move whenever the struct
// or the static data before it changes size (D35: S7 moved them onto the
// render stack's lines, +7 % on every mesh step).
static Camera      view_camera;
static LightConfig view_light;

const Camera *scene_view_camera(void) { return &view_camera; }
const LightConfig *scene_view_light(void) { return &view_light; }

// Runs between every two mesh_draw calls, so it is pinned with the mesh phase
// (ENGINE_HOT_LOOP, hot_text.ld): unpinned it can share mesh_draw's I-cache
// lines (D35)
static ENGINE_HOT_LOOP void scene_draw_objects(Scene *scene) {
    for (int i = 0; i < scene->object_count; i++) {
        SceneObject *obj = &scene->objects[i];
        if (obj->visible && obj->on_draw) {
            obj->on_draw(obj, &view_camera, &view_light);
        }
    }
}

// --- Scene lifecycle ---

void scene_init(Scene *scene) {
    // No objects yet; empty collision and physics worlds
    scene_objects_init(scene);

    // The scene's meshes take their D-cache colours from the start (D26)
    mesh_placement_reset();

    // Initialize lighting
    lighting_init(&scene->lighting);

    // Load scene textures
    for (int i = 0; i < scene->texture_count; i++) {
        if (scene->texture_paths[i]) {
            texture_load_slot(scene->texture_slots[i], scene->texture_paths[i]);
        }
    }

    // Call scene-specific init (sets up objects, colliders, camera, etc.)
    if (scene->on_init) {
        scene->on_init(scene);
    }

    scene->loaded = true;
    debugf("Scene loaded: %s (%d objects)\n", scene->name, scene->object_count);
}

void scene_update(Scene *scene, float dt) {
    if (!scene->loaded) return;

    // Update objects with per-object callbacks
    for (int i = 0; i < scene->object_count; i++) {
        SceneObject *obj = &scene->objects[i];
        if (obj->active && obj->on_update) {
            obj->on_update(obj, dt);
        }
    }

    // Scene-level update (camera input, game logic, etc.)
    if (scene->on_update) {
        scene->on_update(scene, dt);
    }

    // Physics (fixed steps), then objects follow their bodies
    PROF_BEGIN(PROF_PHYSICS);
    if (scene->physics.body_count > 0) {
        physics_world_update(&scene->physics, dt);
    }
    scene_sync_bodies(scene);
    PROF_END(PROF_PHYSICS);

    // Camera (after the objects it may follow), colliders follow their
    // objects, then the pair tests
    PROF_BEGIN(PROF_SCENE_SYS);
    camera_update(&scene->camera);
    scene_sync_colliders(scene);
    collision_test_all(&scene->collision);
    STATS_SET(colliders, scene->collision.count);
    STATS_SET(collision_pairs, scene->collision.result_count);
    PROF_END(PROF_SCENE_SYS);
}

void scene_draw(Scene *scene) {
    if (!scene->loaded) return;

    // The frame's camera and lighting, at their pinned colour
    view_camera = scene->camera;
    view_light = scene->lighting;

    // Background: the sky when it covers the screen, otherwise a colour
    // clear (never both: the clear would be overdrawn; D14)
    if (sky_covers_screen()) {
        PROF_BEGIN(PROF_SKY);
        sky_draw();
        PROF_END(PROF_SKY);
    } else {
        rdpq_clear(scene->bg_color);
    }
    rdpq_clear_z(ZBUF_MAX);

    // Scene-level draw (main rendering)
    if (scene->on_draw) {
        scene->on_draw(scene);
    }

    // Draw objects with per-object callbacks
    PROF_BEGIN(PROF_OBJECTS);
    scene_draw_objects(scene);
    PROF_END(PROF_OBJECTS);

    // Post-draw: HUD, overlays, 2D elements (after all 3D geometry)
    if (scene->on_post_draw) {
        scene->on_post_draw(scene);
    }
}

void scene_cleanup(Scene *scene) {
    if (!scene->loaded) return;

    // Call scene-specific cleanup
    if (scene->on_cleanup) {
        scene->on_cleanup(scene);
    }

    // Free scene textures
    for (int i = 0; i < scene->texture_count; i++) {
        texture_free_slot(scene->texture_slots[i]);
    }

    scene->object_count = 0;
    scene->loaded = false;
    debugf("Scene unloaded: %s\n", scene->name);
}

// --- Scene Manager ---

void scene_manager_init(SceneManager *mgr) {
    memset(mgr, 0, sizeof(SceneManager));
}

void scene_manager_switch(SceneManager *mgr, Scene *next,
                          TransitionType type, float speed) {
    if (type == TRANSITION_CUT) {
        // Instant switch
        if (mgr->current && mgr->current->loaded) {
            scene_cleanup(mgr->current);
        }
        if (mgr->current != next) engine_frame_queue_release();   // the old scene's queue memory
        mgr->current = next;
        scene_init(next);
        mgr->transitioning = false;
    } else {
        // Start fade transition
        mgr->pending = next;
        mgr->transition_type = type;
        mgr->transition_speed = speed;
        mgr->transition_progress = 0.0f;
        mgr->phase = 0;  // fade-out
        mgr->transitioning = true;
    }
}

void scene_manager_update(SceneManager *mgr, float dt) {
    // Handle scene reset request (reusable soft-reset for any scene)
    if (mgr->current && mgr->current->reset_requested) {
        mgr->current->reset_requested = false;
        scene_cleanup(mgr->current);
        scene_init(mgr->current);
        return;  // Let init settle — update starts next frame
    }

    if (!mgr->transitioning) {
        // Normal update
        if (mgr->current) {
            scene_update(mgr->current, dt);
        }
        return;
    }

    // Transition in progress
    mgr->transition_progress += mgr->transition_speed * dt;

    if (mgr->phase == 0) {
        // Fade-out phase: still updating current scene
        if (mgr->current) {
            scene_update(mgr->current, dt);
        }
        if (mgr->transition_progress >= 1.0f) {
            // Fade-out complete — switch scenes
            if (mgr->current && mgr->current->loaded) {
                scene_cleanup(mgr->current);
            }
            if (mgr->current != mgr->pending) engine_frame_queue_release();
            mgr->current = mgr->pending;
            mgr->pending = NULL;
            scene_init(mgr->current);
            mgr->phase = 2;  // fade-in
            mgr->transition_progress = 0.0f;
        }
    } else if (mgr->phase == 2) {
        // Fade-in phase: updating new scene
        if (mgr->current) {
            scene_update(mgr->current, dt);
        }
        if (mgr->transition_progress >= 1.0f) {
            // Transition complete
            mgr->transitioning = false;
            mgr->transition_progress = 1.0f;
        }
    }
}

void scene_manager_draw(SceneManager *mgr) {
    // Draw current scene
    if (mgr->current) {
        scene_draw(mgr->current);
    }

    // Draw transition overlay
    if (mgr->transitioning) {
        // Compute alpha: fade-out = increasing, fade-in = decreasing
        float alpha_f;
        if (mgr->phase == 0) {
            alpha_f = mgr->transition_progress;
        } else {
            alpha_f = 1.0f - mgr->transition_progress;
        }
        if (alpha_f < 0.0f) alpha_f = 0.0f;
        if (alpha_f > 1.0f) alpha_f = 1.0f;
        uint8_t alpha = (uint8_t)(alpha_f * 255.0f);

        if (alpha > 0) {
            // Choose overlay color based on transition type
            color_t overlay;
            if (mgr->transition_type == TRANSITION_FADE_WHITE) {
                overlay = RGBA32(0xFF, 0xFF, 0xFF, alpha);
            } else {
                overlay = RGBA32(0x00, 0x00, 0x00, alpha);
            }

            // Draw fullscreen overlay using triangles in 1-cycle mode
            // (fill mode only works with rectangles on real hardware)
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_zbuf(false, false);
            rdpq_set_prim_color(overlay);

            // Fullscreen quad as two triangles
            // TRIFMT_FILL: {X, Y}
            const float w = ENGINE_SCREEN_W, h = ENGINE_SCREEN_H;
            float v0[] = {0.0f, 0.0f};
            float v1[] = {w,    0.0f};
            float v2[] = {w,    h};
            float v3[] = {0.0f, h};
            rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
            rdpq_triangle(&TRIFMT_FILL, v0, v2, v3);
            STATS_ADD(tris_ui, 2);
        }
    }
}

bool scene_manager_is_transitioning(const SceneManager *mgr) {
    return mgr->transitioning;
}

Scene *scene_manager_current(const SceneManager *mgr) {
    return mgr->current;
}
