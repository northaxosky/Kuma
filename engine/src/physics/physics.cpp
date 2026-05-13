#include <kuma/physics.h>

#include <algorithm>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <kuma/log.h>
#include <kuma/transform.h>

#include "physics/jolt_globals.h"
#include "physics/layers.h"

namespace kuma::physics {

namespace {

// ── Per-body backend record ─────────────────────────────────────
// One entry per live body. Stored in a packed vector inside
// BodyStore; PhysicsHandle::index points into the same vector,
// with a free-list of recycled slots so handles stay compact.
struct BodyRecord {
    JPH::BodyID jolt_id;          // empty when slot is on the free list
    EntityID entity;              // owning entity for sync-back
    BodyType type;
    bool in_use = false;
};

// ── BodyStore ───────────────────────────────────────────────────
// Owns the entity -> body mapping. Two indices coexist:
//   records_     packed vector keyed by PhysicsHandle::index
//   by_entity_   slot -> handle index, with generation check
class BodyStore {
public:
    PhysicsHandle add(EntityID entity, JPH::BodyID jolt_id, BodyType type) {
        uint32_t index;
        if (!free_slots_.empty()) {
            index = free_slots_.back();
            free_slots_.pop_back();
            records_[index] = BodyRecord{jolt_id, entity, type, true};
        } else {
            index = static_cast<uint32_t>(records_.size());
            records_.push_back(BodyRecord{jolt_id, entity, type, true});
        }
        by_entity_[entity.id] = EntitySlot{entity.generation, index};
        ++live_count_;
        return PhysicsHandle{index};
    }

    BodyRecord* lookup(EntityID entity) {
        auto it = by_entity_.find(entity.id);
        if (it == by_entity_.end()) return nullptr;
        if (it->second.generation != entity.generation) return nullptr;
        auto& rec = records_[it->second.index];
        return rec.in_use ? &rec : nullptr;
    }

    // Remove by entity. Returns the Jolt body id so the caller can
    // hand it to BodyInterface::RemoveBody / DestroyBody.
    JPH::BodyID remove(EntityID entity) {
        auto it = by_entity_.find(entity.id);
        if (it == by_entity_.end()) return JPH::BodyID{};
        if (it->second.generation != entity.generation) return JPH::BodyID{};
        const uint32_t index = it->second.index;
        by_entity_.erase(it);
        return remove_at(index);
    }

    // Remove by record index. Used by the prune-stale sweep, which
    // walks records directly so it can still drop bodies whose ECS
    // slot was reused (in which case by_entity_ no longer points
    // at the old generation and entity-keyed remove would miss them).
    JPH::BodyID remove_by_index(uint32_t index) {
        if (index >= records_.size() || !records_[index].in_use) {
            return JPH::BodyID{};
        }
        const EntityID owner = records_[index].entity;
        auto it = by_entity_.find(owner.id);
        if (it != by_entity_.end() && it->second.index == index) {
            by_entity_.erase(it);
        }
        return remove_at(index);
    }

    uint32_t live_count() const { return live_count_; }
    const std::vector<BodyRecord>& records() const { return records_; }

private:
    struct EntitySlot {
        uint32_t generation;
        uint32_t index;
    };

    JPH::BodyID remove_at(uint32_t index) {
        auto& rec = records_[index];
        const JPH::BodyID id = rec.jolt_id;
        rec.in_use = false;
        rec.jolt_id = JPH::BodyID{};
        free_slots_.push_back(index);
        --live_count_;
        return id;
    }

    std::vector<BodyRecord> records_;
    std::vector<uint32_t> free_slots_;
    std::unordered_map<uint32_t, EntitySlot> by_entity_;
    uint32_t live_count_ = 0;
};

// ── Module state ────────────────────────────────────────────────
struct State {
    PhysicsConfig config{};
    std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;
    std::unique_ptr<detail::KumaBPLayerInterface> bp_layer_interface;
    std::unique_ptr<detail::KumaObjectVsBPLayerFilter> object_vs_bp_filter;
    std::unique_ptr<detail::KumaObjectPairFilter> object_pair_filter;
    std::unique_ptr<JPH::PhysicsSystem> system;
    BodyStore bodies;

