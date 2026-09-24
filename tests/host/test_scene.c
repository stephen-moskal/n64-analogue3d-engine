#include "test.h"
#include "scene/scene.h"
#include <string.h>

// Scene objects and their attachments (src/scene/scene_objects.c): the
// scene owns the physics and collision worlds, objects own their collider
// and body, bodies move objects and objects move colliders (ROADMAP_v2 S7).

static Scene scene;   // ~11 KB: static storage

static SceneObject object_at(vec3_t pos, uint16_t flags) {
    return (SceneObject){
        .position = pos, .scale = {1, 1, 1},
        .active = true, .visible = true, .flags = flags,
    };
}

static int add_with_sphere(vec3_t pos, float radius) {
    SceneObject o = object_at(pos, 0);
    int idx = scene_add_object(&scene, &o);
    scene_object_set_collider(&scene, idx, collision_add_sphere(&scene.collision, pos, radius,
        COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL));
    return idx;
}

// A zero-initialised object owns nothing: scene_add_object clears the handles
// (otherwise it would own collider 0 and body 0 and remove them with itself)
static void test_add_clears_handles(void) {
    scene_objects_init(&scene);
    collision_add_sphere(&scene.collision, (vec3_t){0, 0, 0}, 1, 1, 1, NULL);
    physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, (vec3_t){0, 0, 0});
    SceneObject zero;
    memset(&zero, 0, sizeof(zero));
    int idx = scene_add_object(&scene, &zero);
    CHECK(idx == 0 && scene.object_count == 1);
    CHECK(scene.objects[0].collider_handle == -1 && scene.objects[0].body_handle == -1);
    scene_remove_object(&scene, idx);
    CHECK(scene.object_count == 0);
    CHECK(scene.collision.count == 1 && scene.physics.body_count == 1);   // still there
}

// Removing an object removes its collider and body; later objects shift
// down and keep theirs
static void test_remove_frees_attachments(void) {
    scene_objects_init(&scene);
    int a = add_with_sphere((vec3_t){0, 0, 0}, 10);
    int b = add_with_sphere((vec3_t){100, 0, 0}, 10);
    int c = add_with_sphere((vec3_t){200, 0, 0}, 10);
    int body = physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, (vec3_t){100, 0, 0});
    scene_object_set_body(&scene, b, body);
    CHECK(a == 0 && b == 1 && c == 2);
    CHECK(scene.collision.count == 3 && scene.physics.body_count == 1);
    CHECK(scene.objects[b].body_handle == body);
    int col_b = scene.objects[b].collider_handle;
    int col_c = scene.objects[c].collider_handle;

    scene_remove_object(&scene, b);
    CHECK(scene.object_count == 2);
    CHECK(scene.collision.count == 2 && !scene.collision.colliders[col_b].active);
    CHECK(scene.physics.body_count == 0 && physics_body_get(&scene.physics, body) == NULL);
    CHECK(scene.objects[1].position.x == 200.0f);          // c is now index 1
    CHECK(scene.objects[1].collider_handle == col_c && scene.collision.colliders[col_c].active);

    scene_remove_object(&scene, 5);                          // out of range: ignored
    scene_remove_object(&scene, -1);
    CHECK(scene.object_count == 2 && scene.collision.count == 2);
}

// Attaching again replaces (and removes) the previous collider or body;
// -1 removes it; handles that are not live are refused
static void test_set_replaces(void) {
    scene_objects_init(&scene);
    int idx = add_with_sphere((vec3_t){0, 0, 0}, 10);
    int first = scene.objects[idx].collider_handle;
    int second = collision_add_sphere(&scene.collision, (vec3_t){0, 5, 0}, 3, 1, 1, NULL);
    scene_object_set_collider(&scene, idx, second);
    CHECK(scene.objects[idx].collider_handle == second);
    CHECK(!scene.collision.colliders[first].active);
    CHECK(scene.objects[idx].collider_offset.y == 5.0f);    // measured on attach
    scene_object_set_collider(&scene, idx, second);          // the same one: kept
    CHECK(scene.collision.colliders[second].active);
    scene_object_set_collider(&scene, idx, -1);
    CHECK(scene.objects[idx].collider_handle == -1 && scene.collision.count == 0);
    scene_object_set_collider(&scene, idx, 40);              // no such collider
    CHECK(scene.objects[idx].collider_handle == -1);

    int b1 = physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, (vec3_t){0, 0, 0});
    int b2 = physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, (vec3_t){0, 0, 0});
    scene_object_set_body(&scene, idx, b1);
    scene_object_set_body(&scene, idx, b2);
    CHECK(scene.objects[idx].body_handle == b2 && physics_body_get(&scene.physics, b1) == NULL);
    scene_object_set_body(&scene, idx, 7);                   // no such body
    CHECK(scene.objects[idx].body_handle == -1 && scene.physics.body_count == 0);
}

