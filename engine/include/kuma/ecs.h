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

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

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

// ── Internal: component type IDs and pool storage ───────────────
// Lives in `detail` because game code never names these types.
namespace detail {

using ComponentTypeID = uint32_t;

// Monotonic counter implemented in ecs.cpp. Each translation unit
// shares the same source of truth.
ComponentTypeID next_component_type_id();

// Returns a unique ID per component type T. The static initializer
// runs once per T, the first time component_id<T>() is called from
// any thread - subsequent calls return the cached value.
template <typename T>
inline ComponentTypeID component_id() {
    static const ComponentTypeID id = next_component_type_id();
    return id;
}

// Type-erased pool interface so the Registry can hold a vector of
// pools-of-different-component-types in one container.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    // Called by Registry::destroy_entity for every pool, regardless
    // of whether this pool actually has the entity. Pools that don't
    // have it must no-op silently.
    virtual void remove_if_present(uint32_t entity_slot) = 0;
};

// Sparse-set pool for component type T.
//
//   sparse_[slot]  → index into dense_, or kNoIndex if missing
//   dense_         → packed array of T (no gaps)
//   entity_slots_  → parallel array: entity slot for dense_[i]
//
// Add: O(1) push_back + sparse update.
// Remove: O(1) swap-with-last + pop_back + two sparse updates.
// has/get: O(1) sparse lookup.
template <typename T>
class ComponentPool : public IComponentPool {
public:
    static constexpr uint32_t kNoIndex = static_cast<uint32_t>(-1);

    void add(uint32_t slot, T value) {
        if (slot >= sparse_.size()) {
            sparse_.resize(slot + 1, kNoIndex);
        }
        if (sparse_[slot] != kNoIndex) {
            // Entity already has this component - overwrite in place.
            // Permissive choice (mirrors std::map::operator[]) so add()
            // doubles as a setter without forcing a remove() dance.
            dense_[sparse_[slot]] = std::move(value);
            return;
        }
        sparse_[slot] = static_cast<uint32_t>(dense_.size());
        dense_.push_back(std::move(value));
        entity_slots_.push_back(slot);
    }

    void remove_if_present(uint32_t slot) override {
        if (slot >= sparse_.size() || sparse_[slot] == kNoIndex) {
            return;  // not present - silent no-op
        }
        const uint32_t index = sparse_[slot];
        const uint32_t last = static_cast<uint32_t>(dense_.size()) - 1;

        if (index != last) {
            // Swap-with-last so dense_ stays packed.
            dense_[index] = std::move(dense_[last]);
            entity_slots_[index] = entity_slots_[last];
            // The swapped-in entity now lives at `index`, so update
            // its sparse pointer to match.
            sparse_[entity_slots_[index]] = index;
        }
        dense_.pop_back();
        entity_slots_.pop_back();
        sparse_[slot] = kNoIndex;
    }

    bool has(uint32_t slot) const {
        return slot < sparse_.size() && sparse_[slot] != kNoIndex;
    }

    T& get(uint32_t slot) {
        // Caller must have verified has(slot) first; assert in debug.
        assert(has(slot) && "ComponentPool::get on missing component");
        return dense_[sparse_[slot]];
    }

    const T& get(uint32_t slot) const {
        assert(has(slot) && "ComponentPool::get on missing component");
        return dense_[sparse_[slot]];
    }

    T* try_get(uint32_t slot) {
        if (!has(slot)) return nullptr;
        return &dense_[sparse_[slot]];
    }

    const T* try_get(uint32_t slot) const {
        if (!has(slot)) return nullptr;
        return &dense_[sparse_[slot]];
    }

    size_t size() const { return dense_.size(); }

private:
    std::vector<uint32_t> sparse_;
    std::vector<T> dense_;
    std::vector<uint32_t> entity_slots_;
};

}  // namespace detail

// Forward declaration - the actual storage lives in ecs_internal.h.
class RegistryImpl;

class Registry {
public:
    Registry();
    ~Registry();

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    // ── Entity lifecycle ───────────────────────────────────────
    EntityID create_entity();
    void destroy_entity(EntityID e);
    bool is_valid(EntityID e) const;

    // ── Component access ───────────────────────────────────────
    // add() on an entity that already has the component overwrites.
    // add() on an invalid entity is a silent no-op.
    template <typename T>
    void add(EntityID e, T component) {
        if (!is_valid(e)) return;
        get_or_create_pool<T>()->add(e.id, std::move(component));
    }

    // remove() on a missing component is a silent no-op.
    template <typename T>
    void remove(EntityID e) {
        if (!is_valid(e)) return;
        if (auto* pool = get_pool<T>()) {
            pool->remove_if_present(e.id);
        }
    }

    template <typename T>
    bool has(EntityID e) const {
        if (!is_valid(e)) return false;
        const auto* pool = get_pool<T>();
        return pool && pool->has(e.id);
    }

    // get() requires the entity to have the component. Asserts in
    // debug builds; undefined behavior in release if missing. Use
    // try_get() when the component might not be present.
    template <typename T>
    T& get(EntityID e) {
        assert(is_valid(e) && "Registry::get on invalid entity");
        return get_pool<T>()->get(e.id);
    }

    template <typename T>
    const T& get(EntityID e) const {
        assert(is_valid(e) && "Registry::get on invalid entity");
        return get_pool<T>()->get(e.id);
    }

    // Returns nullptr if the entity is invalid OR doesn't have T.
    // The pointer is valid until the next mutation of T's pool.
    template <typename T>
    T* try_get(EntityID e) {
        if (!is_valid(e)) return nullptr;
        auto* pool = get_pool<T>();
        return pool ? pool->try_get(e.id) : nullptr;
    }

    template <typename T>
    const T* try_get(EntityID e) const {
        if (!is_valid(e)) return nullptr;
        const auto* pool = get_pool<T>();
        return pool ? pool->try_get(e.id) : nullptr;
    }

private:
    // Pool factory passed through type-erased boundary so the impl
    // doesn't need to template on T.
    using PoolFactory = detail::IComponentPool* (*)();

    detail::IComponentPool* get_or_create_pool_raw(detail::ComponentTypeID id,
                                                   PoolFactory factory);
    detail::IComponentPool* get_pool_raw(detail::ComponentTypeID id) const;

    template <typename T>
    detail::ComponentPool<T>* get_pool() const {
        auto* raw = get_pool_raw(detail::component_id<T>());
        return static_cast<detail::ComponentPool<T>*>(raw);
    }

    template <typename T>
    detail::ComponentPool<T>* get_or_create_pool() {
        auto* raw = get_or_create_pool_raw(
            detail::component_id<T>(),
            []() -> detail::IComponentPool* { return new detail::ComponentPool<T>(); });
        return static_cast<detail::ComponentPool<T>*>(raw);
    }

    RegistryImpl* impl_;
};

}  // namespace kuma
