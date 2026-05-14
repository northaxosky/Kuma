#include <kuma/character.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <kuma/log.h>

#include "physics/native_context.h"

// Skeleton implementation. Public API is fully declared so game
// code and tests can link, but the real CharacterStore + simulate
// loop arrive in the next commit. Including the CharacterVirtual
// header here proves Jolt's character module compiles and links
// against the engine.

namespace kuma::character {

namespace {
bool g_initialized = false;
}

bool init() {
    if (g_initialized) {
        kuma::log::warn("character::init called twice; ignoring");
        return true;
    }
    if (kuma::physics::native_context() == nullptr) {
        kuma::log::error(
            "character::init requires physics to be initialized first");
        return false;
    }
    g_initialized = true;
    kuma::log::info("Character module initialized (skeleton - simulation not yet wired)");
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    kuma::log::info("Character module shut down");
}

void simulate(float /*dt*/, Registry& /*registry*/) {
    // no-op until the controller store and simulation loop land
}

void remove_character(Registry& /*registry*/, EntityID /*e*/) {}

void destroy_entity(Registry& registry, EntityID e) {
    remove_character(registry, e);
    registry.destroy_entity(e);
}

uint32_t character_count() {
    return 0;
}

}  // namespace kuma::character
