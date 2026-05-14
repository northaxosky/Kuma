// Real implementation: drives miniaudio's ma_engine for the device,
// mixer, and 3D spatializer; manages a cache of loaded Sounds keyed
// by path; tracks playing instances with generation-checked handles.

#define MA_IMPLEMENTATION
#include <miniaudio.h>

#include <kuma/audio.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <kuma/asset_format.h>
#include <kuma/log.h>
#include <kuma/transform.h>

namespace kuma::audio {

// ── Public opaque types - real definitions live here ────────────

// Loaded sound data. Owns the .ksound payload bytes + parsed header
// metadata for the lifetime of the audio module. Each playing
// instance constructs its OWN data source (ma_audio_buffer_ref for
// PCM, ma_decoder for compressed) referencing these shared bytes -
// sharing a single data source across instances doesn't work
// because miniaudio data sources are stateful (independent read
// cursors per playback).
struct Sound {
    std::string path;
    std::vector<std::byte> payload;
    asset_format::KSoundHeader header{};
    bool is_pcm = false;
};

namespace {

// ── Per-instance backend record ─────────────────────────────────
// Each playing sound owns its own data source so multiple instances
// of the same Sound can run concurrently with independent positions
// and read cursors. Instances spawned by play_sound / play_sound_at
// have entity == EntityID{} - the validate-alive sweep skips those
// because they have no owning ECS entity to validate against.
struct InstanceRecord {
    ma_sound sound{};
    ma_audio_buffer_ref pcm_ref{};
    ma_decoder decoder{};
    EntityID entity{};
    bool sound_inited = false;
    bool pcm_ref_inited = false;
    bool decoder_inited = false;
    bool in_use = false;
    uint32_t generation = 1;  // bumped on free; matches handle.generation
};

// ── Module state ────────────────────────────────────────────────
struct State {
    ma_engine engine{};
    bool engine_inited = false;

    // Path-keyed cache so load_sound("foo.ksound") returns the same
    // Sound* across calls. Sounds live until shutdown - never unloaded
    // mid-run because that would dangle bytes referenced by playing
    // instances' data sources.
    std::unordered_map<std::string, std::unique_ptr<Sound>> sound_cache;

    // Packed instance records. play_sound / play_sound_at pull from
    // the free list or grow; the validate-alive sweep + finished-sound
    // pruner free entries back. SoundHandle.index points into here,
    // SoundHandle.generation matches the record's generation.
    std::vector<InstanceRecord> instances;
    std::vector<uint32_t> free_slots;
    uint32_t live_count = 0;