// Colliders follow their objects at the offset they were attached with
static void test_collider_sync(void) {
    scene_objects_init(&scene);
    SceneObject o = object_at((vec3_t){10, 20, 30}, 0);
    int s = scene_add_object(&scene, &o);
    int sphere = collision_add_sphere(&scene.collision, (vec3_t){10, 25, 30}, 4, 1, 1, NULL);
    scene_object_set_collider(&scene, s, sphere);            // offset (0, 5, 0)
    int b = scene_add_object(&scene, &o);
    int box = collision_add_aabb(&scene.collision, (vec3_t){0, 0, 0}, (vec3_t){20, 10, 60}, 2, 2, NULL);
    scene_object_set_collider(&scene, b, box);               // centre (10,5,30): offset (0,-15,0)

    scene.objects[s].position = (vec3_t){-100, 0, 7};
    scene.objects[b].position = (vec3_t){50, 15, 30};
    scene_sync_colliders(&scene);
    const Collider *cs = &scene.collision.colliders[sphere];
    CHECK(cs->shape.sphere.center.x == -100.0f && cs->shape.sphere.center.y == 5.0f &&
          cs->shape.sphere.center.z == 7.0f);
    CHECK(cs->shape.sphere.radius == 4.0f);
    CHECK(cs->bounds.min.y == 1.0f && cs->bounds.max.y == 9.0f);   // broadphase box too
    const Collider *cb = &scene.collision.colliders[box];
    CHECK(cb->shape.aabb.min.x == 40.0f && cb->shape.aabb.max.x == 60.0f);
    CHECK(cb->shape.aabb.min.y == -5.0f && cb->shape.aabb.max.y == 5.0f);
    CHECK(cb->shape.aabb.min.z == 0.0f && cb->shape.aabb.max.z == 60.0f);
    CHECK(cb->bounds.min.x == 40.0f && cb->bounds.max.x == 60.0f);

    // An object that does not move leaves its box bit-identical
    ColliderAABB before = cb->shape.aabb;
    for (int i = 0; i < 1000; i++) scene_sync_colliders(&scene);
    CHECK(memcmp(&before, &cb->shape.aabb, sizeof(before)) == 0);
}

// Pair tests see colliders where their objects are
static void test_pairs_follow_objects(void) {
    scene_objects_init(&scene);
    int a = add_with_sphere((vec3_t){0, 0, 0}, 10);
    int b = add_with_sphere((vec3_t){100, 0, 0}, 10);
    scene_sync_colliders(&scene);
    CHECK(collision_test_all(&scene.collision) == 0);
    scene.objects[b].position = (vec3_t){15, 0, 0};          // overlaps a
    scene_sync_colliders(&scene);
    CHECK(collision_test_all(&scene.collision) == 1);
    CHECK(scene.collision.results[0].id_a == scene.objects[a].collider_handle);
    CHECK(scene.collision.results[0].id_b == scene.objects[b].collider_handle);
    scene.objects[b].position = (vec3_t){15, 50, 0};
    scene_sync_colliders(&scene);
    CHECK(collision_test_all(&scene.collision) == 0);
}

// A body moves its object, and the object its collider (scene_update's
// order); the collider on the default layer does not stop the body's ENV
// ground raycasts
static void test_body_drives_object(void) {
    scene_objects_init(&scene);
    int floor = collision_add_aabb(&scene.collision, (vec3_t){-500, -20, -500}, (vec3_t){500, 0, 500},
                                   COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV,
                                   COLLISION_LAYER_DEFAULT | COLLISION_LAYER_ENV, NULL);
    collision_set_static(&scene.collision, floor, true);
    vec3_t start = {30, 300, -40};
    SceneObject o = object_at(start, SCENE_OBJ_CASTS_SHADOW);
    int idx = scene_add_object(&scene, &o);
    int body = physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, start);
    scene_object_set_body(&scene, idx, body);
    scene_object_set_collider(&scene, idx, collision_add_sphere(&scene.collision, start,
        PHYSICS_DEF_BALL.radius, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL));

    float lowest = start.y;
    for (int i = 0; i < 60 * 8; i++) {                       // 8 s
        physics_world_update(&scene.physics, PHYSICS_DT);
        scene_sync_bodies(&scene);
        scene_sync_colliders(&scene);
        collision_test_all(&scene.collision);
        if (scene.objects[idx].position.y < lowest) lowest = scene.objects[idx].position.y;
    }
    const SceneObject *obj = &scene.objects[idx];
    const PhysicsBody *pb = physics_body_get(&scene.physics, body);
    CHECK(pb != NULL && physics_body_is_grounded(pb));
    CHECK(obj->position.x == pb->position.x && obj->position.y == pb->position.y &&
          obj->position.z == pb->position.z);
    CHECK(lowest >= PHYSICS_DEF_BALL.radius - 0.5f);           // never through the floor
    CHECK(obj->position.y <= PHYSICS_DEF_BALL.radius + 2.0f);  // resting on it
    const Collider *c = &scene.collision.colliders[obj->collider_handle];
    CHECK(c->shape.sphere.center.y == obj->position.y);

    // Held by game code (kinematic): the object stays where it is put
    PhysicsBody *held = physics_body_get(&scene.physics, body);
    held->kinematic = true;
    held->position = (vec3_t){0, 200, 0};
    held->velocity = VEC3_ZERO;
    for (int i = 0; i < 30; i++) {
        physics_world_update(&scene.physics, PHYSICS_DT);
        scene_sync_bodies(&scene);
    }
    CHECK(obj->position.y == 200.0f);
    held->kinematic = false;                                   // released: falls
    for (int i = 0; i < 30; i++) {
        physics_world_update(&scene.physics, PHYSICS_DT);
        scene_sync_bodies(&scene);
    }
    CHECK(obj->position.y < 200.0f);
}

