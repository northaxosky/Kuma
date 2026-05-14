#pragma once

// ── Kuma Physics ────────────────────────────────────────────────
// Rigid body dynamics, collision detection, and queries.
//
// World scale: 1 unit = 1 meter. Designed for FPS-arena scale
// (active simulation area roughly within a few hundred meters of
// the origin). Single-precision positions; large-world precision
// (Jolt's DOUBLE_PRECISION mode) is not enabled.
//
// Determinism: not guaranteed. The simulation runs across multiple
// worker threads and outcomes can vary by run, machine, or build.
// Do not rely on identical results across runs.
//
// Threading: the physics namespace is not thread-safe. Call every
// function on the main thread, between frames. Worker threads spun
// up internally only run inside physics::simulate().
//
// Public API never exposes the underlying physics backend's types.
// Callers see opaque PhysicsHandle values and engine math types.

#include <cstdint>

#include <kuma/ecs.h>
#include <kuma/math.h>

namespace kuma {

class Registry;

namespace physics {

// ── Configuration ───────────────────────────────────────────────

enum class BodyType : uint8_t {
    // Driven by the simulation. Position and rotation are written
    // back into the entity's Transform every frame. Move via
    // set_linear_velocity / apply_impulse; set_transform is a
    // teleport and discards velocity.
    Dynamic,

    // Frozen at creation pose. Other bodies collide with it but it
    // never moves on its own. set_transform forces a broadphase
    // refresh and is intended for rare repositions (e.g. a moving
    // platform) - prefer Kinematic for that case.
    Static,

    // Game owns the pose. Each frame the current Transform is read
    // and pushed to the simulation as a kinematic target, which
    // computes the implied velocity for one fixed step. Other
    // dynamic bodies are pushed by it.
    Kinematic,
};

enum class BodyShape : uint8_t {
    Sphere,    // dimensions.x = radius (y, z ignored)
    Box,       // dimensions = half-extents on each axis
    Capsule,   // dimensions.x = radius, dimensions.y = half-height
};

// Categories used to filter collision pairs. The full enum is
// declared up front so future gameplay code can reference layers
// like Player / Enemy / PlayerProjectile without breaking saves
// or component values. The collision matrix wired up internally
// only enables a subset; callers can request any layer but pairs
// involving disabled layers will not collide until the matrix is
// expanded.
enum class PhysicsLayer : uint8_t {
    StaticWorld,
    Dynamic,
    Player,
    Enemy,
    PlayerProjectile,
    EnemyProjectile,
    Pickup,
    Sensor,
    Count,
};

// Top-level engine knobs. Defaults match a few-hundred-body
// arena scenario; raise the body limits before stress-testing.
struct PhysicsConfig {
    uint32_t max_bodies = 65536;
    uint32_t max_body_pairs = 65536;
    uint32_t max_contact_constraints = 10240;

    // Per-frame scratch memory the backend can carve up however it
    // likes. Reset between fixed steps. Bigger means fewer surprise
    // OOMs on heavy collision frames; cost is just resident memory.
    size_t temp_allocator_bytes = 10 * 1024 * 1024;

    // Fixed-timestep simulation: each frame we run integer steps of
    // length fixed_step until the accumulator is drained, capped at
    // max_steps_per_frame to prevent the spiral-of-death where a
    // slow frame compounds into more physics work next frame.
    float fixed_step = 1.0f / 60.0f;
    uint32_t max_steps_per_frame = 4;

    Vec3 gravity = {0.0f, -9.81f, 0.0f};
};

// Opaque reference to a body inside the physics world. Holds no
// backend types; callers cannot dereference it. Compare against
// PhysicsHandle{} to test for the invalid sentinel.
struct PhysicsHandle {
    uint32_t index = UINT32_MAX;
    bool valid() const { return index != UINT32_MAX; }
};

// ── Component ───────────────────────────────────────────────────

// Pure-data component. Add to an entity that also has a Transform
// and the body will be created lazily on the next simulate() call.
// Edit fields freely before creation; once `created` flips to true
// the authoring fields are frozen and runtime changes use the
// physics::set_* / apply_* functions.
struct PhysicsBody {
    PhysicsHandle handle{};
    bool created = false;

    BodyType type = BodyType::Dynamic;
    BodyShape shape = BodyShape::Sphere;
    Vec3 dimensions = {0.5f, 0.5f, 0.5f};

    float mass = 1.0f;        // ignored for Static / Kinematic
    float restitution = 0.2f; // 0 = no bounce, 1 = elastic
    float friction = 0.5f;    // 0 = ice, 1 = rubber

    PhysicsLayer layer = PhysicsLayer::Dynamic;
};

// ── Lifecycle ───────────────────────────────────────────────────
// Called by the engine, not by game code.

bool init(const PhysicsConfig& config);
void shutdown();

// ── Per-frame ───────────────────────────────────────────────────
// Runs the fixed-step simulation loop, then syncs body poses back
// into entity Transforms. Engine calls this once per frame from
// begin_frame, after time::tick.
void simulate(float dt, Registry& registry);

// ── Body operations ─────────────────────────────────────────────
// All operations are silent no-ops when the entity is invalid, has
// no PhysicsBody, or whose body has not been created yet.

// Teleport a body to a new pose. For Dynamic bodies this also
// zeroes velocity. For Static bodies this forces a broadphase
// refresh - cheap for a few moves, prefer Kinematic for things
// that move every frame.
void set_transform(EntityID e, const Vec3& position, const Quat& rotation);

void set_linear_velocity(EntityID e, const Vec3& velocity);
void apply_impulse(EntityID e, const Vec3& impulse);

// Kinematic-only. The simulation derives velocity from the delta
// between current pose and target, applied over one fixed step.
void set_kinematic_target(EntityID e, const Vec3& position, const Quat& rotation);

// ── Queries ─────────────────────────────────────────────────────

Vec3 get_linear_velocity(EntityID e);

// Approximate ground check: returns true if the body had a
// downward-facing contact during the previous simulate() call.
// Cheap heuristic; for tight character control prefer a dedicated
// raycast (not yet exposed).
bool is_on_ground(EntityID e);

// ── Entity lifecycle helpers ────────────────────────────────────
// The ECS does not run component destructors, so removing a
// PhysicsBody or destroying its entity will leak the underlying
// body unless one of these is called first. simulate() also runs
// a validate-alive sweep that catches stragglers, but explicit
// cleanup is preferred and asserts in debug if a body was leaked.

void remove_body(EntityID e);

// Equivalent to remove_body(e) followed by registry.destroy_entity(e).
// Prefer this when an entity has a PhysicsBody.
void destroy_entity(Registry& registry, EntityID e);

// ── Diagnostics ─────────────────────────────────────────────────

// Number of live bodies currently registered with the simulation.
// Useful for debug overlays and stress-test caps.
uint32_t body_count();

// World gravity vector, in m/s^2 along each axis. Set at init via
// PhysicsConfig and immutable thereafter. Kuma uses {0, -9.81, 0}
// by default; other modules read this so gameplay gravity stays
// consistent with the simulation.
Vec3 gravity();

// ── Cross-module backend access ─────────────────────────────────
// Returned pointer is opaque - the layout is private to the
// physics implementation. Kuma modules that build on top of Jolt
// (e.g. character controllers) cast it back to the real type
// internally. Game code MUST NOT use this; treat it as a private
// engine-internal handshake.
//
// Pointer is owned by the physics module and remains valid until
// physics::shutdown(). nullptr if physics is not initialized.
void* native_context();

}  // namespace physics
}  // namespace kuma
