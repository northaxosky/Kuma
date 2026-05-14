#pragma once

// ── Kuma Scene ──────────────────────────────────────────────────
// Multi-mesh scene loading. A "scene" is a recipe for spawning a
// coherent set of entities - typically a level, a room, or a prop
// group - from a single .kscene asset baked from glTF.
//
// Asset pipeline: kuma-bake's `scene` subcommand walks the glTF
// scene tree, dedups mesh primitives, composes world transforms
// from the parent chain, and writes:
//
//   sponza.kscene                  the scene index (mesh paths +
//                                   per-node world transforms)
//   sponza-meshes/0.kmesh          one .kmesh per unique primitive
//   sponza-meshes/1.kmesh          (shared by N nodes via mesh
//   ...                             dedup at bake time)
//
// At runtime, scene::load reads the .kscene, resolves each mesh
// path relative to the .kscene's own directory (no "../" traversal
// or absolute paths allowed), and lets ResourceManager cache them.
// scene::spawn then creates one ECS entity per node, attaching a
// Transform (already world-space), a MeshRef (borrowing the cached
// Mesh*), a RenderTag, and a SceneTag.
//
// Threading: every kuma::scene:: function must be called from the
// main thread.

#include <cstdint>

#include <kuma/ecs.h>
#include <kuma/material.h>
#include <kuma/math.h>

namespace kuma {

class Registry;
class ResourceManager;
struct Mesh;

namespace scene {

// Opaque loaded scene. Owned by the scene module from load() until
// shutdown(); subsequent loads of the same canonical path return
// the same pointer.
struct Scene;

// ECS marker that carries the scene-instance ID. spawn() returns
// the id allocated for that spawn so callers can despawn precisely
// or filter views to a specific scene instance.
struct SceneTag {
    uint32_t scene_id = 0;
};

// Returned by spawn / load_and_spawn so callers can reason about
// what happened. mesh_failed_count tracks how many node entries
// referenced a mesh that ResourceManager couldn't load - those
// entities still spawn (with MeshRef.mesh == nullptr) so game code
// can choose whether to treat partial loads as errors.
struct SceneInstance {
    uint32_t id = 0;                  // 0 == invalid (no spawn happened)
    uint32_t entity_count = 0;        // total entities spawned
    uint32_t mesh_failed_count = 0;   // entities whose mesh load failed
    bool valid() const { return id != 0; }
};

// ── Lifecycle ───────────────────────────────────────────────────
// Engine drives these. Scene module needs ResourceManager to
// resolve mesh references, so it must init AFTER ResourceManager.
bool init(ResourceManager& resources);
void shutdown();

// ── Scene loading ───────────────────────────────────────────────
// Returns nullptr on missing file, bad header, or if the scene
// module is not initialized. Cache hits return the same pointer.
const Scene* load(const char* path);

// Spawn one entity per node in the scene. Each spawn allocates a
// fresh SceneInstance::id from an internal counter, so two spawns
// of the same scene produce two independent instance IDs and
// despawn(id) only affects one of them.
SceneInstance spawn(const Scene* scene, Registry& registry);

// Convenience: load(path) then spawn. Returns invalid SceneInstance
// on load failure (with a logged error).
SceneInstance load_and_spawn(const char* path, Registry& registry);

// Destroy every entity tagged with the given scene_id. Safe even
// if the id is unknown (no-op). Iterates view<SceneTag>() into a
// temporary buffer first, then destroys, so it never mutates the
// registry mid-iteration.
void despawn(Registry& registry, uint32_t scene_id);

// ── Diagnostics ─────────────────────────────────────────────────

// Number of distinct .kscene assets currently cached.
uint32_t loaded_scene_count();

}  // namespace scene

// ── MeshRef component ───────────────────────────────────────────
// Pure-data component holding a borrowed pointer to a Mesh owned
// by ResourceManager. The render system iterates
// view<Transform, MeshRef, RenderTag> and calls
// renderer.set_mesh(mesh_ref.mesh) per entity. Null-mesh entries
// are skipped silently so partial scene loads don't crash.
struct MeshRef {
    const Mesh* mesh = nullptr;
};

// ── MaterialRef component ───────────────────────────────────────
// Pure-data component holding a borrowed pointer to a Material
// owned by ResourceManager. The render system uses it to bind the
// right texture set for an entity's mesh. Entities that lack this
// component (or that point at a null Material) render with the
// renderer's default material - useful for placeholder geometry
// before the materials bake pass populates real data.
struct MaterialRef {
    const Material* material = nullptr;
};

}  // namespace kuma
