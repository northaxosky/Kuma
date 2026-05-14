#include <kuma/scene.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <kuma/asset_format.h>
#include <kuma/log.h>
#include <kuma/resource_manager.h>
#include <kuma/transform.h>

namespace kuma::scene {

// ── Public opaque type - real definition lives here ─────────────
//
// Each Scene owns its raw .kscene bytes (so the parsed offsets stay
// valid) plus the absolute paths of every referenced mesh resolved
// once at load time. spawn() walks the cached node table to create
// entities; the parsed mesh paths are handed straight to
// ResourceManager which keeps its own cache.
struct Scene {
    std::filesystem::path origin_dir;     // .kscene's parent directory
    std::vector<std::byte> raw;            // entire .kscene file
    asset_format::KSceneHeader header{};
    std::vector<std::filesystem::path> mesh_paths;      // resolved absolute paths
    std::vector<std::filesystem::path> material_paths;  // resolved absolute paths
};

namespace {

// ── Module state ────────────────────────────────────────────────
struct State {
    ResourceManager* resources = nullptr;
    // Path-keyed cache of loaded scenes. Key uses
    // std::filesystem::weakly_canonical so foo.kscene, ./foo.kscene,
    // and a path with redundant separators all resolve to the same
    // Scene*.
    std::unordered_map<std::string, std::unique_ptr<Scene>> cache;
    uint32_t next_scene_id = 1;  // 0 reserved for "invalid SceneInstance"
};

State* g_state = nullptr;

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize size = f.tellg();
    if (size <= 0) return {};
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

// Translate a relative path (mesh or material) stored in the
// .kscene's string table into an absolute path on disk. Rejects
// absolute paths and any "../" traversal so a maliciously-crafted
// .kscene can't point at arbitrary files outside its directory tree.
std::optional<std::filesystem::path> resolve_relative_path(
    const std::filesystem::path& origin_dir,
    std::string_view relative
) {
    std::filesystem::path rel(relative);
    if (rel.is_absolute()) return std::nullopt;
    for (const auto& segment : rel) {
        if (segment == "..") return std::nullopt;
    }
    std::error_code ec;
    auto candidate = origin_dir / rel;
    auto canonical = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return std::nullopt;
    return canonical;
}

// Decompose a column-major 4x4 world matrix into Kuma's
// position + quaternion + scale Transform. Lossy for general
// affine matrices (shear, negative scale combined with non-axis
// rotation) but correct for the vast majority of glTF scenes
// where every node's transform is already a pure TRS composition.
void apply_world_matrix_to_transform(const float m[16], Transform& out) {
    // Column-major: column k is m[k*4..k*4+3]; element (row, col)
    // sits at m[col*4 + row].
    const Vec3 position{m[12], m[13], m[14]};

    // Per-axis scale is the length of each basis column (X, Y, Z).
    // Detect mirroring via the determinant of the upper-left 3x3
    // and fold its sign into the X-axis scale.
    const Vec3 col_x{m[0], m[1], m[2]};
    const Vec3 col_y{m[4], m[5], m[6]};
    const Vec3 col_z{m[8], m[9], m[10]};

    const float sx_mag = std::sqrt(col_x.x * col_x.x + col_x.y * col_x.y + col_x.z * col_x.z);
    const float sy_mag = std::sqrt(col_y.x * col_y.x + col_y.y * col_y.y + col_y.z * col_y.z);
    const float sz_mag = std::sqrt(col_z.x * col_z.x + col_z.y * col_z.y + col_z.z * col_z.z);

    const float det =
        col_x.x * (col_y.y * col_z.z - col_y.z * col_z.y) -
        col_x.y * (col_y.x * col_z.z - col_y.z * col_z.x) +
        col_x.z * (col_y.x * col_z.y - col_y.y * col_z.x);
    const float sign = (det < 0.0f) ? -1.0f : 1.0f;

    const Vec3 scale{sx_mag * sign, sy_mag, sz_mag};

    // Build the rotation matrix by removing scale from each column,
    // then convert to a quaternion using Shepperd's method (the
    // canonical "trace of the matrix" decomposition).
    const float inv_sx = sx_mag > 0.0f ? sign / sx_mag : 0.0f;
    const float inv_sy = sy_mag > 0.0f ? 1.0f / sy_mag : 0.0f;
    const float inv_sz = sz_mag > 0.0f ? 1.0f / sz_mag : 0.0f;

    const float r00 = m[0]  * inv_sx, r01 = m[4]  * inv_sy, r02 = m[8]  * inv_sz;
    const float r10 = m[1]  * inv_sx, r11 = m[5]  * inv_sy, r12 = m[9]  * inv_sz;
    const float r20 = m[2]  * inv_sx, r21 = m[6]  * inv_sy, r22 = m[10] * inv_sz;

    Quat q{};
    const float trace = r00 + r11 + r22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r21 - r12) / s;
        q.y = (r02 - r20) / s;
        q.z = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }

