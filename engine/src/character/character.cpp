#include <kuma/character.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <kuma/log.h>
#include <kuma/transform.h>

#include "physics/native_context.h"

namespace kuma::character {

namespace {

// ── Per-character backend record ────────────────────────────────
// Each live Character owns one CharacterVirtual instance plus the
// capsule shape it was built from (Jolt holds a Ref to the shape;
// keeping a copy here keeps the lifetime obvious in our store).
struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    JPH::ShapeRefC shape;
    EntityID entity;
    bool in_use = false;
};

// ── CharacterStore ──────────────────────────────────────────────
// Mirrors physics::BodyStore: packed records + free-list +
// generation-checked entity lookup. Index-based removal so the
// validate-alive sweep still works after an ECS slot is reused.
class CharacterStore {
public:
    physics::PhysicsHandle add(EntityID entity,
                                JPH::Ref<JPH::CharacterVirtual> character,
                                JPH::ShapeRefC shape) {
        uint32_t index;
        if (!free_slots_.empty()) {
            index = free_slots_.back();
            free_slots_.pop_back();
            records_[index] = CharacterRecord{character, shape, entity, true};
        } else {
            index = static_cast<uint32_t>(records_.size());
            records_.push_back(CharacterRecord{character, shape, entity, true});
        }
        by_entity_[entity.id] = EntitySlot{entity.generation, index};
        ++live_count_;
        return physics::PhysicsHandle{index};
    }

    CharacterRecord* lookup(EntityID entity) {
        auto it = by_entity_.find(entity.id);
        if (it == by_entity_.end()) return nullptr;
        if (it->second.generation != entity.generation) return nullptr;
        auto& rec = records_[it->second.index];
        return rec.in_use ? &rec : nullptr;
    }

    bool remove(EntityID entity) {
        auto it = by_entity_.find(entity.id);
        if (it == by_entity_.end()) return false;
        if (it->second.generation != entity.generation) return false;
        const uint32_t index = it->second.index;
        by_entity_.erase(it);
        return remove_at(index);
    }

    bool remove_by_index(uint32_t index) {
        if (index >= records_.size() || !records_[index].in_use) {
            return false;
        }
        const EntityID owner = records_[index].entity;
        auto it = by_entity_.find(owner.id);
        if (it != by_entity_.end() && it->second.index == index) {
            by_entity_.erase(it);
        }
        return remove_at(index);
    }

    void clear() {
        records_.clear();
        free_slots_.clear();
        by_entity_.clear();
        live_count_ = 0;
    }

    uint32_t live_count() const { return live_count_; }
    const std::vector<CharacterRecord>& records() const { return records_; }

private:
    struct EntitySlot {
        uint32_t generation;
        uint32_t index;
    };

    bool remove_at(uint32_t index) {
        auto& rec = records_[index];
        rec.in_use = false;
        rec.character = nullptr;  // releases Jolt's Ref
        rec.shape = nullptr;
        free_slots_.push_back(index);
        --live_count_;
        return true;
    }

    std::vector<CharacterRecord> records_;
    std::vector<uint32_t> free_slots_;
    std::unordered_map<uint32_t, EntitySlot> by_entity_;
    uint32_t live_count_ = 0;
};

// ── Module state ────────────────────────────────────────────────
struct State {
    const physics::detail::PhysicsNativeContext* physics_native = nullptr;
    CharacterStore characters;

