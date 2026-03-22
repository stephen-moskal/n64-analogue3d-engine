#include "physics.h"
#include <math.h>
#include <string.h>

// --- Built-in body definitions ---

const PhysicsBodyDef PHYSICS_DEF_BALL = {
    .mass = 1.0f, .restitution = 0.7f, .friction = 0.3f,
    .damping = 0.02f, .gravity_scale = 1.0f, .radius = 20.0f
};

const PhysicsBodyDef PHYSICS_DEF_HEAVY = {
    .mass = 5.0f, .restitution = 0.2f, .friction = 0.6f,
    .damping = 0.05f, .gravity_scale = 1.5f, .radius = 25.0f
};

const PhysicsBodyDef PHYSICS_DEF_FLOATY = {
    .mass = 0.5f, .restitution = 0.9f, .friction = 0.1f,
    .damping = 0.01f, .gravity_scale = 0.3f, .radius = 15.0f
};

// --- Lifecycle ---

void physics_world_init(PhysicsWorld *world, CollisionWorld *collision) {
    memset(world->bodies, 0, sizeof(world->bodies));
    world->body_count = 0;
    world->accumulator = 0.0f;
    world->gravity = PHYSICS_GRAVITY_DEFAULT;
    world->collision = collision;
}

// --- Body management ---

int physics_body_add(PhysicsWorld *world, const PhysicsBodyDef *def, vec3_t position) {
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        if (!world->bodies[i].active) {
            PhysicsBody *b = &world->bodies[i];
            b->active = true;
            b->position = position;
            b->velocity = VEC3_ZERO;
            b->acceleration = VEC3_ZERO;
            b->mass = def->mass;
            b->restitution = def->restitution;
            b->friction = def->friction;
            b->damping = def->damping;
            b->gravity_scale = def->gravity_scale;
            b->radius = def->radius;
            b->grounded = false;
            b->ground_normal = VEC3_UP;
            b->ground_y = -9999.0f;
            b->collision_layer_mask = COLLISION_LAYER_ENV;
            world->body_count++;
            return i;
        }
    }
    return -1;
}

void physics_body_remove(PhysicsWorld *world, int handle) {
    if (handle < 0 || handle >= PHYSICS_MAX_BODIES) return;
    if (world->bodies[handle].active) {
        world->bodies[handle].active = false;
        world->body_count--;
    }
}

PhysicsBody *physics_body_get(PhysicsWorld *world, int handle) {
    if (handle < 0 || handle >= PHYSICS_MAX_BODIES) return NULL;
    if (!world->bodies[handle].active) return NULL;
    return &world->bodies[handle];
}

// --- Forces and impulses ---

void physics_body_apply_impulse(PhysicsBody *body, vec3_t impulse) {
    float inv_mass = (body->mass > 0.001f) ? 1.0f / body->mass : 1.0f;
    vec3_t dv = vec3_scale(&impulse, inv_mass);
    body->velocity = vec3_add(&body->velocity, &dv);
}

void physics_body_apply_force(PhysicsBody *body, vec3_t force) {
    float inv_mass = (body->mass > 0.001f) ? 1.0f / body->mass : 1.0f;
    vec3_t accel = vec3_scale(&force, inv_mass);
    body->acceleration = vec3_add(&body->acceleration, &accel);
}

void physics_body_set_velocity(PhysicsBody *body, vec3_t velocity) {
    body->velocity = velocity;
}

// --- Queries ---

bool physics_body_is_grounded(const PhysicsBody *body) {
    return body->grounded;
}

float physics_body_speed_sq(const PhysicsBody *body) {
    return vec3_length_sq(&body->velocity);
}

float physics_body_speed(const PhysicsBody *body) {
    return sqrtf(physics_body_speed_sq(body));
}

// --- Simulation ---

static void physics_step(PhysicsWorld *world) {
    for (int i = 0; i < PHYSICS_MAX_BODIES; i++) {
        PhysicsBody *b = &world->bodies[i];
        if (!b->active) continue;

        // Apply gravity
        b->velocity.y += world->gravity * b->gravity_scale * PHYSICS_DT;

        // Apply external acceleration
        vec3_t accel_dt = vec3_scale(&b->acceleration, PHYSICS_DT);
        b->velocity = vec3_add(&b->velocity, &accel_dt);
        b->acceleration = VEC3_ZERO;

        // Air damping
        float damp = 1.0f - b->damping;
        b->velocity = vec3_scale(&b->velocity, damp);

        // Integrate position
        vec3_t vel_dt = vec3_scale(&b->velocity, PHYSICS_DT);
        b->position = vec3_add(&b->position, &vel_dt);

        // Ground detection via downward raycast
        if (world->collision) {
            Ray ray;
            ray.origin = (vec3_t){
                b->position.x,
                b->position.y + b->radius,
                b->position.z
            };
            ray.direction = (vec3_t){0, -1, 0};
            ray.max_distance = b->radius * 3.0f;

            CollisionResult result;
            if (collision_raycast(world->collision, &ray,
                                  b->collision_layer_mask, &result)) {
                float ground_y = result.point.y;
                b->ground_y = ground_y;
                b->ground_normal = result.normal;

                // Penetration resolution
                if (b->position.y - b->radius < ground_y) {
                    b->position.y = ground_y + b->radius;

                    // Bounce: decompose velocity into normal and tangent
                    float vn = vec3_dot(&b->velocity, &result.normal);
                    if (vn < 0) {  // Moving into surface
                        vec3_t v_normal = vec3_scale(&result.normal, vn);
                        vec3_t v_tangent = vec3_sub(&b->velocity, &v_normal);

                        // Reflect normal component with restitution
                        v_normal = vec3_scale(&v_normal, -b->restitution);
                        // Damp tangent with friction
                        float fric = 1.0f - b->friction;
                        v_tangent = vec3_scale(&v_tangent, fric);

                        b->velocity = vec3_add(&v_normal, &v_tangent);

                        // Rest threshold — stop micro-bouncing
                        if (fabsf(b->velocity.y) < 10.0f && vn > -20.0f) {
                            b->velocity.y = 0.0f;
                            b->grounded = true;
                        } else {
                            b->grounded = false;
                        }
                    }
                } else {
                    b->grounded = false;
                }
            } else {
                b->grounded = false;
                b->ground_y = -9999.0f;
            }
        }
    }
}

void physics_world_update(PhysicsWorld *world, float dt) {
    world->accumulator += dt;
    int steps = 0;
    while (world->accumulator >= PHYSICS_DT && steps < PHYSICS_MAX_STEPS) {
        physics_step(world);
        world->accumulator -= PHYSICS_DT;
        steps++;
    }
    // Clamp leftover to prevent drift after stalls
    if (world->accumulator > PHYSICS_DT) {
        world->accumulator = PHYSICS_DT;
    }
}
