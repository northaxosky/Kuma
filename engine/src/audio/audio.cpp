// Skeleton implementation. Defines the public API symbols so the
// linker is happy and game code (and tests) can compile against
// kuma/audio.h. Real ma_engine instantiation, SoundStore, simulate
// loop, and AudioSource sync arrive in the next commits. The
// MA_IMPLEMENTATION include here proves miniaudio links cleanly.

#define MA_IMPLEMENTATION
#include <miniaudio.h>

#include <kuma/audio.h>

#include <kuma/log.h>

namespace kuma::audio {

namespace {
bool g_initialized = false;
}

bool init() {
    if (g_initialized) {
        kuma::log::warn("audio::init called twice; ignoring");
        return true;
    }
    g_initialized = true;
    kuma::log::info("Audio initialized (skeleton - playback not yet wired)");
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    g_initialized = false;
    kuma::log::info("Audio shut down");
}

const Sound* load_sound(const char* /*path*/) {
    return nullptr;
}

SoundHandle play_sound(const Sound* /*sound*/) {
    return {};
}

SoundHandle play_sound_at(const Sound* /*sound*/, const Vec3& /*world_position*/) {
    return {};
}

void stop_sound(SoundHandle /*handle*/) {}

void set_master_volume(float /*volume*/) {}

void set_listener_pose(const Vec3& /*position*/,
                        const Vec3& /*forward*/,
                        const Vec3& /*up*/) {}

void simulate(Registry& /*registry*/) {}

void remove_source(Registry& /*registry*/, EntityID /*e*/) {}

void destroy_entity(Registry& registry, EntityID e) {
    remove_source(registry, e);
    registry.destroy_entity(e);
}

uint32_t playing_count() {
    return 0;
}

}  // namespace kuma::audio

