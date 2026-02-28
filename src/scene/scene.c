#include "scene.h"
#include "../render/texture.h"
#include <string.h>

// --- Scene lifecycle ---

void scene_init(Scene *scene) {
    // Initialize collision world
    collision_world_init(&scene->collision);

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

    // Update camera
    camera_update(&scene->camera);

    // Run collision tests
    collision_test_all(&scene->collision);
}

void scene_draw(Scene *scene) {
    if (!scene->loaded) return;

    // Clear background
    rdpq_clear(scene->bg_color);
    rdpq_clear_z(ZBUF_MAX);

    // Scene-level draw (main rendering)
    if (scene->on_draw) {
        scene->on_draw(scene);
    }

    // Draw objects with per-object callbacks
    for (int i = 0; i < scene->object_count; i++) {
        SceneObject *obj = &scene->objects[i];
        if (obj->visible && obj->on_draw) {
            obj->on_draw(obj, &scene->camera, &scene->lighting);
        }
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

// --- Object management ---

int scene_add_object(Scene *scene, const SceneObject *obj) {
    if (scene->object_count >= SCENE_MAX_OBJECTS) return -1;
    int idx = scene->object_count;
    scene->objects[idx] = *obj;
    scene->object_count++;
    return idx;
}

void scene_remove_object(Scene *scene, int index) {
    if (index < 0 || index >= scene->object_count) return;
    // Shift remaining objects down
    for (int i = index; i < scene->object_count - 1; i++) {
        scene->objects[i] = scene->objects[i + 1];
    }
    scene->object_count--;
}

SceneObject *scene_get_object(Scene *scene, int index) {
    if (index < 0 || index >= scene->object_count) return NULL;
    return &scene->objects[index];
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
            float v0[] = {  0.0f,   0.0f};
            float v1[] = {320.0f,   0.0f};
            float v2[] = {320.0f, 240.0f};
            float v3[] = {  0.0f, 240.0f};
            rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
            rdpq_triangle(&TRIFMT_FILL, v0, v2, v3);
        }
    }
}

bool scene_manager_is_transitioning(const SceneManager *mgr) {
    return mgr->transitioning;
}

Scene *scene_manager_current(const SceneManager *mgr) {
    return mgr->current;
}