    out.set_position(position);
    out.set_rotation(q.normalized());
    out.set_scale(scale);
}

}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────

bool init(ResourceManager& resources) {
    if (g_state != nullptr) {
        kuma::log::warn("scene::init called twice; ignoring");
        return true;
    }
    auto state = std::make_unique<State>();
    state->resources = &resources;
    g_state = state.release();
    kuma::log::info("Scene module initialized");
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;
    g_state->cache.clear();
    delete g_state;
    g_state = nullptr;
    kuma::log::info("Scene module shut down");
}

// ── Loading ─────────────────────────────────────────────────────

const Scene* load(const char* path) {
    if (g_state == nullptr || path == nullptr) return nullptr;

    // Normalize the cache key so foo.kscene, ./foo.kscene, and a
    // version with redundant separators or symlinks all share an
    // entry. weakly_canonical handles missing files gracefully -
    // we'll detect those via the read_file failure below.
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        kuma::log::error("scene::load: failed to canonicalize '%s'", path);
        return nullptr;
    }
    const std::string key = canonical.string();
    if (auto it = g_state->cache.find(key); it != g_state->cache.end()) {
        return it->second.get();
    }

    auto bytes = read_file(canonical);
    if (bytes.empty()) {
        kuma::log::error("scene::load: failed to read '%s'", path);
        return nullptr;
    }

    asset_format::KSceneHeader header{};
    auto parse = asset_format::parse_kscene_header(bytes.data(), bytes.size(), header);
    if (parse != asset_format::ParseResult::Ok) {
        kuma::log::error("scene::load: parse failed for '%s' (code %d)",
                         path, static_cast<int>(parse));
        return nullptr;
    }

    auto scene = std::make_unique<Scene>();
    scene->origin_dir = canonical.parent_path();
    scene->header     = header;

    // Resolve every mesh path to its absolute on-disk location so
    // spawn() can hand them straight to ResourceManager. Rejected
    // paths (absolute, "../" traversal, or canonical failure) get
    // an empty path - the loader logs and treats them as missing.
    const auto* mesh_entries = reinterpret_cast<const asset_format::KSceneMeshEntry*>(
        bytes.data() + header.mesh_table_offset);
    const char* string_table = reinterpret_cast<const char*>(
        bytes.data() + header.string_table_offset);

    scene->mesh_paths.reserve(header.mesh_count);
    for (uint32_t i = 0; i < header.mesh_count; ++i) {
        const auto& entry = mesh_entries[i];
        std::string_view rel(string_table + entry.path_offset, entry.path_length);
        auto resolved = resolve_relative_path(scene->origin_dir, rel);
        if (!resolved) {
            kuma::log::warn(
                "scene::load: mesh #%u '%.*s' rejected (absolute path or '..' traversal)",
                i, static_cast<int>(rel.size()), rel.data());
            scene->mesh_paths.emplace_back();
        } else {
            scene->mesh_paths.push_back(*resolved);
        }
    }

    // Materials follow the same string-table conventions and
    // path-safety rules as meshes. Material table is empty for
    // scenes baked before the materials pass landed - the loop
    // below just doesn't execute in that case.
    const auto* material_entries = reinterpret_cast<const asset_format::KSceneMeshEntry*>(
        bytes.data() + header.material_table_offset);
    scene->material_paths.reserve(header.material_count);
    for (uint32_t i = 0; i < header.material_count; ++i) {
        const auto& entry = material_entries[i];
        std::string_view rel(string_table + entry.path_offset, entry.path_length);
        auto resolved = resolve_relative_path(scene->origin_dir, rel);
        if (!resolved) {
            kuma::log::warn(
                "scene::load: material #%u '%.*s' rejected (absolute path or '..' traversal)",
                i, static_cast<int>(rel.size()), rel.data());
            scene->material_paths.emplace_back();
        } else {
            scene->material_paths.push_back(*resolved);
        }
    }

    scene->raw = std::move(bytes);
    Scene* raw_ptr = scene.get();
    g_state->cache.emplace(key, std::move(scene));
    kuma::log::info("Scene loaded: %s (%u meshes, %u nodes)",
                    path, header.mesh_count, header.node_count);
    return raw_ptr;
}

// ── Spawning ────────────────────────────────────────────────────

