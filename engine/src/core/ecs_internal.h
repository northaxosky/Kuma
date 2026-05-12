#pragma once

// Private implementation header - reachable from tests via the
// engine/src include path, NOT from public game code.
//
// Holds the sparse-set storage for entities and (later) components.
// The split exists so the public ecs.h has zero implementation
// types in it - swapping storage strategies later means rewriting
// this file, not the public header.

#include <kuma/ecs.h>

#include <cstdint>
#include <vector>

namespace kuma {

class RegistryImpl {
public:
    EntityID create_entity();
    void destroy_entity(EntityID e);
    bool is_valid(EntityID e) const;

private:
    // Per-slot generation counter. Index = EntityID::id, value =
    // current generation for that slot. A handle is valid iff its
    // generation matches the slot's current generation.
    //
    // Slot 0 is reserved (kInvalidEntity). Valid slots start at 1.
    // Generation 0 also means "never used" - the first time slot N
    // is allocated it bumps to generation 1, matching the invariant
    // that valid entities have generation >= 1.
    std::vector<uint32_t> generations_;

    // Free-list of destroyed slot IDs ready for reuse. Pop from the
    // back (LIFO) for cache-friendliness - recently-freed slots are
    // hottest in cache.
    std::vector<uint32_t> free_slots_;
};

}  // namespace kuma
