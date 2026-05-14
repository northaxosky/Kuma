// Real implementation: drives miniaudio's ma_engine for the device,
// mixer, and 3D spatializer; manages a cache of loaded Sounds keyed
// by path; tracks playing instances with generation-checked handles.
// AudioSource component sync + simulate live in the next commit.

#define MA_IMPLEMENTATION
#include <miniaudio.h>

#include <kuma/audio.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <kuma/asset_format.h>
#include <kuma/log.h>

namespace kuma::audio {

// ── Public opaque types - real definitions live here ────────────

// Loaded sound data. Owns its payload bytes for the lifetime of
// the audio module, so the ma_audio_buffer / ma_decoder we construct
// from those bytes always reads from valid memory. Template_sound
// is the "factory" that ma_sound_init_copy clones for each playback
// instance; it never plays itself.
struct Sound {
    std::string path;
    std::vector<std::byte> payload;
    bool is_pcm = false;

    ma_audio_buffer audio_buffer{};
    ma_decoder decoder{};
    ma_sound template_sound{};

    bool audio_buffer_inited = false;
    bool decoder_inited = false;
    bool template_inited = false;
};

namespace {

// ── Per-instance backend record ─────────────────────────────────
struct InstanceRecord {
    ma_sound sound{};
    bool in_use = false;
    bool sound_inited = false;
    uint32_t generation = 1;  // bump on free; matches handle.generation
};

// ── Module state ────────────────────────────────────────────────
struct State {
    ma_engine engine{};
    bool engine_inited = false;

    // Path-keyed cache so load_sound("foo.ksound") returns the same
    // Sound* across calls. Sounds live until shutdown - never unloaded
    // mid-run because that would dangle ma_audio_buffer / ma_decoder
    // pointers held by playing instances.
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

inline ma_vec3f to_ma_vec3(const Vec3& v) { return ma_vec3f{v.x, v.y, v.z}; }

// ── Sound loading ───────────────────────────────────────────────

bool load_pcm(Sound* sound, const asset_format::KSoundHeader& header) {
    const uint8_t* payload_start = reinterpret_cast<const uint8_t*>(sound->payload.data())
                                    + header.payload_offset;

    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
        ma_format_f32,
        header.channels,
        header.frame_count,
        payload_start,
        nullptr);

    if (ma_audio_buffer_init(&cfg, &sound->audio_buffer) != MA_SUCCESS) {
        kuma::log::error("audio: ma_audio_buffer_init failed for %s", sound->path.c_str());
        return false;
    }
    sound->audio_buffer_inited = true;

    if (ma_sound_init_from_data_source(&g_state->engine,
                                        &sound->audio_buffer,
                                        /*flags=*/0,
                                        /*group=*/nullptr,
                                        &sound->template_sound) != MA_SUCCESS) {
        kuma::log::error("audio: ma_sound_init_from_data_source(PCM) failed for %s",
                          sound->path.c_str());
        return false;
    }
    sound->template_inited = true;
    return true;
}

bool load_compressed(Sound* sound, const asset_format::KSoundHeader& header) {
    const uint8_t* payload_start = reinterpret_cast<const uint8_t*>(sound->payload.data())
                                    + header.payload_offset;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, header.channels, header.sample_rate);

    if (ma_decoder_init_memory(payload_start,
                                header.payload_size,
                                &cfg,
                                &sound->decoder) != MA_SUCCESS) {
        kuma::log::error("audio: ma_decoder_init_memory failed for %s", sound->path.c_str());
        return false;
    }
    sound->decoder_inited = true;

    if (ma_sound_init_from_data_source(&g_state->engine,
                                        &sound->decoder,
                                        /*flags=*/0,
                                        /*group=*/nullptr,
                                        &sound->template_sound) != MA_SUCCESS) {
        kuma::log::error("audio: ma_sound_init_from_data_source(decoder) failed for %s",
                          sound->path.c_str());
        return false;
    }
    sound->template_inited = true;
    return true;
}

void destroy_sound(Sound* sound) {
    if (sound->template_inited) {
        ma_sound_uninit(&sound->template_sound);
        sound->template_inited = false;
    }
    if (sound->decoder_inited) {
        ma_decoder_uninit(&sound->decoder);
        sound->decoder_inited = false;
    }
    if (sound->audio_buffer_inited) {
        ma_audio_buffer_uninit(&sound->audio_buffer);
        sound->audio_buffer_inited = false;
    }
}

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
    if (rec.sound_inited) {
        ma_sound_uninit(&rec.sound);
        rec.sound_inited = false;
    }
    rec.in_use = false;
    // Bump generation so any stale handles pointing at this slot
    // are detected as invalid by future stop_sound calls.
    ++rec.generation;
    if (rec.generation == 0) rec.generation = 1;
    g_state->free_slots.push_back(index);
    --g_state->live_count;
}

// Returns nullptr if the handle is stale or already freed. Used by
// stop_sound and any future "is this handle still alive" query.
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
    //   1. Stop + uninit every playing instance
    //   2. Uninit every Sound's template + data source
    //   3. Uninit the engine
    // Reversing this would have miniaudio dereference freed memory.
    for (auto& rec : g_state->instances) {
        if (rec.sound_inited) {
            ma_sound_uninit(&rec.sound);
            rec.sound_inited = false;
        }
        rec.in_use = false;
    }
    g_state->instances.clear();
    g_state->free_slots.clear();
    g_state->live_count = 0;

    for (auto& kv : g_state->sound_cache) {
        destroy_sound(kv.second.get());
    }
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

    // Cache hit: return the same Sound* we returned last time so the
    // caller can rely on pointer-equality and the underlying data
    // stays shared across all instances.
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
    sound->is_pcm = (header.format == asset_format::kAudioFormatPcmF32);

    const bool ok = sound->is_pcm ? load_pcm(sound.get(), header)
                                   : load_compressed(sound.get(), header);
    if (!ok) {
        destroy_sound(sound.get());
        return nullptr;
    }

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
    if (!sound->template_inited) return {};

    SoundHandle handle{};
    InstanceRecord* rec = allocate_instance(handle);

    if (ma_sound_init_copy(&g_state->engine,
                            const_cast<ma_sound*>(&sound->template_sound),
                            /*flags=*/0,
                            /*group=*/nullptr,
                            &rec->sound) != MA_SUCCESS) {
        kuma::log::error("audio: ma_sound_init_copy failed for %s", sound->path.c_str());
        free_instance(handle.index);
        return {};
    }
    rec->sound_inited = true;

    // Force the spatialization state explicitly - the template's
    // setting is not inherited reliably and silently-non-spatial
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
    if (rec == nullptr) return;  // stale, already finished, or invalid - safe no-op
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

void simulate(Registry& /*registry*/) {
    if (g_state == nullptr) return;

    // Prune one-shots that finished naturally. Walk the records by
    // index so later passes (validate-alive on AudioSource components)
    // can also work index-based and not race the iteration.
    for (uint32_t i = 0; i < g_state->instances.size(); ++i) {
        InstanceRecord& rec = g_state->instances[i];
        if (!rec.in_use) continue;
        if (rec.sound_inited && ma_sound_at_end(&rec.sound)) {
            free_instance(i);
        }
    }

    // AudioSource component sync arrives in the next commit.
}

// ── Lifecycle helpers (component) ───────────────────────────────

void remove_source(Registry& /*registry*/, EntityID /*e*/) {
    // Real implementation arrives with the AudioSource sync commit.
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

