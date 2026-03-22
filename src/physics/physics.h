#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdbool.h>
#include "../math/vec3.h"
#include "../collision/collision.h"

#define PHYSICS_MAX_BODIES       32
#define PHYSICS_DT               (1.0f / 60.0f)
#define PHYSICS_MAX_STEPS        4
#define PHYSICS_GRAVITY_DEFAULT  -981.0f   // ~10x real gravity (N64 scale units/s^2)

// --- Data-driven body definition (static const, reusable across scenes) ---

typedef struct {
    float mass;            // kg (affects impulse response, >0)
    float restitution;     // Bounce factor 0..1 (0=no bounce, 1=perfect elastic)
    float friction;        // Tangential velocity damping on ground contact 0..1
    float damping;         // Air resistance per step (velocity *= 1-damping)
    float gravity_scale;   // Multiplier on world gravity (0=floating, 1=normal, 2=heavy)
    float radius;          // Collision sphere radius for ground detection
} PhysicsBodyDef;

// --- Runtime physics body ---

typedef struct {
    bool   active;
    vec3_t position;
    vec3_t velocity;
    vec3_t acceleration;   // External forces accumulated this frame (reset after step)

    // Material properties (copied from def, mutable at runtime)
    float  mass;
    float  restitution;
    float  friction;
    float  damping;
    float  gravity_scale;
    float  radius;

    // Ground state (updated each physics step)
    bool   grounded;
    vec3_t ground_normal;
    float  ground_y;       // Y of ground surface at current XZ position

    // Configuration
    int    collision_layer_mask;  // Which collision layers to raycast against
} PhysicsBody;

// --- Physics world (one per scene that needs physics) ---

typedef struct {
    PhysicsBody bodies[PHYSICS_MAX_BODIES];
    int         body_count;
    float       accumulator;       // Semi-fixed timestep accumulator
    float       gravity;           // World gravity (Y acceleration, negative = down)
    CollisionWorld *collision;     // Pointer to scene's collision world (for raycasts)
} PhysicsWorld;

// --- Lifecycle ---

void physics_world_init(PhysicsWorld *world, CollisionWorld *collision);
void physics_world_update(PhysicsWorld *world, float dt);

// --- Body management ---

// Add a body with the given definition at position. Returns handle or -1 if full.
int  physics_body_add(PhysicsWorld *world, const PhysicsBodyDef *def, vec3_t position);
void physics_body_remove(PhysicsWorld *world, int handle);
PhysicsBody *physics_body_get(PhysicsWorld *world, int handle);

// --- Forces and impulses ---

// Apply instantaneous velocity change: delta_v = impulse / mass
void physics_body_apply_impulse(PhysicsBody *body, vec3_t impulse);

// Apply continuous force: acceleration += force / mass (accumulated until next step)
void physics_body_apply_force(PhysicsBody *body, vec3_t force);

// Set velocity directly (overrides current velocity)
void physics_body_set_velocity(PhysicsBody *body, vec3_t velocity);

// --- Queries ---

bool  physics_body_is_grounded(const PhysicsBody *body);
float physics_body_speed(const PhysicsBody *body);
float physics_body_speed_sq(const PhysicsBody *body);  // Avoids sqrt

// --- Built-in body definitions ---

extern const PhysicsBodyDef PHYSICS_DEF_BALL;      // Standard bouncy ball (r=20, e=0.7)
extern const PhysicsBodyDef PHYSICS_DEF_HEAVY;     // Heavy, low bounce (r=25, e=0.2)
extern const PhysicsBodyDef PHYSICS_DEF_FLOATY;    // Low gravity, high bounce (r=15, e=0.9)

#endif
