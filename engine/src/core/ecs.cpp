#include <kuma/ecs.h>

#include "core/ecs_internal.h"

namespace kuma {

namespace detail {

ComponentTypeID next_component_type_id() {
    // Function-local static so the counter has internal linkage from
    // the caller's perspective but a single definition point. Each
    // distinct component type triggers exactly one increment via the
    // function-local static inside component_id<T>().
    static ComponentTypeID counter = 0;
    return counter++;
}

}  // namespace detail

// ── Registry (PImpl forwarding) ─────────────────────────────────

Registry::Registry() : impl_(new RegistryImpl()) {}

Registry::~Registry() { delete impl_; }

EntityID Registry::create_entity() { return impl_->create_entity(); }

void Registry::destroy_entity(EntityID e) { impl_->destroy_entity(e); }

bool Registry::is_valid(EntityID e) const { return impl_->is_valid(e); }

detail::IComponentPool* Registry::get_pool_raw(detail::ComponentTypeID id) const {
    return impl_->get_pool(id);
}

detail::IComponentPool* Registry::get_or_create_pool_raw(detail::ComponentTypeID id,
                                                         PoolFactory factory) {
    return impl_->get_or_create_pool(id, factory);
}

// ── RegistryImpl ────────────────────────────────────────────────

uint32_t Registry::generation_for_slot(uint32_t slot) const {
    return impl_->generation_for_slot(slot);
}

EntityID RegistryImpl::create_entity() {
    // Reserve slot 0 on first use so kInvalidEntity{0,0} stays
    // permanently invalid no matter how many entities we churn.
    if (generations_.empty()) {
        generations_.push_back(0);  // slot 0: never valid
    }

    uint32_t slot;
    if (!free_slots_.empty()) {
        slot = free_slots_.back();
        free_slots_.pop_back();
    } else {
        slot = static_cast<uint32_t>(generations_.size());
        generations_.push_back(0);
    }

    // Bump generation on (re)allocation. Fresh slots go from 0 -> 1,
    // reused slots increment. Result: every live handle has gen >= 1,
    // and stale handles fail the generation match in is_valid().
    generations_[slot] += 1;

    return EntityID{slot, generations_[slot]};
}

void RegistryImpl::destroy_entity(EntityID e) {
    if (!is_valid(e)) {
        return;  // silent no-op for stale / invalid handles
    }
    // Strip every component the entity held. Pools that don't have
    // this entity no-op internally, so this is safe and cheap.
    for (auto& pool : pools_) {
        if (pool) pool->remove_if_present(e.id);
    }
    // Bump generation so any outstanding copies of `e` become stale.
    generations_[e.id] += 1;
    free_slots_.push_back(e.id);
}

bool RegistryImpl::is_valid(EntityID e) const {
    if (e.id == 0) return false;                    // reserved slot
    if (e.id >= generations_.size()) return false;  // never allocated
    return generations_[e.id] == e.generation;
}

uint32_t RegistryImpl::generation_for_slot(uint32_t slot) const {
    if (slot >= generations_.size()) return 0;
    return generations_[slot];
}

detail::IComponentPool* RegistryImpl::get_pool(detail::ComponentTypeID id) const {
    if (id >= pools_.size()) return nullptr;
    return pools_[id].get();
}

detail::IComponentPool* RegistryImpl::get_or_create_pool(
    detail::ComponentTypeID id,
    detail::IComponentPool* (*factory)()) {
    if (id >= pools_.size()) {
        pools_.resize(id + 1);
    }
    if (!pools_[id]) {
        pools_[id].reset(factory());
    }
    return pools_[id].get();
}

}  // namespace kuma
