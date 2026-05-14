#pragma once

// ── Kuma Audio ──────────────────────────────────────────────────
// 3D-positional audio playback. Built on top of miniaudio's
// ma_engine: the audio module owns the device + mixer + spatializer
// thread; the public API only exposes opaque Sound + SoundHandle
// types so callers never see miniaudio headers.
//
// Asset pipeline: kuma-bake converts .wav/.mp3/.ogg/.flac source
// files into the .ksound binary format. Engine loads .ksound only -
// short SFX are baked to uncompressed PCM for zero-decode-at-load,
// long music can be baked as compressed-passthrough so disk size
// stays small.
//
// Threading: every kuma::audio:: function must be called from the
// main thread. miniaudio runs its own audio device thread for the
// mixer; we never touch it directly.

#include <cstdint>

#include <kuma/ecs.h>
#include <kuma/math.h>

namespace kuma {

class Registry;
class Transform;

namespace audio {

// Opaque "loaded sound data" - the template that play_sound copies
// from. Owned by the audio module from load_sound() until shutdown().
// Multiple play_sound() calls on the same Sound* share its data.
struct Sound;

// Opaque reference to a currently-playing instance. Becomes invalid
// once the sound finishes naturally (one-shots) or is stopped. Stale
// handles are detected via an internal generation counter, so calling
// stop_sound() on a finished handle is a safe no-op (it never stops
// an unrelated newer sound that happens to occupy the same slot).
struct SoundHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;
    bool valid() const { return index != UINT32_MAX; }
};

// ── Lifecycle ───────────────────────────────────────────────────
// Engine drives these. Independent of physics/character - audio can
// initialize at any point during engine startup after the window.
bool init();
void shutdown();

// ── Sound loading ───────────────────────────────────────────────
// Loads a .ksound asset. Returns nullptr on missing file, bad
// header, or backend init failure. Subsequent loads of the same
// canonical path return the same pointer (path-based cache) so the
// underlying decoded data is shared.
const Sound* load_sound(const char* path);

// ── Immediate playback ──────────────────────────────────────────
// Fire-and-forget. The returned handle is only useful for early
// stop_sound() calls; otherwise game code can ignore it.

// Play without 3D positioning. Used for UI clicks, music, voice.
SoundHandle play_sound(const Sound* sound);

// Play at a world position with full distance attenuation +
// stereo panning relative to the current listener pose.
SoundHandle play_sound_at(const Sound* sound, const Vec3& world_position);

// Idempotent. No-op on invalid / stale / already-finished handles.
void stop_sound(SoundHandle handle);

// ── Global controls ─────────────────────────────────────────────
// Master volume, 0..1 linear gain. Clamped if out of range.
void set_master_volume(float volume);

// Listener pose used for 3D spatial math. Game code calls this once
// per frame, typically with camera position + camera forward and a
// world-up vector (NOT camera-local up - that would tilt the
// perceived world when the camera pitches).
void set_listener_pose(const Vec3& position, const Vec3& forward, const Vec3& up);

// ── Per-frame ───────────────────────────────────────────────────
// Reconciles AudioSource components with the backend: creates new
// instances, syncs volume / looping / spatial-position, prunes
// finished one-shots, validates entity liveness.
void simulate(Registry& registry);

// ── Component lifecycle helpers ─────────────────────────────────
// AudioSource is a pure-data ECS component; ECS does not run
// destructors on entity destroy. simulate() runs a validate-alive
// sweep that catches stragglers, but explicit cleanup is preferred.
void remove_source(Registry& registry, EntityID e);
void destroy_entity(Registry& registry, EntityID e);

// ── Diagnostics ─────────────────────────────────────────────────
// Number of currently-playing sound instances (one-shot + ECS).
uint32_t playing_count();

}  // namespace audio

// ── AudioSource ─────────────────────────────────────────────────
// Pure-data component. Pairs with a Transform when spatial=true;
// non-spatial sources (music, UI) need only the AudioSource itself.
//
// Mutable fields (volume, looping) are synced into the running
// instance every frame, so game code can adjust them at runtime.
// Changing the `sound` pointer or `spatial` flag after creation
// requires removing + re-adding the component to take effect.
struct AudioSource {
    const audio::Sound* sound = nullptr;
    float volume = 1.0f;
    bool looping = false;
    bool play_on_create = true;
    bool spatial = true;

    // Read-only state (audio::simulate writes)
    bool playing = false;

    // Internal
    audio::SoundHandle handle{};
    bool created = false;
};

}  // namespace kuma
