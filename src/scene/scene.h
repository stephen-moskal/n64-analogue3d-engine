#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <libdragon.h>
#include "../math/vec3.h"
#include "../render/camera.h"
#include "../render/lighting.h"
#include "../collision/collision.h"

// --- Limits ---

#define SCENE_MAX_OBJECTS   32
#define SCENE_MAX_TEXTURES  16

// --- Scene Object ---

typedef struct SceneObject {
    // Transform
    vec3_t position;
    vec3_t rotation;      // Euler angles (radians)
    vec3_t scale;

    // State flags
    bool active;          // Participates in update
    bool visible;         // Participates in draw

    // Collision integration
    int collider_handle;  // Handle in scene's CollisionWorld (-1 = none)

    // Object behavior (optional per-object callbacks)
    void *data;           // Type-specific data pointer
    void (*on_update)(struct SceneObject *obj, float dt);
    void (*on_draw)(struct SceneObject *obj, const Camera *cam, const LightConfig *light);
} SceneObject;

// --- Scene ---

typedef struct Scene {
    const char *name;

    // Objects
    SceneObject objects[SCENE_MAX_OBJECTS];
    int object_count;

    // Subsystems owned by this scene
    Camera camera;
    LightConfig lighting;
    CollisionWorld collision;

    // Per-scene texture management
    const char *texture_paths[SCENE_MAX_TEXTURES];
    int texture_slots[SCENE_MAX_TEXTURES];  // Which slot each texture maps to
    int texture_count;

    // World positioning (for shared-coordinate scenes)
    vec3_t world_offset;  // (0,0,0) for independent scenes

    // Background color
    color_t bg_color;

    // Scene lifecycle callbacks
    void (*on_init)(struct Scene *scene);
    void (*on_update)(struct Scene *scene, float dt);
    void (*on_draw)(struct Scene *scene);
    void (*on_post_draw)(struct Scene *scene);  // After per-object draws (HUD, overlays)
    void (*on_cleanup)(struct Scene *scene);

    // State
    bool loaded;
} Scene;

// --- Transition types ---

typedef enum {
    TRANSITION_CUT,         // Instant switch
    TRANSITION_FADE_BLACK,  // Fade out to black -> switch -> fade in
    TRANSITION_FADE_WHITE,  // Fade out to white -> switch -> fade in
} TransitionType;

// --- Scene Manager ---

typedef struct {
    Scene *current;
    Scene *pending;            // Next scene (waiting for transition)

    // Transition state
    TransitionType transition_type;
    float transition_progress; // 0.0 -> 1.0
    float transition_speed;    // Progress per second
    int phase;                 // 0=fade-out, 1=switch, 2=fade-in
    bool transitioning;
} SceneManager;

// --- Scene lifecycle ---
void scene_init(Scene *scene);
void scene_update(Scene *scene, float dt);
void scene_draw(Scene *scene);
void scene_cleanup(Scene *scene);

// --- Object management ---
int  scene_add_object(Scene *scene, const SceneObject *obj);
void scene_remove_object(Scene *scene, int index);
SceneObject *scene_get_object(Scene *scene, int index);

// --- Scene manager ---
void  scene_manager_init(SceneManager *mgr);
void  scene_manager_switch(SceneManager *mgr, Scene *next,
                           TransitionType type, float speed);
void  scene_manager_update(SceneManager *mgr, float dt);
void  scene_manager_draw(SceneManager *mgr);
bool  scene_manager_is_transitioning(const SceneManager *mgr);
Scene *scene_manager_current(const SceneManager *mgr);

#endif
