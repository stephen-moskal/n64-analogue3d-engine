# Physics System

Lightweight physics layer built on top of the collision system. Provides gravity, velocity integration, ground detection, bounce response, and impulse forces. Designed for N64 hardware constraints — not a full rigid body solver, but sufficient for movement, jumping, projectile arcs, and hit reactions.

**Source:** `src/physics/physics.c/h`
**Dependencies:** `collision/collision.h`, `math/vec3.h`

## Architecture Overview

```
physics_world_update(dt)
    ├── accumulator += dt
    └── while (accumulator >= PHYSICS_DT):
          for each active body:
          ├── apply gravity (velocity.y += gravity * gravity_scale * dt)
          ├── apply external acceleration (velocity += acceleration * dt)
          ├── air damping (velocity *= 1 - damping)
          ├── integrate position (position += velocity * dt)
          ├── ground raycast (downward from body center)
          ├── penetration resolution (push out of ground)
          ├── bounce response (decompose, reflect, restitute)
          └── rest detection (stop micro-bouncing)
```

### Semi-Fixed Timestep

The physics system uses a **semi-fixed timestep** while the rest of the engine uses variable timestep. This gives physics deterministic, frame-rate-independent behavior without affecting input, camera, particles, or menu systems.

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `PHYSICS_DT` | 1/60s (16.67ms) | Fixed step size |
| `PHYSICS_MAX_STEPS` | 4 | Spiral-of-death prevention |
| Accumulator | float | Carries leftover time between frames |

**How it works:**
- Each frame, the real elapsed `dt` is added to the accumulator
- The simulation steps in fixed increments of `PHYSICS_DT`
- At 60 FPS: 1 step per frame. At 30 FPS: 2 steps per frame
- Below 15 FPS: capped at 4 steps (physics slows down gracefully)
- Leftover accumulator time carries to the next frame, capped at one step so a stall does not replay as a burst of steps

```c
void physics_world_update(PhysicsWorld *world, float dt) {
    world->accumulator += dt;
    int steps = 0;
    while (world->accumulator >= PHYSICS_DT && steps < PHYSICS_MAX_STEPS) {
        physics_step(world);
        world->accumulator -= PHYSICS_DT;
        steps++;
    }
    if (world->accumulator > PHYSICS_DT) world->accumulator = PHYSICS_DT;
}
```

**Why not variable timestep for physics?** Variable dt causes trajectory differences at different frame rates — a ball dropped at 30 FPS would land at a slightly different position than at 60 FPS due to floating-point integration error accumulation. Semi-fixed timestep produces identical results regardless of display rate.

**Why not fixed timestep for everything?** The engine previously tried a 30Hz fixed-timestep accumulator for all logic, which caused duplicate frames at 60 FPS (no logic update every other frame). Semi-fixed timestep only applies to physics; input, camera, and rendering stay on variable dt for responsiveness.

## Data Types

### PhysicsBodyDef

Data-driven body definition. Define as `static const` and reuse across scenes.

```c
typedef struct {
    float mass;            // kg (affects impulse response, >0)
    float restitution;     // Bounce factor 0..1 (0=no bounce, 1=perfect elastic)
    float friction;        // Tangential velocity damping on ground contact 0..1
    float damping;         // Air resistance per step (velocity *= 1-damping)
    float gravity_scale;   // Multiplier on world gravity (0=floating, 1=normal)
    float radius;          // Collision sphere radius for ground detection
} PhysicsBodyDef;
```

| Field | Range | Effect |
|-------|-------|--------|
| `mass` | >0 | Higher mass = less velocity change from impulses (`dv = impulse / mass`) |
| `restitution` | 0..1 | 0 = dead stop on contact, 0.7 = bouncy ball, 1.0 = perfect bounce |
| `friction` | 0..1 | 0 = ice (no tangential damping), 1.0 = sticky (full tangent kill) |
| `damping` | 0..1 | 0 = no air resistance, 0.02 = light drag, 0.1 = heavy drag |
| `gravity_scale` | any | 0 = floating, 1 = normal gravity, 2 = heavy, negative = anti-gravity |
| `radius` | >0 | Sphere radius for ground raycast origin offset and penetration check |

### PhysicsBody

Runtime state for a single physics object.

```c
typedef struct {
    bool   active;
    bool   kinematic;      // Moved by game code: the simulation leaves it alone
    vec3_t position;       // Authoritative position (the scene copies it to the object)
    vec3_t velocity;       // Current velocity (units/second)
    vec3_t acceleration;   // External forces, reset after each step

    // Material (from def, mutable at runtime)
    float  mass, restitution, friction, damping, gravity_scale, radius;

    // Ground state (updated each step)
    bool   grounded;       // True when resting on surface
    vec3_t ground_normal;  // Surface normal at contact point
    float  ground_y;       // Y height of ground below body

    int    collision_layer_mask;  // Raycast filter (default: COLLISION_LAYER_ENV)
} PhysicsBody;
```