    // Same accumulator pattern physics uses, with the same fixed
    // step. Character + physics drain in lockstep so contact
    // interactions stay consistent across frame rate fluctuation.
    float accumulator = 0.0f;
    float fixed_step = 1.0f / 60.0f;
    uint32_t max_steps_per_frame = 4;
};

State* g_state = nullptr;

// ── Math conversions ────────────────────────────────────────────
inline JPH::RVec3 to_rvec3(const Vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
inline JPH::Vec3 to_vec3(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline Vec3 from_vec3(const JPH::Vec3& v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}
inline Vec3 from_rvec3(const JPH::RVec3& v) {
    return {static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())};
}
inline Quat yaw_to_quat(float yaw_radians) {
    return Quat::from_axis_angle({0.0f, 1.0f, 0.0f}, yaw_radians);
}
inline JPH::Quat to_jph_quat(const Quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

// ── Validation ──────────────────────────────────────────────────
// Authoring values can be hand-edited or imported from data; clamp
// the geometry-relevant ones to safe ranges so a typo doesn't
// crash Jolt or produce a phantom upside-down character.
void clamp_authoring_fields(Character& c, EntityID entity) {
    bool warned = false;
    auto warn = [&](const char* field) {
        if (warned) return;
        kuma::log::warn(
            "character: clamped invalid authoring fields on entity %u (%s)",
            entity.id, field);
        warned = true;
    };

    if (c.radius <= 0.0f) {
        c.radius = 0.35f;
        warn("radius");
    }
    if (c.half_height <= 0.0f) {
        c.half_height = 0.55f;
        warn("half_height");
    }
    if (c.step_height < 0.0f) {
        c.step_height = 0.0f;
        warn("step_height");
    }
    if (c.step_height > c.radius * 2.0f) {
        c.step_height = c.radius * 2.0f;
        warn("step_height > 2 * radius");
    }
    if (c.max_slope_radians <= 0.0f || c.max_slope_radians >= 1.5707963f) {
        c.max_slope_radians = 0.78f;
        warn("max_slope_radians");
    }
    if (c.mass <= 0.0f) {
        c.mass = 70.0f;
        warn("mass");
    }
}

// ── Capsule shape ───────────────────────────────────────────────
// Build a capsule shape oriented with its axis along world +Y and
// its bottom at the entity's Transform position. The translation
// pushes the capsule UP by (half_height + radius) so the entity
// position is at the feet, which matches the FPS-eye-height idiom.
JPH::ShapeRefC build_capsule(const Character& c) {
    JPH::Ref<JPH::CapsuleShapeSettings> capsule_settings =
        new JPH::CapsuleShapeSettings(c.half_height, c.radius);
    JPH::ShapeRefC capsule = capsule_settings->Create().Get();

    JPH::Ref<JPH::RotatedTranslatedShapeSettings> wrapper =
        new JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0.0f, c.half_height + c.radius, 0.0f),
            JPH::Quat::sIdentity(),
            capsule);
    return wrapper->Create().Get();
}

// ── Body filters for CharacterVirtual::Update ───────────────────
// CharacterVirtual asks for these filters once per Update so it
// can decide what to collide with. We delegate to the physics
// module's existing layer matrix - no custom filtering yet.
class AcceptAllBroadPhase final : public JPH::BroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::BroadPhaseLayer /*layer*/) const override {
        return true;
    }
};

class AcceptAllObjects final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer /*layer*/) const override {
        return true;
    }
};

class AcceptAllBodies final : public JPH::BodyFilter {};

class AcceptAllShapes final : public JPH::ShapeFilter {};

// ── Lifecycle helpers ───────────────────────────────────────────
// Tear down every live record. Called both from shutdown() and
// from any future "reset world" path; keeping it private so the
// public API stays small.
void destroy_all_records() {
    g_state->characters.clear();
}

// Validate-alive sweep. Runs at the START of simulate() (unlike
// physics, which runs it after) because a stale CharacterVirtual
// being Updated against a dead entity would assert inside Jolt.
void prune_stale_characters(Registry& registry) {
    if (g_state->characters.live_count() == 0) return;

    std::vector<uint32_t> dead_indices;
    const auto& records = g_state->characters.records();
    for (uint32_t i = 0; i < records.size(); ++i) {
        const auto& rec = records[i];
        if (!rec.in_use) continue;
        if (!registry.is_valid(rec.entity) ||
            !registry.has<Character>(rec.entity)) {
            dead_indices.push_back(i);
        }
    }

    for (uint32_t i : dead_indices) {
        g_state->characters.remove_by_index(i);
    }

    if (!dead_indices.empty()) {
        kuma::log::warn(
            "character: cleaned up %zu leaked controllers (caller forgot to call remove_character / destroy_entity)",
            dead_indices.size());
    }
}