    // Listener tracking - used to suppress the spam-warn after the
    // first time a spatial sound plays before set_listener_pose.
    bool listener_set = false;
};

State* g_state = nullptr;

std::vector<std::byte> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize size = f.tellg();
    if (size <= 0) return {};
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

// ── Instance setup ──────────────────────────────────────────────
// Construct the per-instance data source from the Sound's owned
// bytes. The Sound never goes away mid-run, so the byte pointer is
// stable for the instance's lifetime.
bool init_instance_data_source(InstanceRecord& rec, const Sound& sound) {
    const uint8_t* payload_start =
        reinterpret_cast<const uint8_t*>(sound.payload.data())
        + sound.header.payload_offset;

    if (sound.is_pcm) {
        if (ma_audio_buffer_ref_init(ma_format_f32,
                                      sound.header.channels,
                                      payload_start,
                                      sound.header.frame_count,
                                      &rec.pcm_ref) != MA_SUCCESS) {
            return false;
        }
        rec.pcm_ref_inited = true;

        if (ma_sound_init_from_data_source(&g_state->engine,
                                            &rec.pcm_ref,
                                            /*flags=*/0,
                                            /*group=*/nullptr,
                                            &rec.sound) != MA_SUCCESS) {
            return false;
        }
        rec.sound_inited = true;
        return true;
    }

    // Compressed: spin up a fresh decoder per instance so each one
    // has its own decode state. They all read from the same shared
    // payload bytes that the Sound owns.
    ma_decoder_config cfg = ma_decoder_config_init(
        ma_format_f32, sound.header.channels, sound.header.sample_rate);
    if (ma_decoder_init_memory(payload_start,
                                sound.header.payload_size,
                                &cfg,
                                &rec.decoder) != MA_SUCCESS) {
        return false;
    }
    rec.decoder_inited = true;

    if (ma_sound_init_from_data_source(&g_state->engine,
                                        &rec.decoder,
                                        /*flags=*/0,
                                        /*group=*/nullptr,
                                        &rec.sound) != MA_SUCCESS) {
        return false;
    }
    rec.sound_inited = true;
    return true;
}

void uninit_instance(InstanceRecord& rec) {
    if (rec.sound_inited) {
        ma_sound_uninit(&rec.sound);
        rec.sound_inited = false;
    }
    if (rec.decoder_inited) {
        ma_decoder_uninit(&rec.decoder);
        rec.decoder_inited = false;
    }
    if (rec.pcm_ref_inited) {
        ma_audio_buffer_ref_uninit(&rec.pcm_ref);
        rec.pcm_ref_inited = false;
    }
}

// ── Instance allocation ─────────────────────────────────────────

InstanceRecord* allocate_instance(SoundHandle& out_handle) {
    uint32_t index;
    if (!g_state->free_slots.empty()) {
        index = g_state->free_slots.back();
        g_state->free_slots.pop_back();
    } else {
        index = static_cast<uint32_t>(g_state->instances.size());
        g_state->instances.emplace_back();
        g_state->instances.back().generation = 1;
    }
    InstanceRecord& rec = g_state->instances[index];
    rec.in_use = true;
    out_handle = SoundHandle{index, rec.generation};
    ++g_state->live_count;
    return &rec;
}

void free_instance(uint32_t index) {
    if (index >= g_state->instances.size()) return;
    InstanceRecord& rec = g_state->instances[index];
    if (!rec.in_use) return;
    uninit_instance(rec);
    rec.in_use = false;
    rec.entity = EntityID{};
    // Bump generation so any stale handles pointing at this slot
    // are detected as invalid by future stop_sound calls.
    ++rec.generation;
    if (rec.generation == 0) rec.generation = 1;
    g_state->free_slots.push_back(index);
    --g_state->live_count;
}

InstanceRecord* lookup_instance(SoundHandle handle) {
    if (!handle.valid()) return nullptr;
    if (handle.index >= g_state->instances.size()) return nullptr;
    InstanceRecord& rec = g_state->instances[handle.index];
    if (!rec.in_use) return nullptr;
    if (rec.generation != handle.generation) return nullptr;
    return &rec;
}

}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────

bool init() {
    if (g_state != nullptr) {
        kuma::log::warn("audio::init called twice; ignoring");
        return true;
    }

    auto state = std::make_unique<State>();

    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &state->engine) != MA_SUCCESS) {
        kuma::log::error("audio::init: ma_engine_init failed (no audio device?)");
        return false;
    }
    state->engine_inited = true;

    g_state = state.release();
    kuma::log::info("Audio initialized: device sample rate %u Hz, %u channels",
                     ma_engine_get_sample_rate(&g_state->engine),
                     ma_engine_get_channels(&g_state->engine));
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;

    // Strict destruction order (per the duck pass):
    //   1. Uninit every playing instance (data source then ma_sound)
    //   2. Drop the Sound cache (just bytes; nothing miniaudio holds)
    //   3. Uninit the engine
    for (auto& rec : g_state->instances) {
        uninit_instance(rec);
        rec.in_use = false;
    }
    g_state->instances.clear();
    g_state->free_slots.clear();
    g_state->live_count = 0;

    g_state->sound_cache.clear();

    if (g_state->engine_inited) {
        ma_engine_uninit(&g_state->engine);
        g_state->engine_inited = false;
    }

    delete g_state;
    g_state = nullptr;
    kuma::log::info("Audio shut down");
}

// ── Sound loading ───────────────────────────────────────────────