**Position ownership:** the body owns its position. A body attached to a scene object (`scene_object_set_body()`) moves the object: after every physics update `scene_sync_bodies()` copies `body->position` to `SceneObject.position` ([SCENE_SYSTEM.md](SCENE_SYSTEM.md)). The physics module itself knows nothing about scenes.

**Kinematic bodies:** set `kinematic` to move a body by hand. `physics_step()` skips it (no gravity, no integration, no ground contact); game code writes its position, and zeroes its velocity if it should start from rest when released. The demo holds the ball this way while it is transformed.

### PhysicsWorld

Container for all physics bodies in a scene.

```c
typedef struct {
    PhysicsBody bodies[PHYSICS_MAX_BODIES];  // 32 body pool
    int         body_count;
    float       accumulator;                  // Semi-fixed timestep state
    float       gravity;                      // World gravity (default: -981.0)
    CollisionWorld *collision;                // For ground raycasts
} PhysicsWorld;
```

**Owned by the scene:** every `Scene` has one (`Scene.physics`, ~2.8 KB). `scene_init()` empties it and points it at the scene's collision world, and `scene_update()` steps it after `on_update` when it holds at least one body; a scene without bodies pays two empty loops per frame.

## Algorithms

### Euler Integration

Each physics step applies forces and integrates position using semi-implicit Euler:

```
velocity.y += gravity * gravity_scale * dt    // Gravity
velocity += acceleration * dt                  // External forces
velocity *= (1 - damping)                      // Air resistance
position += velocity * dt                      // Position integration
acceleration = {0, 0, 0}                       // Reset external forces
```

Semi-implicit Euler updates velocity first, then uses the new velocity to update position. This is more stable than explicit Euler (which uses old velocity) and sufficient for the N64's game-scale physics.

### Ground Detection

Each step, a downward raycast detects the ground surface below the body:

```
Ray origin:    (body.x, body.y + radius, body.z)
Ray direction: (0, -1, 0)
Max distance:  radius * 3.0
Layer mask:    body.collision_layer_mask (default: COLLISION_LAYER_ENV)
```

The ray starts from above the body center (offset by radius) to avoid self-intersection. The `3× radius` max distance ensures ground is detected even during fast falls without wasting time on distant surfaces.

The raycast uses the existing collision system (`collision_raycast()`), which tests against all AABB and sphere colliders matching the layer mask.

### Bounce Response

When a body penetrates the ground (position.y - radius < ground_y), the velocity is decomposed into normal and tangential components:

```
vn = dot(velocity, ground_normal)              // Normal speed (negative = into surface)
v_normal  = ground_normal * vn                 // Normal velocity component
v_tangent = velocity - v_normal                // Tangential velocity component

v_normal  = v_normal * -restitution            // Reflect and scale by bounciness
v_tangent = v_tangent * (1 - friction)         // Damp sliding motion

velocity = v_normal + v_tangent                // Recombine
```

This decomposition handles arbitrary surface normals — not just flat ground. A ball hitting a tilted surface will bounce off at the correct angle.

### Rest Detection

To prevent infinite micro-bouncing, a rest threshold stops bodies with very small bounce velocity:

```
if (|velocity.y| < 10.0 && |vn| < 20.0):
    velocity.y = 0
    grounded = true
```

Once grounded, the body stays on the surface. Gravity still applies each step but is immediately canceled by the ground contact. Applying an impulse re-launches the body.

### Penetration Resolution

When a body sinks below the ground surface:

```
body.position.y = ground_y + radius
```

This simple position correction snaps the body to the surface before computing bounce. It prevents bodies from tunneling through thin surfaces at high speeds (within the `3× radius` raycast range).

## API Reference

### Lifecycle

```c
// Initialize world with pointer to scene's collision system
void physics_world_init(PhysicsWorld *world, CollisionWorld *collision);

// Advance simulation by dt seconds (semi-fixed timestep internally)
void physics_world_update(PhysicsWorld *world, float dt);
```

### Body Management

```c
// Create a body from a definition at the given position. Returns handle or -1.
int physics_body_add(PhysicsWorld *world, const PhysicsBodyDef *def, vec3_t position);

// Remove a body by handle
void physics_body_remove(PhysicsWorld *world, int handle);

// Get body pointer (NULL if invalid/inactive)
PhysicsBody *physics_body_get(PhysicsWorld *world, int handle);
```

### Forces and Impulses

