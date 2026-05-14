#pragma once

// ── Physics native context ──────────────────────────────────────
// Bundle of Jolt-side handles that other engine modules (notably
// character) need to drive their own simulations against the
// shared PhysicsSystem. Lives behind the opaque void* returned by
// physics::native_context() in the public header; consumers cast
// to this struct via this internal-only header.
//
// Every pointer is owned by the physics module and valid only
// between physics::init() and physics::shutdown(). Consumers must
// run their teardown before physics::shutdown() returns.

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace kuma::physics::detail {

struct PhysicsNativeContext {
    JPH::PhysicsSystem* system = nullptr;
    JPH::TempAllocator* temp_allocator = nullptr;
    JPH::BroadPhaseLayerInterface* broad_phase_layer_interface = nullptr;
    JPH::ObjectVsBroadPhaseLayerFilter* object_vs_broad_phase_filter = nullptr;
    JPH::ObjectLayerPairFilter* object_layer_pair_filter = nullptr;
};

}  // namespace kuma::physics::detail