const Sound* load_sound(const char* path) {
    if (g_state == nullptr) return nullptr;
    if (path == nullptr) return nullptr;

    auto it = g_state->sound_cache.find(path);
    if (it != g_state->sound_cache.end()) {
        return it->second.get();
    }

    std::vector<std::byte> bytes = read_file(path);
    if (bytes.empty()) {
        kuma::log::error("audio::load_sound: failed to read '%s'", path);
        return nullptr;
    }

    asset_format::KSoundHeader header{};
    auto parse = asset_format::parse_ksound_header(bytes.data(), bytes.size(), header);
    if (parse != asset_format::ParseResult::Ok) {
        kuma::log::error("audio::load_sound: parse failed for '%s' (code %d)",
                          path, static_cast<int>(parse));
        return nullptr;
    }

    auto sound = std::make_unique<Sound>();
    sound->path = path;
    sound->payload = std::move(bytes);
    sound->header = header;
    sound->is_pcm = (header.format == asset_format::kAudioFormatPcmF32);

    Sound* raw = sound.get();
    g_state->sound_cache.emplace(path, std::move(sound));
    kuma::log::info("Sound loaded: %s (%u Hz, %u ch, %s)",
                     path, header.sample_rate, header.channels,
                     raw->is_pcm ? "PCM" : "compressed");
    return raw;
}

// ── Playback ────────────────────────────────────────────────────

namespace {

SoundHandle play_internal(const Sound* sound, bool spatial, const Vec3& position) {
    if (g_state == nullptr || sound == nullptr) return {};

    SoundHandle handle{};
    InstanceRecord* rec = allocate_instance(handle);

    if (!init_instance_data_source(*rec, *sound)) {
        kuma::log::error("audio: failed to construct data source for %s",
                          sound->path.c_str());
        free_instance(handle.index);
        return {};
    }

    // Force the spatialization state explicitly - silently-non-spatial
    // 3D plays would be a nightmare to debug.
    ma_sound_set_spatialization_enabled(&rec->sound, spatial ? MA_TRUE : MA_FALSE);
    if (spatial) {
        ma_sound_set_position(&rec->sound, position.x, position.y, position.z);
        if (!g_state->listener_set) {
            kuma::log::warn("audio: spatial sound played before set_listener_pose; "
                             "listener defaults to origin facing -Z");
        }
    }

    if (ma_sound_start(&rec->sound) != MA_SUCCESS) {
        kuma::log::error("audio: ma_sound_start failed for %s", sound->path.c_str());
        free_instance(handle.index);
        return {};
    }
    return handle;
}

}  // namespace

SoundHandle play_sound(const Sound* sound) {
    return play_internal(sound, /*spatial=*/false, {});
}

SoundHandle play_sound_at(const Sound* sound, const Vec3& world_position) {
    return play_internal(sound, /*spatial=*/true, world_position);
}

void stop_sound(SoundHandle handle) {
    if (g_state == nullptr) return;
    InstanceRecord* rec = lookup_instance(handle);
    if (rec == nullptr) return;
    free_instance(handle.index);
}

// ── Global controls ─────────────────────────────────────────────

void set_master_volume(float volume) {
    if (g_state == nullptr) return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    ma_engine_set_volume(&g_state->engine, volume);
}

void set_listener_pose(const Vec3& position, const Vec3& forward, const Vec3& up) {
    if (g_state == nullptr) return;
    constexpr ma_uint32 kListener = 0;  // single-listener for now
    ma_engine_listener_set_position(&g_state->engine, kListener, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&g_state->engine, kListener, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&g_state->engine, kListener, up.x, up.y, up.z);
    g_state->listener_set = true;
}

// ── Per-frame ───────────────────────────────────────────────────

