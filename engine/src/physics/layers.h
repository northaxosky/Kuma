#pragma once

// ── Physics layer filters ───────────────────────────────────────
// Jolt asks the consumer to provide three filter classes:
//
//   BroadPhaseLayerInterface     ObjectLayer -> BroadPhaseLayer
//   ObjectVsBroadPhaseLayerFilter Should ObjectLayer collide with a BPLayer?
//   ObjectLayerPairFilter         Should ObjectLayer A collide with ObjectLayer B?
//
// We collapse our 8 PhysicsLayer values into 2 broadphase buckets
// (NON_MOVING / MOVING) because a body's broadphase layer changes
// rarely - any non-Static layer goes into MOVING. Pair filtering
// then lives in a small symmetric matrix indexed by ObjectLayer.

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <kuma/physics.h>

namespace kuma::physics::detail {

// Two broadphase layers is the standard Jolt recipe and matches
// what the official sample code uses. Static colliders live in
// NON_MOVING so the broadphase can skip them on update.
namespace broad_phase {
constexpr JPH::BroadPhaseLayer kNonMoving{0};
constexpr JPH::BroadPhaseLayer kMoving{1};
constexpr JPH::uint kNumLayers = 2;
}  // namespace broad_phase

// Convenience: cast a kuma PhysicsLayer to JPH's underlying type.
inline JPH::ObjectLayer to_jph(PhysicsLayer layer) {
    return static_cast<JPH::ObjectLayer>(layer);
}

// ── BroadPhaseLayerInterface ────────────────────────────────────
// Jolt asks "what broadphase bucket does this object layer go in?"
// every time a body is created or moves. Lookup is a small array.
class KumaBPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    KumaBPLayerInterface() {
        for (auto& slot : object_to_bp_) slot = broad_phase::kMoving;
        object_to_bp_[to_jph(PhysicsLayer::StaticWorld)] = broad_phase::kNonMoving;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return broad_phase::kNumLayers; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        JPH_ASSERT(layer < static_cast<JPH::ObjectLayer>(PhysicsLayer::Count));
        return object_to_bp_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        switch (layer.GetValue()) {
            case 0: return "NonMoving";
            case 1: return "Moving";
            default: return "?";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer object_to_bp_[static_cast<size_t>(PhysicsLayer::Count)]{};
};

// ── ObjectVsBroadPhaseLayerFilter ───────────────────────────────
// Cheap pre-filter consulted before the per-pair matrix. NonMoving
// objects only need to be tested against Moving (statics never
// collide with each other), so we short-circuit that here.
class KumaObjectVsBPLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer bp) const override {
        if (object == to_jph(PhysicsLayer::StaticWorld)) {
            return bp == broad_phase::kMoving;
        }
        return true;
    }
};

// ── ObjectLayerPairFilter ───────────────────────────────────────
// Final say on whether two specific layers collide. Backed by a
// symmetric NxN bool matrix that is built once at construction. To
// enable a new pair, add a row in the constructor's pair list.
class KumaObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    KumaObjectPairFilter() {
        constexpr struct {
            PhysicsLayer a;
            PhysicsLayer b;
        } kEnabledPairs[] = {
            {PhysicsLayer::StaticWorld, PhysicsLayer::Dynamic},
            {PhysicsLayer::Dynamic,     PhysicsLayer::Dynamic},
        };
        for (const auto& pair : kEnabledPairs) {
            const auto i = static_cast<size_t>(pair.a);
            const auto j = static_cast<size_t>(pair.b);
            matrix_[i][j] = true;
            matrix_[j][i] = true;
        }
    }

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        JPH_ASSERT(a < static_cast<JPH::ObjectLayer>(PhysicsLayer::Count));
        JPH_ASSERT(b < static_cast<JPH::ObjectLayer>(PhysicsLayer::Count));
        return matrix_[a][b];
    }

private:
    static constexpr size_t kCount = static_cast<size_t>(PhysicsLayer::Count);
    bool matrix_[kCount][kCount]{};
};

}  // namespace kuma::physics::detail
