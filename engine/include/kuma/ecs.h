#pragma once

// ── Kuma ECS ────────────────────────────────────────────────────
// Entity-Component-System registry. Sparse-set storage internally
// (see ADR 0006). Public API contract is storage-agnostic so the
// implementation can evolve without breaking callers.
//
// Discipline rules:
//   - Components should be POD-like (trivially copyable preferred,
//     no virtual functions, no non-trivial destructors). Resource
//     cleanup belongs in user code on destroy_entity.
//   - Mutating the registry (add/remove/destroy) DURING iteration
//     of a view() is undefined behavior. Defer to end-of-frame in
//     user code.
//
// Frame phase: typically Phase 3 UPDATE (game logic).

#include <cstdint>

namespace kuma {

// Generational handle. Two 32-bit fields = 8 bytes total. When an
// entity slot is destroyed and reused, generation bumps so any
// stored copy of the old handle detects it's stale via is_valid().
//
// Generation 0 is reserved for the invalid sentinel; valid entities
// start at generation 1 and increment from there per slot reuse.
struct EntityID {
    uint32_t id = 0;
    uint32_t generation = 0;

    bool operator==(const EntityID& other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(const EntityID& other) const { return !(*this == other); }
};

// Sentinel for "no entity / invalid handle". Any default-constructed
// EntityID compares equal to this; valid entities never do.
constexpr EntityID kInvalidEntity{0, 0};

// Forward declaration - storage details live in src/core/ecs_internal.h.
class RegistryImpl;

class Registry {
public:
    Registry();
    ~Registry();

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // ── Entity lifecycle ───────────────────────────────────────
    // Allocates a fresh EntityID. Reuses destroyed slots (with
    // bumped generation) before growing the underlying storage.
    EntityID create_entity();

    // Marks the entity destroyed. The slot becomes available for
    // future create_entity() calls; subsequent is_valid() on the
    // same handle returns false because generations differ.
    void destroy_entity(EntityID e);

    // True if `e` refers to a currently-live entity. False for
    // kInvalidEntity, for handles to destroyed entities, and for
    // out-of-range IDs.
    bool is_valid(EntityID e) const;

private:
    RegistryImpl* impl_;
};

}  // namespace kuma