namespace {

void sync_component(EntityID entity, AudioSource& src, const Transform* transform) {
    if (g_state == nullptr) return;

    if (!src.created) {
        if (src.sound == nullptr) return;
        if (src.spatial && transform == nullptr) {
            kuma::log::warn(
                "audio: spatial AudioSource on entity %u is missing Transform; skipping",
                entity.id);
            return;
        }

        SoundHandle handle{};
        InstanceRecord* rec = allocate_instance(handle);

        if (!init_instance_data_source(*rec, *src.sound)) {
            kuma::log::error("audio: failed to construct data source for AudioSource on entity %u",
                              entity.id);
            free_instance(handle.index);
            return;
        }

        ma_sound_set_spatialization_enabled(&rec->sound, src.spatial ? MA_TRUE : MA_FALSE);
        if (src.spatial && transform != nullptr) {
            const Vec3& p = transform->position();
            ma_sound_set_position(&rec->sound, p.x, p.y, p.z);
        }
        ma_sound_set_volume(&rec->sound, src.volume);
        ma_sound_set_looping(&rec->sound, src.looping ? MA_TRUE : MA_FALSE);

        if (src.play_on_create) {
            if (ma_sound_start(&rec->sound) != MA_SUCCESS) {
                kuma::log::error("audio: ma_sound_start failed for AudioSource on entity %u",
                                  entity.id);
                free_instance(handle.index);
                return;
            }
        }

        src.handle = handle;
        src.created = true;
        src.playing = src.play_on_create;

        // Tag the backing instance with its owning entity so the
        // validate-alive sweep can clean it up if the entity is
        // destroyed without going through audio::remove_source.
        rec->entity = entity;
        return;
    }

    InstanceRecord* rec = lookup_instance(src.handle);
    if (rec == nullptr) {
        src.playing = false;
        return;
    }

    if (src.spatial && transform != nullptr) {
        const Vec3& p = transform->position();
        ma_sound_set_position(&rec->sound, p.x, p.y, p.z);
    }
    ma_sound_set_volume(&rec->sound, src.volume);
    ma_sound_set_looping(&rec->sound, src.looping ? MA_TRUE : MA_FALSE);

    src.playing = ma_sound_is_playing(&rec->sound) == MA_TRUE;
}

}  // namespace

void simulate(Registry& registry) {
    if (g_state == nullptr) return;

    // Validate-alive sweep: if a component-backed instance's entity
    // was destroyed (or its AudioSource removed) without going through
    // audio::remove_source / audio::destroy_entity, the instance
    // would otherwise leak - looping sounds especially since
    // ma_sound_at_end never returns true for them. Index-based walk
    // mirrors the physics + character pattern.
    for (uint32_t i = 0; i < g_state->instances.size(); ++i) {
        InstanceRecord& rec = g_state->instances[i];
        if (!rec.in_use) continue;
        // Fire-and-forget instances have no owning entity; they're
        // pruned by the ma_sound_at_end check below.
        if (rec.entity.id == 0 && rec.entity.generation == 0) continue;
        if (!registry.is_valid(rec.entity) || !registry.has<AudioSource>(rec.entity)) {
            free_instance(i);
        }
    }

    // Prune one-shots that finished naturally. Looping sounds never
    // hit ma_sound_at_end so they're correctly preserved here.
    for (uint32_t i = 0; i < g_state->instances.size(); ++i) {
        InstanceRecord& rec = g_state->instances[i];
        if (!rec.in_use) continue;
        if (rec.sound_inited && ma_sound_at_end(&rec.sound)) {
            free_instance(i);
        }
    }

    for (auto [entity, src, transform] : registry.view<AudioSource, Transform>()) {
        if (!src.spatial && src.created) continue;
        sync_component(entity, src, &transform);
    }

    for (auto [entity, src] : registry.view<AudioSource>()) {
        if (src.spatial) continue;
        sync_component(entity, src, nullptr);
    }
}

// ── Lifecycle helpers (component) ───────────────────────────────

void remove_source(Registry& registry, EntityID e) {
    if (g_state == nullptr) return;
    AudioSource* src = registry.try_get<AudioSource>(e);
    if (src == nullptr) return;
    if (src->created) {
        InstanceRecord* rec = lookup_instance(src->handle);
        if (rec != nullptr) {
            free_instance(src->handle.index);
        }
        src->handle = {};
        src->created = false;
        src->playing = false;
    }
}

void destroy_entity(Registry& registry, EntityID e) {
    remove_source(registry, e);
    registry.destroy_entity(e);
}

uint32_t playing_count() {
    if (g_state == nullptr) return 0;
    return g_state->live_count;
}

}  // namespace kuma::audio
