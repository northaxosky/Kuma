#pragma once

// ── Kuma Character ──────────────────────────────────────────────
// Kinematic FPS character controller. Built on top of Jolt's
// CharacterVirtual: a capsule that handles step-and-slide collision,
// slope detection, auto-step over short obstacles, ground state
// detection, and pushes dynamic bodies it touches without being
// bounced around by them.
//
// Add a Character component (plus a Transform) to an entity; on
// the next character::simulate() the controller is created. Game
// code writes per-frame inputs (wish_direction, wish_jump, yaw);
// the simulation reads them, runs the kinematic step, and writes
// back the resulting position into the Transform.
//
// World scale: 1 unit = 1 meter, same as physics. Capsule total
// height = 2 * (half_height + radius). Eye height for first-person
// rendering is independent (set on the camera controller).
//
// Threading: not thread-safe. Call every function on the main
// thread, between frames.

#include <cstdint>

#include <kuma/ecs.h>
#include <kuma/math.h>
#include <kuma/physics.h>

namespace kuma {

class Registry;

// ── Component ───────────────────────────────────────────────────
// Pure-data component. Pairs with a Transform on the same entity.
// Authoring fields are frozen once the controller is created;
// runtime state lives in the Internal section below.
struct Character {
    // ── Authoring ───────────────────────────────────────────
    float radius = 0.35f;
    float half_height = 0.55f;          // total capsule height = 2 * (half_height + radius)
    float walk_speed = 5.0f;            // m/s on the ground
    float jump_speed = 5.5f;            // m/s vertical impulse on jump
    float max_slope_radians = 0.78f;    // ~45 degrees - steeper counts as wall
    float step_height = 0.3f;           // auto-step over obstacles up to this tall
    float air_acceleration = 12.0f;     // m/s^2 while airborne
    float mass = 70.0f;                 // for pushing dynamics
    physics::PhysicsLayer layer = physics::PhysicsLayer::Player;

    // ── Per-frame inputs (game writes; simulate consumes) ───
    // wish_direction is normalized and projected to the horizontal
    // plane internally - any non-unit / non-horizontal vector is
    // accepted. Both inputs are reset to defaults at the start of
    // every simulate() call so stale game-code writes can't latch.
    Vec3 wish_direction = {0, 0, 0};
    bool wish_jump = false;

    // Authoritative yaw for the capsule. simulate() writes
    // Transform.rotation = quat-from-yaw to keep the entity's
    // visible orientation in sync. External writes to
    // Transform.rotation on Character entities are overwritten.
    float yaw_radians = 0.0f;

    // ── Read-only state (simulate writes) ───────────────────
    bool on_ground = false;
    bool on_steep = false;
    Vec3 velocity = {0, 0, 0};

    // ── Internal ─────────────────────────────────────────────
    physics::PhysicsHandle handle{};
    bool created = false;
};

namespace character {

// ── Lifecycle ───────────────────────────────────────────────────
// Engine drives these. Must be called AFTER physics::init / BEFORE
// physics::shutdown so the underlying CharacterVirtual instances
// have a valid PhysicsSystem to collide against.
bool init();
void shutdown();

// ── Per-frame ───────────────────────────────────────────────────
// Reads each Character + Transform, runs the fixed-step controller
// loop, syncs results back. Game code typically calls this AFTER
// FpsCameraController::update (which writes wish_*) and BEFORE
// physics::simulate (so character pushes feed into the same step).
void simulate(float dt, Registry& registry);

// ── Lifecycle helpers ───────────────────────────────────────────
// The ECS does not run component destructors, so removing a
// Character or destroying its entity will leak the underlying
// CharacterVirtual unless one of these is called first. simulate()
// also runs a validate-alive sweep that catches stragglers.

void remove_character(Registry& registry, EntityID e);
void destroy_entity(Registry& registry, EntityID e);

// ── Diagnostics ─────────────────────────────────────────────────
uint32_t character_count();

}  // namespace character
}  // namespace kuma
