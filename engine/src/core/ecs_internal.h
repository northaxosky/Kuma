#pragma once

// Private implementation header - reachable from tests via the
// engine/src include path, NOT from public game code.
//
// Holds the sparse-set storage for entities and components. The
// split exists so the public ecs.h has no implementation types
// in it - swapping storage strategies later means rewriting this
// file, not the public header.

#include <kuma/ecs.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace kuma {

class RegistryImpl {
public:
    EntityID create_entity();
    void destroy_entity(EntityID e);
    bool is_valid(EntityID e) const;
    uint32_t generation_for_slot(uint32_t slot) const;

    // Pool access - called by Registry's templated methods through
    // the public Registry::get_pool / get_or_create_pool helpers.
    // The void*-style pointer is only safe because both sides agree
    // on the static_cast back to ComponentPool<T>*.
    detail::IComponentPool* get_pool(detail::ComponentTypeID id) const;
    detail::IComponentPool* get_or_create_pool(detail::ComponentTypeID id,
                                               detail::IComponentPool* (*factory)());

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

    // One entry per registered component type, indexed by
    // detail::component_id<T>(). Slots are resized lazily as new
    // component types appear.
    std::vector<std::unique_ptr<detail::IComponentPool>> pools_;
};

}  // namespace kuma