```c
// Instantaneous velocity change: delta_v = impulse / mass
// Use for: jumps, knockback, explosions, projectile launch
void physics_body_apply_impulse(PhysicsBody *body, vec3_t impulse);

// Continuous force (accumulated per frame): acceleration += force / mass
// Use for: wind, currents, thrusters, magnetic pull
void physics_body_apply_force(PhysicsBody *body, vec3_t force);

// Override velocity directly
// Use for: teleportation reset, cutscene positioning, manual movement
void physics_body_set_velocity(PhysicsBody *body, vec3_t velocity);
```

**Impulse vs Force:** An impulse changes velocity instantly in one frame (jump, hit). A force is accumulated and applied over time during the next physics step (wind, gravity). Forces are reset to zero after each step; impulses are applied once.

### Queries

```c
bool  physics_body_is_grounded(const PhysicsBody *body);   // On a surface?
float physics_body_speed(const PhysicsBody *body);          // |velocity| (uses sqrt)
float physics_body_speed_sq(const PhysicsBody *body);       // |velocity|² (no sqrt)
```

Use `speed_sq` for comparisons to avoid `sqrtf` overhead on the VR4300.

## Built-in Body Definitions

Three reusable presets for common physics behaviors:

| Definition | Mass | Restitution | Friction | Damping | Gravity | Radius | Use Case |
|------------|------|-------------|----------|---------|---------|--------|----------|
| `PHYSICS_DEF_BALL` | 1.0 | 0.7 | 0.3 | 0.02 | 1.0× | 20 | Bouncy ball, projectiles |
| `PHYSICS_DEF_HEAVY` | 5.0 | 0.2 | 0.6 | 0.05 | 1.5× | 25 | Boulders, heavy objects |
| `PHYSICS_DEF_FLOATY` | 0.5 | 0.9 | 0.1 | 0.01 | 0.3× | 15 | Feathers, magic orbs |

### Custom Definitions

Define your own body types as compile-time data:

```c
static const PhysicsBodyDef my_projectile = {
    .mass = 0.2f,
    .restitution = 0.3f,
    .friction = 0.5f,
    .damping = 0.01f,
    .gravity_scale = 0.5f,    // Half gravity for floaty arcs
    .radius = 10.0f,
};

int handle = physics_body_add(&world, &my_projectile, start_pos);
```

## Integration Pattern

The scene owns the world, steps it and moves objects to their bodies ([SCENE_SYSTEM.md](SCENE_SYSTEM.md)); a scene only adds bodies and attaches them to objects.

### Spawning a Physics Object

```c
// The body, then the object it moves (the demo's launch_ball())
int body = physics_body_add(&scene->physics, &PHYSICS_DEF_BALL, spawn_pos);
int idx  = spawn_object(scene, "Ball", mesh_defs_get_sphere(), spawn_pos,
                        (vec3_t){20, 20, 20}, false, 0, 0);
scene_object_set_body(scene, idx, body);

// Optional: a collider for pair tests and camera push-out, moved with the
// object. Not on the ENV layer the body's ground ray uses.
scene_object_set_collider(scene, idx, collision_add_sphere(&scene->collision, spawn_pos,
    PHYSICS_DEF_BALL.radius, COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, NULL));
```

### Frame Update

Nothing to call: `scene_update()` runs `physics_world_update()` and `scene_sync_bodies()` after `on_update`. Game code reads and pushes bodies in `on_update`:

```c
static void my_scene_update(Scene *scene, float dt) {
    SceneObject *obj = scene_get_object(scene, ball_index);
    PhysicsBody *body = obj ? physics_body_get(&scene->physics, obj->body_handle) : NULL;
    if (body && body->grounded && action_pressed(ACTION_CONFIRM)) {
        physics_body_apply_impulse(body, (vec3_t){0, 300, 0});   // jump
    }
}
```

### Applying Impulses

```c
// Jump
PhysicsBody *body = physics_body_get(&world, handle);
if (body && body->grounded) {
    physics_body_apply_impulse(body, (vec3_t){0, 300, 0});
}

// Knockback from hit direction
vec3_t knockback = vec3_scale(&hit_dir, 500.0f);
knockback.y = 200.0f;  // Pop upward
physics_body_apply_impulse(body, knockback);
```

## Vector Math Utilities

The physics system uses `vec3.h` (header-only), which includes these utilities added for physics support:

### Constants

| Constant | Value | Usage |
|----------|-------|-------|
| `VEC3_ZERO` | (0, 0, 0) | Reset velocity, zero-init |
| `VEC3_ONE` | (1, 1, 1) | Unit scale |
| `VEC3_UP` | (0, 1, 0) | Gravity direction, ground normal |
| `VEC3_DOWN` | (0, -1, 0) | Downward raycast direction |
| `VEC3_RIGHT` | (1, 0, 0) | Horizontal axis |
| `VEC3_FORWARD` | (0, 0, -1) | Camera-forward convention |

