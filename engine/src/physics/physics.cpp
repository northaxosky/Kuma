#include <kuma/physics.h>

#include <kuma/log.h>

#include "physics/jolt_globals.h"

// Skeleton implementation. The public API is fully declared so
// game code and tests can link against it, but the real Jolt
// instantiation, body management, and simulation loop arrive in
// the next commit. For now every entry point is a logged no-op
// and queries return safe defaults.

namespace kuma::physics {

namespace {
bool g_initialized = false;
}

bool init(const PhysicsConfig& /*config*/) {
    if (g_initialized) {
        kuma::log::warn("physics::init called twice; ignoring");
        return true;
    }
    detail::ensure_jolt_globals_initialized();
    g_initialized = true;
    kuma::log::info("Physics initialized (skeleton - simulation not yet wired)");
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    kuma::log::info("Physics shut down");
}

void simulate(float /*dt*/, Registry& /*registry*/) {
    // no-op until the body store and simulation loop land
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
    return 0;
}

}  // namespace kuma::physics
