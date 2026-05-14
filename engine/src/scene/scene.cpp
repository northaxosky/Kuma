#include <kuma/scene.h>

#include <kuma/log.h>

// Skeleton implementation. Public API is fully declared so game
// code and tests can link, but the real .kscene reader, mesh
// resolution, entity spawning, and despawn logic arrive in the
// next commits.

namespace kuma::scene {

namespace {
bool g_initialized = false;
ResourceManager* g_resources = nullptr;
}

bool init(ResourceManager& resources) {
    if (g_initialized) {
        kuma::log::warn("scene::init called twice; ignoring");
        return true;
    }
    g_resources = &resources;
    g_initialized = true;
    kuma::log::info("Scene module initialized (skeleton - loading not yet wired)");
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    g_resources = nullptr;
    g_initialized = false;
    kuma::log::info("Scene module shut down");
}

const Scene* load(const char* /*path*/) {
    return nullptr;
}

SceneInstance spawn(const Scene* /*scene*/, Registry& /*registry*/) {
    return {};
}

SceneInstance load_and_spawn(const char* /*path*/, Registry& /*registry*/) {
    return {};
}

void despawn(Registry& /*registry*/, uint32_t /*scene_id*/) {}

uint32_t loaded_scene_count() {
    return 0;
}

}  // namespace kuma::scene
