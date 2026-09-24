#include "scene.h"

// Scene objects and what is attached to them: colliders in the scene's
// CollisionWorld and bodies in its PhysicsWorld. No rendering here, so the
// host tests cover it (tests/host/test_scene.c).

void scene_objects_init(Scene *scene) {
    scene->object_count = 0;
    collision_world_init(&scene->collision);
    physics_world_init(&scene->physics, &scene->collision);
}

// --- Object management ---

int scene_add_object(Scene *scene, const SceneObject *obj) {
    if (scene->object_count >= SCENE_MAX_OBJECTS) return -1;
    int idx = scene->object_count;
    SceneObject *o = &scene->objects[idx];
    *o = *obj;
    // A zero-initialised object would otherwise own collider 0 and body 0
    o->collider_handle = -1;
    o->body_handle = -1;
    o->collider_offset = VEC3_ZERO;
    scene->object_count++;
    return idx;
}

void scene_remove_object(Scene *scene, int index) {
    if (index < 0 || index >= scene->object_count) return;
    const SceneObject *obj = &scene->objects[index];
    collision_remove(&scene->collision, obj->collider_handle);   // -1: no-op
    physics_body_remove(&scene->physics, obj->body_handle);
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

int scene_find_object(const Scene *scene, int from, int step, uint16_t flags) {
    const int n = scene->object_count;
    step = step < 0 ? -1 : 1;
    int i = (from >= 0 && from < n) ? from : (step > 0 ? -1 : n);
    for (int k = 0; k < n; k++) {
        i += step;
        if (i >= n) i = 0;
        if (i < 0) i = n - 1;
        if ((scene->objects[i].flags & flags) == flags) return i;
    }
    return -1;
}

// --- Attachments ---

static vec3_t collider_center(const Collider *c) {
    if (c->type == COLLIDER_SPHERE) return c->shape.sphere.center;
    vec3_t sum = vec3_add(&c->shape.aabb.min, &c->shape.aabb.max);
    return vec3_scale(&sum, 0.5f);
}

void scene_object_set_collider(Scene *scene, int index, int collider) {
    SceneObject *obj = scene_get_object(scene, index);
    if (!obj) return;
    if (obj->collider_handle != collider)
        collision_remove(&scene->collision, obj->collider_handle);
    obj->collider_handle = -1;
    obj->collider_offset = VEC3_ZERO;
    if (collider < 0 || collider >= COLLISION_MAX_COLLIDERS) return;
    const Collider *c = &scene->collision.colliders[collider];
    if (!c->active) return;
    vec3_t center = collider_center(c);
    obj->collider_handle = collider;
    obj->collider_offset = vec3_sub(&center, &obj->position);
}

void scene_object_set_body(Scene *scene, int index, int body) {
    SceneObject *obj = scene_get_object(scene, index);
    if (!obj) return;
    if (obj->body_handle != body)
        physics_body_remove(&scene->physics, obj->body_handle);
    obj->body_handle = physics_body_get(&scene->physics, body) ? body : -1;
}

// --- Per-frame sync ---

void scene_sync_bodies(Scene *scene) {
    for (int i = 0; i < scene->object_count; i++) {
        SceneObject *obj = &scene->objects[i];
        if (obj->body_handle < 0) continue;
        const PhysicsBody *body = physics_body_get(&scene->physics, obj->body_handle);
        if (body) obj->position = body->position;
    }
}

void scene_sync_colliders(Scene *scene) {
    for (int i = 0; i < scene->object_count; i++) {
        const SceneObject *obj = &scene->objects[i];
        if (obj->collider_handle < 0) continue;
        const Collider *c = &scene->collision.colliders[obj->collider_handle];
        vec3_t target = vec3_add(&obj->position, &obj->collider_offset);
        if (c->type == COLLIDER_SPHERE) {
            collision_update_sphere(&scene->collision, obj->collider_handle, target);
        } else {
            // Translate the box; unchanged when the object did not move
            vec3_t center = collider_center(c);
            vec3_t delta = vec3_sub(&target, &center);
            if (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f) continue;
            vec3_t min = vec3_add(&c->shape.aabb.min, &delta);
            vec3_t max = vec3_add(&c->shape.aabb.max, &delta);
            collision_update_aabb(&scene->collision, obj->collider_handle, min, max);
        }
    }
}