// Lazy-create CharacterVirtual instances for any new Character
// component that has a Transform. Missing-Transform components
// log once and are skipped (will be picked up next frame if a
// Transform is added).
void create_pending_characters(Registry& registry) {
    for (auto [entity, character, transform] : registry.view<Character, Transform>()) {
        if (character.created) continue;

        clamp_authoring_fields(character, entity);

        JPH::ShapeRefC shape = build_capsule(character);
        if (shape == nullptr) {
            kuma::log::error(
                "character: failed to build capsule shape on entity %u",
                entity.id);
            continue;
        }

        JPH::Ref<JPH::CharacterVirtualSettings> settings =
            new JPH::CharacterVirtualSettings();
        settings->mShape = shape;
        settings->mUp = JPH::Vec3::sAxisY();
        settings->mMaxSlopeAngle = character.max_slope_radians;
        settings->mMass = character.mass;
        // Plane perpendicular to Up with "supporting volume" cone -
        // standard idiom for upright capsules so the character can
        // walk with its bottom touching the ground.
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -character.radius);

        JPH::Ref<JPH::CharacterVirtual> jch = new JPH::CharacterVirtual(
            settings,
            to_rvec3(transform.position()),
            to_jph_quat(yaw_to_quat(character.yaw_radians)),
            g_state->physics_native->system);

        character.handle = g_state->characters.add(entity, jch, shape);
        character.created = true;
    }
}

// Run one fixed character step for every live controller.
// Reads / consumes wish_direction + wish_jump, integrates gravity
// manually (Jolt's CharacterVirtual::Update only uses gravity for
// slope detection, not velocity integration), invokes Update,
// reads back ground state + velocity + position.
void step_one(Registry& registry, float step) {
    const Vec3 gravity = physics::gravity();
    const JPH::Vec3 jph_gravity = to_vec3(gravity);

    AcceptAllBroadPhase bp_filter;
    AcceptAllObjects obj_filter;
    AcceptAllBodies body_filter;
    AcceptAllShapes shape_filter;

    for (auto [entity, character, transform] : registry.view<Character, Transform>()) {
        if (!character.created) continue;
        auto* rec = g_state->characters.lookup(entity);
        if (rec == nullptr) continue;

        // Consume per-frame inputs at the top so a missed simulate()
        // can never re-apply yesterday's jump request.
        Vec3 wish_dir = character.wish_direction;
        const bool wish_jump = character.wish_jump;
        character.wish_direction = {0, 0, 0};
        character.wish_jump = false;

        // Flatten to horizontal plane and clamp to unit length so a
        // diagonal input doesn't get sqrt(2)-faster motion.
        wish_dir.y = 0.0f;
        const float wish_len_sq =
            wish_dir.x * wish_dir.x + wish_dir.z * wish_dir.z;
        if (wish_len_sq > 1.0f) {
            const float inv_len = 1.0f / std::sqrt(wish_len_sq);
            wish_dir.x *= inv_len;
            wish_dir.z *= inv_len;
        }

        const bool grounded = character.on_ground;

        // Read current velocity from Jolt then split horizontal vs
        // vertical so we can drive each independently.
        const JPH::Vec3 current_v = rec->character->GetLinearVelocity();
        Vec3 horizontal{current_v.GetX(), 0.0f, current_v.GetZ()};
        float vertical_y = current_v.GetY();

        if (grounded) {
            // Snap to target on the ground - tight FPS feel.
            horizontal.x = wish_dir.x * character.walk_speed;
            horizontal.z = wish_dir.z * character.walk_speed;
        } else {
            // Air: accelerate toward target without ever exceeding
            // walk_speed in the wish direction. Preserves horizontal
            // momentum from jumps - you keep moving forward through
            // the arc, with limited steering.
            const Vec3 target = {wish_dir.x * character.walk_speed,
                                  0.0f,
                                  wish_dir.z * character.walk_speed};
            const Vec3 delta = {target.x - horizontal.x,
                                 0.0f,
                                 target.z - horizontal.z};
            const float delta_len_sq = delta.x * delta.x + delta.z * delta.z;
            const float max_step = character.air_acceleration * step;
            if (delta_len_sq > max_step * max_step) {
                const float inv = max_step / std::sqrt(delta_len_sq);
                horizontal.x += delta.x * inv;
                horizontal.z += delta.z * inv;
            } else {
                horizontal.x = target.x;
                horizontal.z = target.z;
            }
        }

        // Apply gravity to vertical velocity. (CharacterVirtual
        // does not integrate gravity itself - it only consults the
        // gravity vector for slope-side detection.)
        vertical_y += gravity.y * step;

        // Jump only on the same step as the request fires AND we're
        // grounded - this keeps "press jump while airborne, then
        // land" from auto-jumping.
        if (wish_jump && grounded) {
            vertical_y = character.jump_speed;
        }

        rec->character->SetLinearVelocity(
            JPH::Vec3(horizontal.x, vertical_y, horizontal.z));
        rec->character->SetRotation(to_jph_quat(yaw_to_quat(character.yaw_radians)));

        rec->character->Update(
            step,
            jph_gravity,
            bp_filter,
            obj_filter,
            body_filter,
            shape_filter,
            *g_state->physics_native->temp_allocator);

        // Sync back: ground state, velocity, transform.
        const auto ground_state = rec->character->GetGroundState();
        character.on_ground = ground_state == JPH::CharacterBase::EGroundState::OnGround;
        character.on_steep =
            ground_state == JPH::CharacterBase::EGroundState::OnSteepGround;

        const JPH::Vec3 new_v = rec->character->GetLinearVelocity();
        character.velocity = from_vec3(new_v);

        transform.set_position(from_rvec3(rec->character->GetPosition()));
        transform.set_rotation(yaw_to_quat(character.yaw_radians));
    }
}

}  // namespace

