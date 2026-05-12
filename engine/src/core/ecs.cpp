#include <kuma/ecs.h>

#include "core/ecs_internal.h"

namespace kuma {

// ── Registry (PImpl forwarding) ─────────────────────────────────

Registry::Registry() : impl_(new RegistryImpl()) {}

Registry::~Registry() { delete impl_; }

EntityID Registry::create_entity() { return impl_->create_entity(); }

void Registry::destroy_entity(EntityID e) { impl_->destroy_entity(e); }

bool Registry::is_valid(EntityID e) const { return impl_->is_valid(e); }

// ── RegistryImpl ────────────────────────────────────────────────

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
    // Bump generation so any outstanding copies of `e` become stale.
    generations_[e.id] += 1;
    free_slots_.push_back(e.id);
}

bool RegistryImpl::is_valid(EntityID e) const {
    if (e.id == 0) return false;                    // reserved slot
    if (e.id >= generations_.size()) return false;  // never allocated
    return generations_[e.id] == e.generation;
}

}  // namespace kuma
