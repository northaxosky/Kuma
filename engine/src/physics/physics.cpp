#include <kuma/physics.h>

#include <algorithm>
#include <memory>
#include <thread>

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <kuma/log.h>

#include "physics/jolt_globals.h"
#include "physics/layers.h"

namespace kuma::physics {

namespace {

// Module state. File-local statics so engine.cpp can drive the
// lifecycle without needing to thread a context through every call.
struct State {
    PhysicsConfig config{};
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;
    std::unique_ptr<detail::KumaBPLayerInterface> bp_layer_interface;
    std::unique_ptr<detail::KumaObjectVsBPLayerFilter> object_vs_bp_filter;
    std::unique_ptr<detail::KumaObjectPairFilter> object_pair_filter;
    std::unique_ptr<JPH::PhysicsSystem> system;
};

State* g_state = nullptr;

// std::thread::hardware_concurrency() can return 0 on weird
// platforms or in containers, and even when it returns 1 the naive
// "minus one" would underflow. Always reserve at least one worker.
uint32_t recommended_worker_count() {
    const uint32_t hw = std::thread::hardware_concurrency();
    if (hw <= 1) return 1;
    return hw - 1;
}

}  // namespace

bool init(const PhysicsConfig& config) {
    if (g_state != nullptr) {
        kuma::log::warn("physics::init called twice; ignoring");
        return true;
    }

    detail::ensure_jolt_globals_initialized();

    auto state = std::make_unique<State>();
    state->config = config;

    state->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(
        static_cast<JPH::uint>(config.temp_allocator_bytes));

    const uint32_t threads = recommended_worker_count();
    state->job_system = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(threads));

    state->bp_layer_interface = std::make_unique<detail::KumaBPLayerInterface>();
    state->object_vs_bp_filter = std::make_unique<detail::KumaObjectVsBPLayerFilter>();
    state->object_pair_filter = std::make_unique<detail::KumaObjectPairFilter>();

    state->system = std::make_unique<JPH::PhysicsSystem>();
    state->system->Init(
        config.max_bodies,
        /*num_body_mutexes=*/0,
        config.max_body_pairs,
        config.max_contact_constraints,
        *state->bp_layer_interface,
        *state->object_vs_bp_filter,
        *state->object_pair_filter);

    state->system->SetGravity(
        JPH::Vec3(config.gravity.x, config.gravity.y, config.gravity.z));

    g_state = state.release();

    kuma::log::info(
        "Physics initialized: %u worker threads, %.1f MB temp allocator, gravity=(%.2f, %.2f, %.2f)",
        threads,
        config.temp_allocator_bytes / (1024.0 * 1024.0),
        config.gravity.x, config.gravity.y, config.gravity.z);
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;

    delete g_state;
    g_state = nullptr;

    kuma::log::info("Physics shut down");
}

void simulate(float /*dt*/, Registry& /*registry*/) {
    // Body management and the simulation loop land in the next
    // commit. The PhysicsSystem is alive and ready - this is just
    // intentionally not driving it yet so the diff stays scoped.
}

void set_transform(EntityID /*e*/, const Vec3& /*position*/, const Quat& /*rotation*/) {}
void set_linear_velocity(EntityID /*e*/, const Vec3& /*velocity*/) {}
void apply_impulse(EntityID /*e*/, const Vec3& /*impulse*/) {}
void set_kinematic_target(EntityID /*e*/, const Vec3& /*position*/, const Quat& /*rotation*/) {}

Vec3 get_linear_velocity(EntityID /*e*/) {
    return {};
}

bool is_on_ground(EntityID /*e*/) {
    return false;
}

void remove_body(EntityID /*e*/) {}

void destroy_entity(Registry& registry, EntityID e) {
    remove_body(e);
    registry.destroy_entity(e);
}

uint32_t body_count() {
    if (g_state == nullptr) return 0;
    return g_state->system->GetNumBodies();
}

}  // namespace kuma::physics
