#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <libdragon.h>
#include "../math/vec3.h"
#include "../render/camera.h"
#include "../render/lighting.h"
#include "../collision/collision.h"
#include "../physics/physics.h"

// --- Limits ---

#define SCENE_MAX_OBJECTS   32
#define SCENE_MAX_TEXTURES  16

// --- Scene Object ---

// SceneObject.flags: which passes and queries include the object
enum {
    SCENE_OBJ_CASTS_SHADOW = 1 << 0,  // drawn by the scene's shadow pass
    SCENE_OBJ_SELECTABLE   = 1 << 1,  // can be picked (the demo's object mode)
    SCENE_OBJ_FLAG_USER    = 1 << 8,  // first bit free for game-specific use
};

typedef struct SceneObject {
    // Transform
    vec3_t position;
    vec3_t rotation;      // Euler angles (radians)
    vec3_t scale;

    // State flags
    bool active;          // Participates in update
    bool visible;         // Participates in draw
    uint16_t flags;       // SCENE_OBJ_* bits

    // Collision and physics (scene_objects.c). The object owns what is
    // attached to it: scene_remove_object() removes both. Each scene update
    // moves the object to its body, then its collider to the object.
    int collider_handle;     // in the scene's CollisionWorld (-1 = none)
    int body_handle;         // in the scene's PhysicsWorld (-1 = none)
    vec3_t collider_offset;  // collider centre - position, set on attach

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
    PhysicsWorld physics;     // raycasts against `collision`

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
    bool reset_requested;  // Set true to trigger cleanup + reinit next frame
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

// The camera and lighting the current frame is drawn with: copies of the
// scene's, which scene_draw() makes before on_draw, at a pinned D-cache colour
// (src/engine/hot_data.ld). Draw code passes these to the renderers, never
// &scene->camera / &scene->lighting: the renderers read them per vertex,
// object and face group, and inside the Scene their colours move whenever
// the struct or the data before it changes size (D35). Object on_draw
// callbacks receive them as their cam and light arguments.
const Camera      *scene_view_camera(void);
const LightConfig *scene_view_light(void);

// --- Objects, colliders and bodies (scene_objects.c, host-tested) ---

// No objects; empty collision and physics worlds (first step of scene_init)
void scene_objects_init(Scene *scene);

// Copies the object and returns its index, or -1 if the scene is full. The
// copy starts without a collider or body (handles -1): attach them with
// scene_object_set_collider() / scene_object_set_body().
int  scene_add_object(Scene *scene, const SceneObject *obj);
// Removes the object's collider and body, then shifts later objects down
void scene_remove_object(Scene *scene, int index);
SceneObject *scene_get_object(Scene *scene, int index);

// Next object after `from` (step 1) or before it (step -1) whose flags
// include all of `flags`, wrapping around; from = -1 starts at the first
// (last) object. -1 if none matches.
int  scene_find_object(const Scene *scene, int from, int step, uint16_t flags);

// Attach a collider (a collision_add_* handle) to an object: the scene moves
// it with the object at its current offset, and removes it with the object.
// A collider attached before is removed; -1 only removes it.
void scene_object_set_collider(Scene *scene, int index, int collider);
// Attach a physics body (a physics_body_add handle): the object follows the
// body. A body attached before is removed; -1 only removes it.
void scene_object_set_body(Scene *scene, int index, int body);

// Per-frame sync, run by scene_update() after the physics step
void scene_sync_bodies(Scene *scene);     // object position = its body's
void scene_sync_colliders(Scene *scene);  // collider centre = position + offset

// --- Scene manager ---
void  scene_manager_init(SceneManager *mgr);
void  scene_manager_switch(SceneManager *mgr, Scene *next,
                           TransitionType type, float speed);
void  scene_manager_update(SceneManager *mgr, float dt);
void  scene_manager_draw(SceneManager *mgr);
bool  scene_manager_is_transitioning(const SceneManager *mgr);
Scene *scene_manager_current(const SceneManager *mgr);

#endif