SceneInstance spawn(const Scene* scene, Registry& registry) {
    if (g_state == nullptr || scene == nullptr) return {};

    SceneInstance instance{};
    instance.id = g_state->next_scene_id++;
    if (g_state->next_scene_id == 0) g_state->next_scene_id = 1;  // wrap-around guard

    // Resolve each mesh path through ResourceManager once per scene
    // load so the cache hits speed up subsequent spawns of the same
    // scene. Failed loads are tracked but don't abort the spawn.
    std::vector<const Mesh*> mesh_lookup(scene->mesh_paths.size(), nullptr);
    for (size_t i = 0; i < scene->mesh_paths.size(); ++i) {
        if (scene->mesh_paths[i].empty()) {
            ++instance.mesh_failed_count;
            continue;
        }
        const Mesh* m = g_state->resources->load_mesh_binary(
            scene->mesh_paths[i].string().c_str());
        if (m == nullptr) {
            kuma::log::warn("scene::spawn: mesh '%s' failed to load",
                            scene->mesh_paths[i].string().c_str());
            ++instance.mesh_failed_count;
        }
        mesh_lookup[i] = m;
    }

    // Same shape for materials. Empty material table (no per-node
    // material assignments yet) just leaves the lookup empty and
    // every node falls back to the renderer's default material.
    std::vector<const Material*> material_lookup(scene->material_paths.size(), nullptr);
    for (size_t i = 0; i < scene->material_paths.size(); ++i) {
        if (scene->material_paths[i].empty()) continue;
        const Material* m = g_state->resources->load_material_binary(
            scene->material_paths[i].string().c_str());
        if (m == nullptr) {
            kuma::log::warn("scene::spawn: material '%s' failed to load",
                            scene->material_paths[i].string().c_str());
        }
        material_lookup[i] = m;
    }

    // Walk node table, spawning one entity per entry. Nodes whose
    // mesh_index is kSceneNoMesh (or whose mesh failed to load)
    // still spawn an entity with Transform + RenderTag - the render
    // system null-checks MeshRef.mesh and skips silently.
    const auto* node_entries = reinterpret_cast<const asset_format::KSceneNodeEntry*>(
        scene->raw.data() + scene->header.node_table_offset);

    for (uint32_t i = 0; i < scene->header.node_count; ++i) {
        const auto& node = node_entries[i];

        EntityID e = registry.create_entity();

        Transform t;
        apply_world_matrix_to_transform(node.transform, t);
        registry.add(e, t);

        const Mesh* mesh = nullptr;
        if (node.mesh_index != asset_format::kSceneNoMesh &&
            node.mesh_index < mesh_lookup.size()) {
            mesh = mesh_lookup[node.mesh_index];
        }
        registry.add(e, MeshRef{mesh});

        const Material* material = nullptr;
        if (node.material_index != asset_format::kSceneNoMaterial &&
            node.material_index < material_lookup.size()) {
            material = material_lookup[node.material_index];
        }
        registry.add(e, MaterialRef{material});

        // RenderTag is currently a struct{} marker living in the
        // sandbox. The scene module shouldn't depend on it - the
        // render system can iterate view<Transform, MeshRef> and
        // skip entities with mesh==nullptr. Leaving RenderTag as
        // a sandbox concern keeps the engine module independent
        // of game-specific tagging conventions.

        registry.add(e, SceneTag{instance.id});
        ++instance.entity_count;
    }

    if (instance.mesh_failed_count > 0) {
        kuma::log::warn("scene::spawn: %u/%u meshes failed to load for scene id %u",
                        instance.mesh_failed_count, scene->header.mesh_count, instance.id);
    }
    return instance;
}

SceneInstance load_and_spawn(const char* path, Registry& registry) {
    const Scene* s = load(path);
    if (s == nullptr) return {};
    return spawn(s, registry);
}

// ── Despawn ─────────────────────────────────────────────────────
// Two-pass: collect every EntityID with a matching SceneTag into a
// temporary buffer first, then destroy. Mutating the registry while
// a view iteration is live is undefined behavior per ecs.h.
void despawn(Registry& registry, uint32_t scene_id) {
    if (g_state == nullptr) return;
    if (scene_id == 0) return;

    std::vector<EntityID> doomed;
    for (auto [e, tag] : registry.view<SceneTag>()) {
        if (tag.scene_id == scene_id) doomed.push_back(e);
    }
    for (EntityID e : doomed) {
        registry.destroy_entity(e);
    }

    if (!doomed.empty()) {
        kuma::log::info("Scene despawned: id %u (%zu entities)",
                        scene_id, doomed.size());
    }
}

uint32_t loaded_scene_count() {
    if (g_state == nullptr) return 0;
    return static_cast<uint32_t>(g_state->cache.size());
}

}  // namespace kuma::scene