bool init() {
    if (g_state != nullptr) {
        kuma::log::warn("character::init called twice; ignoring");
        return true;
    }

    void* raw = physics::native_context();
    if (raw == nullptr) {
        kuma::log::error(
            "character::init requires physics to be initialized first");
        return false;
    }

    auto state = std::make_unique<State>();
    state->physics_native =
        static_cast<const physics::detail::PhysicsNativeContext*>(raw);
    g_state = state.release();

    kuma::log::info("Character module initialized");
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;
    destroy_all_records();
    delete g_state;
    g_state = nullptr;
    kuma::log::info("Character module shut down");
}

void simulate(float dt, Registry& registry) {
    if (g_state == nullptr) return;
    if (dt < 0.0f) dt = 0.0f;

    // Sweep dead controllers first so we don't Update against a
    // stale CharacterVirtual whose entity is gone.
    prune_stale_characters(registry);
    create_pending_characters(registry);

    g_state->accumulator += dt;
    const float step = g_state->fixed_step;

    uint32_t steps = 0;
    while (g_state->accumulator >= step && steps < g_state->max_steps_per_frame) {
        step_one(registry, step);
        g_state->accumulator -= step;
        ++steps;
    }

    if (steps == g_state->max_steps_per_frame && g_state->accumulator >= step) {
        g_state->accumulator = 0.0f;
    }
}

void remove_character(Registry& registry, EntityID e) {
    if (g_state == nullptr) return;
    g_state->characters.remove(e);
    if (Character* c = registry.try_get<Character>(e)) {
        c->created = false;
        c->handle = {};
        c->velocity = {};
        c->on_ground = false;
        c->on_steep = false;
    }
}

void destroy_entity(Registry& registry, EntityID e) {
    remove_character(registry, e);
    registry.destroy_entity(e);
}

uint32_t character_count() {
    if (g_state == nullptr) return 0;
    return g_state->characters.live_count();
}

}  // namespace kuma::character