// Selection and pass membership by flags, including objects added later
// (the demo's ball, D20)
static void test_find_object(void) {
    const uint16_t SEL = SCENE_OBJ_SELECTABLE, SH = SCENE_OBJ_CASTS_SHADOW;
    const uint16_t flags[] = { SEL | SH, 0, SEL, SH, SEL | SH };
    scene_objects_init(&scene);
    for (int i = 0; i < 5; i++) {
        SceneObject o = object_at(VEC3_ZERO, flags[i]);
        scene_add_object(&scene, &o);
    }
    CHECK(scene_find_object(&scene, -1, 1, SEL) == 0);
    CHECK(scene_find_object(&scene, 0, 1, SEL) == 2);
    CHECK(scene_find_object(&scene, 2, 1, SEL) == 4);
    CHECK(scene_find_object(&scene, 4, 1, SEL) == 0);         // wraps
    CHECK(scene_find_object(&scene, 0, -1, SEL) == 4);        // wraps backwards
    CHECK(scene_find_object(&scene, -1, -1, SEL) == 4);       // from the end
    CHECK(scene_find_object(&scene, 4, -1, SEL) == 2);
    CHECK(scene_find_object(&scene, 0, 1, SEL | SH) == 4);    // every bit required
    CHECK(scene_find_object(&scene, 4, 1, SEL | SH) == 0);
    CHECK(scene_find_object(&scene, 1, 1, 0) == 2);           // no flags: any object
    CHECK(scene_find_object(&scene, -1, 1, SCENE_OBJ_FLAG_USER) == -1);

    SceneObject late = object_at(VEC3_ZERO, SEL);
    int idx = scene_add_object(&scene, &late);
    CHECK(scene_find_object(&scene, 4, 1, SEL) == idx);
    CHECK(scene_find_object(&scene, 0, -1, SEL) == idx);

    scene_objects_init(&scene);
    SceneObject one = object_at(VEC3_ZERO, SEL);
    scene_add_object(&scene, &one);
    CHECK(scene_find_object(&scene, 0, 1, SEL) == 0);         // the only match: itself
    CHECK(scene_find_object(&scene, 0, -1, SEL) == 0);
    scene_objects_init(&scene);
    CHECK(scene_find_object(&scene, -1, 1, SEL) == -1);       // empty scene
}

// Init (every scene_init, so every reset) empties objects and both worlds
static void test_reinit_empties(void) {
    scene_objects_init(&scene);
    add_with_sphere((vec3_t){0, 0, 0}, 10);
    int idx = add_with_sphere((vec3_t){50, 0, 0}, 10);
    scene_object_set_body(&scene, idx,
        physics_body_add(&scene.physics, &PHYSICS_DEF_BALL, (vec3_t){50, 0, 0}));
    scene_objects_init(&scene);
    CHECK(scene.object_count == 0);
    CHECK(scene.collision.count == 0 && scene.collision.high == 0);
    CHECK(scene.physics.body_count == 0 && scene.physics.collision == &scene.collision);
}

void run_scene_tests(void) {
    RUN_TEST(test_add_clears_handles);
    RUN_TEST(test_remove_frees_attachments);
    RUN_TEST(test_set_replaces);
    RUN_TEST(test_collider_sync);
    RUN_TEST(test_pairs_follow_objects);
    RUN_TEST(test_body_drives_object);
    RUN_TEST(test_find_object);
    RUN_TEST(test_reinit_empties);
}