    // Fixed-step accumulator. Drained inside simulate() at config.fixed_step
    // intervals, capped by max_steps_per_frame to prevent spiral-of-death.
    float accumulator = 0.0f;
};

State* g_state = nullptr;

uint32_t recommended_worker_count() {
    const uint32_t hw = std::thread::hardware_concurrency();
    if (hw <= 1) return 1;
    return hw - 1;
}

// ── Shape factory ───────────────────────────────────────────────
// Maps the kuma-side {BodyShape, dimensions} tuple to a Jolt shape.
// Returns an empty Ref on unrecognized shapes (Jolt's body create
// path will reject the body and we log the error in create_body).
JPH::ShapeRefC build_shape(const PhysicsBody& body) {
    switch (body.shape) {
        case BodyShape::Sphere: {
            const float radius = body.dimensions.x > 0.0f ? body.dimensions.x : 0.5f;
            return new JPH::SphereShape(radius);
        }
        case BodyShape::Box: {
            const auto& d = body.dimensions;
            return new JPH::BoxShape(JPH::Vec3(d.x, d.y, d.z));
        }
        case BodyShape::Capsule: {
            const float radius = body.dimensions.x > 0.0f ? body.dimensions.x : 0.5f;
            const float half_height = body.dimensions.y > 0.0f ? body.dimensions.y : 0.5f;
            return new JPH::CapsuleShape(half_height, radius);
        }
    }
    return JPH::ShapeRefC{};
}

JPH::EMotionType to_jph_motion_type(BodyType type) {
    switch (type) {
        case BodyType::Static:    return JPH::EMotionType::Static;
        case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
        case BodyType::Dynamic:   return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Dynamic;
}

JPH::EActivation activation_for(BodyType type) {
    return type == BodyType::Static ? JPH::EActivation::DontActivate
                                    : JPH::EActivation::Activate;
}

// Convert engine types to/from Jolt math (single-precision build).
inline JPH::RVec3 to_rvec3(const Vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
inline JPH::Vec3 to_vec3(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline JPH::Quat to_quat(const Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline Vec3 from_vec3(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
inline Vec3 from_rvec3(const JPH::RVec3& v) {
    return {static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())};
}
inline Quat from_quat(const JPH::Quat& q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

// ── Body creation ───────────────────────────────────────────────
// Walks the registry for any PhysicsBody not yet flipped to
// `created`, validates it has a Transform to seed the spawn pose
// from, and registers it with Jolt + the BodyStore. Missing-
// Transform components log a warning and are skipped (the user can
// add the Transform later and the body will be created next frame).
void create_pending_bodies(Registry& registry) {
    JPH::BodyInterface& bi = g_state->system->GetBodyInterface();

    for (auto [entity, body, transform] : registry.view<PhysicsBody, Transform>()) {
        if (body.created) continue;

        JPH::ShapeRefC shape = build_shape(body);
        if (shape == nullptr) {
            kuma::log::warn("physics: invalid shape on entity %u; skipping body create", entity.id);
            continue;
        }

        JPH::BodyCreationSettings settings(
            shape,
            to_rvec3(transform.position()),
            to_quat(transform.rotation()),
            to_jph_motion_type(body.type),
            detail::to_jph(body.layer));

        if (body.type == BodyType::Dynamic) {
            settings.mMassPropertiesOverride.mMass = body.mass;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        }
        settings.mRestitution = body.restitution;
        settings.mFriction = body.friction;

        JPH::Body* jolt_body = bi.CreateBody(settings);
        if (jolt_body == nullptr) {
            kuma::log::error(
                "physics: CreateBody returned null on entity %u (body pool exhausted?)",
                entity.id);
            continue;
        }

        bi.AddBody(jolt_body->GetID(), activation_for(body.type));

        body.handle = g_state->bodies.add(entity, jolt_body->GetID(), body.type);
        body.created = true;
    }
}

// Push current Transform poses for kinematic bodies into the
// simulation as targets for the next fixed step. Jolt derives the
// implied velocity so collisions still push dynamics around.
void push_kinematic_targets(Registry& registry, float dt) {
    if (dt <= 0.0f) return;
    JPH::BodyInterface& bi = g_state->system->GetBodyInterface();

    for (auto [entity, body, transform] : registry.view<PhysicsBody, Transform>()) {
        if (!body.created) continue;
        if (body.type != BodyType::Kinematic) continue;
        bi.MoveKinematic(
            g_state->bodies.lookup(entity)->jolt_id,
            to_rvec3(transform.position()),
            to_quat(transform.rotation()),
            dt);
    }
}

// After the fixed-step loop runs, copy Dynamic body poses back
// into the entity Transforms so rendering and gameplay see the
// authoritative simulation state. Static bodies never move so we
// skip them; kinematic Transforms are game-owned so we skip those
// too (the game's writes are the source of truth there).
void sync_dynamic_transforms(Registry& registry) {
    const JPH::BodyInterface& bi = g_state->system->GetBodyInterfaceNoLock();

    for (auto [entity, body, transform] : registry.view<PhysicsBody, Transform>()) {
        if (!body.created) continue;
        if (body.type != BodyType::Dynamic) continue;
        const JPH::BodyID id = g_state->bodies.lookup(entity)->jolt_id;
        const JPH::RVec3 pos = bi.GetPosition(id);
        const JPH::Quat rot = bi.GetRotation(id);
        transform.set_position(from_rvec3(pos));
        transform.set_rotation(from_quat(rot));
    }
}

// Belt-and-suspenders cleanup: if a caller forgot to call
// physics::remove_body before destroying an entity (or removing
// the PhysicsBody component), the body would otherwise leak. Walk
// every record by index and drop any whose owning entity is gone
// or has lost its PhysicsBody component. Going by index (not by
// entity) means we still catch bodies whose ECS slot was reused
// and is now owned by a different generation.
void prune_stale_bodies(Registry& registry) {
    if (g_state->bodies.live_count() == 0) return;
    JPH::BodyInterface& bi = g_state->system->GetBodyInterface();

    std::vector<uint32_t> dead_indices;
    const auto& records = g_state->bodies.records();
    for (uint32_t i = 0; i < records.size(); ++i) {
        const auto& rec = records[i];
        if (!rec.in_use) continue;
        if (!registry.is_valid(rec.entity) || !registry.has<PhysicsBody>(rec.entity)) {
            dead_indices.push_back(i);
        }
    }

    for (uint32_t i : dead_indices) {
        const JPH::BodyID id = g_state->bodies.remove_by_index(i);
        if (!id.IsInvalid()) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
    }

    if (!dead_indices.empty()) {
        kuma::log::warn(
            "physics: cleaned up %zu leaked bodies (caller forgot to call remove_body / destroy_entity)",
            dead_indices.size());
    }
}

}  // namespace

bool init(const PhysicsConfig& config) {
    if (g_state != nullptr) {
        kuma::log::warn("physics::init called twice; ignoring");
        return true;
    }

    detail::ensure_jolt_globals_initialized();

    auto state = std::make_unique<State>();
    state->config = config;

    state->temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(
        static_cast<JPH::uint>(config.temp_allocator_bytes));

    const uint32_t threads = recommended_worker_count();
    state->job_system = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(threads));

    state->bp_layer_interface = std::make_unique<detail::KumaBPLayerInterface>();
    state->object_vs_bp_filter = std::make_unique<detail::KumaObjectVsBPLayerFilter>();
    state->object_pair_filter = std::make_unique<detail::KumaObjectPairFilter>();

    state->system = std::make_unique<JPH::PhysicsSystem>();
    state->system->Init(
        config.max_bodies,
        /*num_body_mutexes=*/0,
        config.max_body_pairs,
        config.max_contact_constraints,
        *state->bp_layer_interface,
        *state->object_vs_bp_filter,
        *state->object_pair_filter);

    state->system->SetGravity(
        JPH::Vec3(config.gravity.x, config.gravity.y, config.gravity.z));

    g_state = state.release();

    kuma::log::info(
        "Physics initialized: %u worker threads, %.1f MB temp allocator, gravity=(%.2f, %.2f, %.2f)",
        threads,
        config.temp_allocator_bytes / (1024.0 * 1024.0),
        config.gravity.x, config.gravity.y, config.gravity.z);
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;

    // Drop every live body before letting the PhysicsSystem destruct,
    // otherwise Jolt asserts on bodies still owning their slots.
    JPH::BodyInterface& bi = g_state->system->GetBodyInterface();
    for (const auto& rec : g_state->bodies.records()) {
        if (!rec.in_use) continue;
        bi.RemoveBody(rec.jolt_id);
        bi.DestroyBody(rec.jolt_id);
    }

    delete g_state;
    g_state = nullptr;

    kuma::log::info("Physics shut down");
}

void simulate(float dt, Registry& registry) {
    if (g_state == nullptr) return;
    if (dt < 0.0f) dt = 0.0f;

    create_pending_bodies(registry);

    g_state->accumulator += dt;
    const float step = g_state->config.fixed_step;
    const uint32_t max_steps = g_state->config.max_steps_per_frame;

    uint32_t steps = 0;
    while (g_state->accumulator >= step && steps < max_steps) {
        push_kinematic_targets(registry, step);
        g_state->system->Update(
            step,
            /*collision_steps=*/1,
            g_state->temp_allocator.get(),
            g_state->job_system.get());
        g_state->accumulator -= step;
        ++steps;
    }

    // Stall guard: if we hit the step cap, drop the remaining
    // accumulator instead of letting it grow unbounded between frames.
    if (steps == max_steps && g_state->accumulator >= step) {
        g_state->accumulator = 0.0f;
    }

    sync_dynamic_transforms(registry);
    prune_stale_bodies(registry);
}

void set_transform(EntityID e, const Vec3& position, const Quat& rotation) {
    if (g_state == nullptr) return;
    auto* rec = g_state->bodies.lookup(e);
    if (rec == nullptr) return;
    g_state->system->GetBodyInterface().SetPositionAndRotation(
        rec->jolt_id, to_rvec3(position), to_quat(rotation), activation_for(rec->type));
    if (rec->type == BodyType::Dynamic) {
        g_state->system->GetBodyInterface().SetLinearVelocity(rec->jolt_id, JPH::Vec3::sZero());
    }
}

void set_linear_velocity(EntityID e, const Vec3& velocity) {
    if (g_state == nullptr) return;
    auto* rec = g_state->bodies.lookup(e);
    if (rec == nullptr) return;
    g_state->system->GetBodyInterface().SetLinearVelocity(rec->jolt_id, to_vec3(velocity));
}

void apply_impulse(EntityID e, const Vec3& impulse) {
    if (g_state == nullptr) return;
    auto* rec = g_state->bodies.lookup(e);
    if (rec == nullptr) return;
    g_state->system->GetBodyInterface().AddImpulse(rec->jolt_id, to_vec3(impulse));
}

void set_kinematic_target(EntityID /*e*/, const Vec3& /*position*/, const Quat& /*rotation*/) {
    // Kinematic targets are pushed implicitly from the entity's
    // Transform inside simulate(); explicit targets aren't needed
    // until we expose multi-target / look-ahead control later.
}

Vec3 get_linear_velocity(EntityID e) {
    if (g_state == nullptr) return {};
    auto* rec = g_state->bodies.lookup(e);
    if (rec == nullptr) return {};
    return from_vec3(g_state->system->GetBodyInterface().GetLinearVelocity(rec->jolt_id));
}

bool is_on_ground(EntityID /*e*/) {
    // Real ground-check needs a downward shapecast or contact-listener
    // tracking; both arrive when the character controller does.
    return false;
}

void remove_body(EntityID e) {
    if (g_state == nullptr) return;
    JPH::BodyID id = g_state->bodies.remove(e);
    if (id.IsInvalid()) return;
    auto& bi = g_state->system->GetBodyInterface();
    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

void destroy_entity(Registry& registry, EntityID e) {
    remove_body(e);
    registry.destroy_entity(e);
}

uint32_t body_count() {
    if (g_state == nullptr) return 0;
    return g_state->bodies.live_count();
}

}  // namespace kuma::physics