### Physics Math Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `vec3_mul` | `(a, b) → vec3` | Component-wise multiply |
| `vec3_reflect` | `(incident, normal) → vec3` | Reflect vector off surface (`v - 2·dot(v,n)·n`) |
| `vec3_project` | `(a, b) → vec3` | Project `a` onto `b` (parallel component) |
| `vec3_reject` | `(a, b) → vec3` | Reject `a` from `b` (perpendicular component) |
| `vec3_clamp_length` | `(v, max) → vec3` | Limit vector magnitude (speed cap) |
| `vec3_move_toward` | `(cur, tgt, delta) → vec3` | Move toward target by max distance |
| `vec3_abs` | `(v) → vec3` | Per-component absolute value |
| `vec3_sign` | `(v) → vec3` | Per-component sign (-1, 0, or 1) |

All functions follow the existing `vec3.h` convention: parameters are `const vec3_t *` (pointer), return `vec3_t` (value).

## Performance

### CPU Cost

| Operation | Per Body | Notes |
|-----------|----------|-------|
| Gravity + integration | ~20 FP ops | 3 multiplies, 3 adds per component |
| Air damping | ~6 FP ops | 1 multiply per component |
| Raycast | ~100-200 cycles | Depends on collider count |
| Bounce decomposition | ~30 FP ops | dot, scale, sub, scale, add |
| **Total per body per step** | **~300 cycles** | Negligible at 93.75 MHz |

At 60 FPS with 1 step/frame and 32 bodies: ~9,600 cycles/frame = ~0.1ms. Physics is well within the ~7-9ms CPU budget available after rendering.

`physics_world_update()` records the body count and the fixed steps it ran in the per-frame stats (`physics_bodies`, `physics_steps`), and `scene_update()` times it, with the body sync, in the `physics` profiler slot ([PROFILING.md](PROFILING.md)); the demo measured ~0.02 ms on the Analogue 3D ([BENCHMARKS.md](BENCHMARKS.md)).

### Memory Cost

| Resource | Size |
|----------|------|
| PhysicsWorld (32 bodies, one per scene) | ~2.8 KB |
| Per body | 88 bytes |
| Code (physics.c) | ~2 KB |

### Limits

| Limit | Value | Rationale |
|-------|-------|-----------|
| `PHYSICS_MAX_BODIES` | 32 | Matches scene object limit, pool-based |
| `PHYSICS_MAX_STEPS` | 4 | Prevents spiral of death below 15 FPS |
| Raycasts per step | 1 per body | Ground detection only |
| Max raycast distance | 3× body radius | Balances detection range vs performance |

## N64 Hardware Considerations

- **VR4300 FPU:** No pipeline — each float multiply takes ~10 cycles. The physics step uses ~30 FP operations per body, well within budget
- **No SIMD:** All vector math is scalar. `vec3_t` is 3 separate floats, not packed
- **Avoid `sqrtf` in loops:** `physics_body_speed_sq()` provides squared speed for comparisons. Only `physics_body_speed()` uses sqrt
- **Reciprocal multiply:** `1.0f / mass` computed once per impulse call, not per step
- **Fixed-point alternative:** Not used. Float math on VR4300 is fast enough for 32 bodies at 60 FPS, and the code stays readable

## Demo Scene Integration

The demo scene (`src/scenes/demo_scene.c`) demonstrates the physics system with a bouncy ball:

- **B button** in normal mode spawns a red sphere above the platform: a scene object with a body and a sphere collider on the default layer
- The ball falls under gravity, bounces on the platform surface (the platform's box collider, top at Y=-85)
- Bounces diminish via restitution (0.7) until the ball comes to rest; each bounce plays a positional sound scaled by the impact speed
- Pressing B again resets the ball position and applies an upward impulse
- The platform's collider is its box, on `COLLISION_LAYER_ENV` for flat ground detection; it moves with the platform, so the ball lands on the platform wherever it is moved
- If the ball rolls off the platform, it continues bouncing on the floor (ground AABB at Y=-100)
- The ball casts a shadow and can be selected (Z, D-Left/Right): while it is transformed it is held in place (kinematic) and moves with the stick; it drops when the mode ends

## Testing

`tests/host/test_physics.c` checks free fall, bounce and rest detection, the max-steps clamp and kinematic bodies on the host; `tests/host/test_scene.c` checks a body moving its object and the object its collider. The ball demo has so far been verified in ares only; the hardware check is planned (ROADMAP_v2 D16).

## Future Extensions

The physics system is designed for incremental expansion:

- **Sphere-sphere collision:** Body-to-body collision response for combat knockback
- **Moving platforms:** a platform's collider already follows its object; bodies resting on it do not ride along yet (no carry)
- **Projectile arcs:** Use `gravity_scale < 1.0` for floaty projectiles
- **Character controller:** PhysicsBody + input = player movement with gravity/jumping
- **Hit reactions:** `apply_impulse()` with directional knockback vector from combat system
