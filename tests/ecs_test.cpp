// Tests for kuma::Registry entity lifecycle and component CRUD.
//
// View<T...> queries land in Commit 3.

#include <kuma/ecs.h>

#include <gtest/gtest.h>

#include <unordered_set>

using kuma::EntityID;
using kuma::kInvalidEntity;
using kuma::Registry;

namespace {

// Plain-data component types for tests. POD-like as the discipline
// rule requires - no virtuals, no expensive destructors.
struct Position {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Velocity {
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
};

struct Tag {};  // empty marker component

}  // namespace

// ── EntityID ────────────────────────────────────────────────────

TEST(EntityID, DefaultIsInvalidEntity) {
    EntityID e;
    EXPECT_EQ(e, kInvalidEntity);
    EXPECT_EQ(e.id, 0u);
    EXPECT_EQ(e.generation, 0u);
}

TEST(EntityID, EqualityChecksBothIdAndGeneration) {
    EXPECT_EQ((EntityID{5, 1}), (EntityID{5, 1}));
    EXPECT_NE((EntityID{5, 1}), (EntityID{5, 2}));   // diff generation
    EXPECT_NE((EntityID{5, 1}), (EntityID{6, 1}));   // diff id
}

// ── Registry: create_entity ─────────────────────────────────────

TEST(Registry, CreateReturnsValidHandle) {
    Registry r;
    EntityID e = r.create_entity();
    EXPECT_TRUE(r.is_valid(e));
    EXPECT_NE(e, kInvalidEntity);
}

TEST(Registry, CreatedEntitiesGetUniqueIds) {
    Registry r;
    EntityID a = r.create_entity();
    EntityID b = r.create_entity();
    EntityID c = r.create_entity();
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
}

TEST(Registry, ValidEntitiesHaveGenerationOneOrMore) {
    // Reserved generation 0 means "never used" - real entities
    // must always have generation >= 1 so kInvalidEntity stays
    // permanently distinguishable.
    Registry r;
    for (int i = 0; i < 5; i++) {
        EntityID e = r.create_entity();
        EXPECT_GE(e.generation, 1u);
    }
}

// ── Registry: destroy + staleness ───────────────────────────────

TEST(Registry, DestroyMakesIsValidFalse) {
    Registry r;
    EntityID e = r.create_entity();
    ASSERT_TRUE(r.is_valid(e));
    r.destroy_entity(e);
    EXPECT_FALSE(r.is_valid(e));
}

TEST(Registry, DestroyOfInvalidIsNoOp) {
    // Mirrors set semantics: removing what isn't there is benign.
    Registry r;
    r.destroy_entity(kInvalidEntity);
    r.destroy_entity(EntityID{999, 999});  // out of range
    EntityID e = r.create_entity();
    r.destroy_entity(e);
    r.destroy_entity(e);  // double-destroy is benign
    EXPECT_FALSE(r.is_valid(e));
}

TEST(Registry, DestroyAndRecreateBumpsGeneration) {
    // Slot reuse is the whole point of generational handles. The
    // new entity may share the slot id but MUST have a different
    // generation, so the old handle fails is_valid().
    Registry r;
    EntityID original = r.create_entity();
    r.destroy_entity(original);
    EntityID reused = r.create_entity();

    EXPECT_EQ(reused.id, original.id);
    EXPECT_NE(reused.generation, original.generation);
    EXPECT_FALSE(r.is_valid(original));  // stale handle
    EXPECT_TRUE(r.is_valid(reused));
}

// ── Registry: invalid handle queries ────────────────────────────

TEST(Registry, InvalidEntityIsNeverValid) {
    Registry r;
    EXPECT_FALSE(r.is_valid(kInvalidEntity));
    r.create_entity();
    EXPECT_FALSE(r.is_valid(kInvalidEntity));  // still false after work
}

TEST(Registry, OutOfRangeHandleIsNotValid) {
    Registry r;
    r.create_entity();
    EXPECT_FALSE(r.is_valid(EntityID{9999, 1}));
}

// ── Registry: stress / churn ────────────────────────────────────

TEST(Registry, ChurnDoesNotCorruptHandles) {
    // Spawn-destroy-spawn loop: every live handle must remain
    // is_valid, every destroyed handle must remain !is_valid,
    // even after thousands of operations.
    Registry r;
    std::vector<EntityID> live;
    for (int i = 0; i < 1000; i++) {
        EntityID e = r.create_entity();
        live.push_back(e);
    }
    // Destroy half of them
    std::vector<EntityID> destroyed;
    for (size_t i = 0; i < live.size(); i += 2) {
        destroyed.push_back(live[i]);
        r.destroy_entity(live[i]);
    }
    // Spawn fresh ones to force slot reuse
    for (int i = 0; i < 500; i++) {
        EntityID e = r.create_entity();
        EXPECT_TRUE(r.is_valid(e));
    }
    // Original survivors still valid
    for (size_t i = 1; i < live.size(); i += 2) {
        EXPECT_TRUE(r.is_valid(live[i]));
    }
    // Destroyed handles still invalid (generations bumped)
    for (EntityID e : destroyed) {
        EXPECT_FALSE(r.is_valid(e));
    }
}

// ── Components: add / has / get ─────────────────────────────────

TEST(RegistryComponents, AddedComponentIsRetrievableViaGet) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{1.0f, 2.0f, 3.0f});

    EXPECT_TRUE(r.has<Position>(e));
    EXPECT_FLOAT_EQ(r.get<Position>(e).x, 1.0f);
    EXPECT_FLOAT_EQ(r.get<Position>(e).y, 2.0f);
    EXPECT_FLOAT_EQ(r.get<Position>(e).z, 3.0f);
}

TEST(RegistryComponents, HasReturnsFalseForMissingComponent) {
    Registry r;
    EntityID e = r.create_entity();
    EXPECT_FALSE(r.has<Position>(e));
    r.add<Position>(e, Position{});
    EXPECT_FALSE(r.has<Velocity>(e));  // different type, also missing
}

TEST(RegistryComponents, GetMutationPersistsAcrossCalls) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{0, 0, 0});

    r.get<Position>(e).x = 42.0f;
    EXPECT_FLOAT_EQ(r.get<Position>(e).x, 42.0f);
}

TEST(RegistryComponents, AddingExistingComponentOverwrites) {
    // Permissive choice: add() doubles as a setter. Spec'd behavior.
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{1, 1, 1});
    r.add<Position>(e, Position{9, 9, 9});

    EXPECT_TRUE(r.has<Position>(e));
    EXPECT_FLOAT_EQ(r.get<Position>(e).x, 9.0f);
}

// ── Components: try_get ─────────────────────────────────────────

TEST(RegistryComponents, TryGetReturnsPointerWhenPresent) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{7, 8, 9});

    Position* p = r.try_get<Position>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 7.0f);
}

TEST(RegistryComponents, TryGetReturnsNullWhenMissing) {
    Registry r;
    EntityID e = r.create_entity();
    EXPECT_EQ(r.try_get<Position>(e), nullptr);
    EXPECT_EQ(r.try_get<Velocity>(e), nullptr);
}

TEST(RegistryComponents, TryGetReturnsNullForInvalidEntity) {
    Registry r;
    EXPECT_EQ(r.try_get<Position>(kInvalidEntity), nullptr);
    EXPECT_EQ(r.try_get<Position>(EntityID{999, 1}), nullptr);
}

// ── Components: remove ──────────────────────────────────────────

TEST(RegistryComponents, RemoveMakesHasFalse) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{});
    ASSERT_TRUE(r.has<Position>(e));
    r.remove<Position>(e);
    EXPECT_FALSE(r.has<Position>(e));
}

TEST(RegistryComponents, RemoveOfMissingIsNoOp) {
    Registry r;
    EntityID e = r.create_entity();
    r.remove<Position>(e);          // never had it
    r.add<Position>(e, Position{});
    r.remove<Position>(e);
    r.remove<Position>(e);          // double-remove
    EXPECT_FALSE(r.has<Position>(e));
}

TEST(RegistryComponents, RemoveDoesNotAffectOtherComponentTypes) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{1, 2, 3});
    r.add<Velocity>(e, Velocity{4, 5, 6});

    r.remove<Position>(e);
    EXPECT_FALSE(r.has<Position>(e));
    EXPECT_TRUE(r.has<Velocity>(e));
    EXPECT_FLOAT_EQ(r.get<Velocity>(e).dx, 4.0f);
}

TEST(RegistryComponents, RemoveDoesNotAffectOtherEntities) {
    // Sparse-set remove uses swap-with-last; this test verifies the
    // swapped-in entity's sparse pointer is still correct.
    Registry r;
    EntityID a = r.create_entity();
    EntityID b = r.create_entity();
    EntityID c = r.create_entity();
    r.add<Position>(a, Position{1, 0, 0});
    r.add<Position>(b, Position{2, 0, 0});
    r.add<Position>(c, Position{3, 0, 0});

    r.remove<Position>(b);  // swap-with-last: c moves into b's old slot

    EXPECT_TRUE(r.has<Position>(a));
    EXPECT_FALSE(r.has<Position>(b));
    EXPECT_TRUE(r.has<Position>(c));
    EXPECT_FLOAT_EQ(r.get<Position>(a).x, 1.0f);
    EXPECT_FLOAT_EQ(r.get<Position>(c).x, 3.0f);
}

// ── Components: entity destruction strips components ────────────

TEST(RegistryComponents, DestroyEntityRemovesAllItsComponents) {
    // After destroy, the slot is reused. The new entity must NOT
    // inherit the old entity's components.
    Registry r;
    EntityID original = r.create_entity();
    r.add<Position>(original, Position{1, 2, 3});
    r.add<Velocity>(original, Velocity{4, 5, 6});

    r.destroy_entity(original);
    EntityID reused = r.create_entity();
    ASSERT_EQ(reused.id, original.id);  // same slot

    EXPECT_FALSE(r.has<Position>(reused));
    EXPECT_FALSE(r.has<Velocity>(reused));
}

// ── Empty / tag components ──────────────────────────────────────

TEST(RegistryComponents, EmptyTagComponentWorks) {
    Registry r;
    EntityID e = r.create_entity();
    r.add<Tag>(e, Tag{});
    EXPECT_TRUE(r.has<Tag>(e));
    r.remove<Tag>(e);
    EXPECT_FALSE(r.has<Tag>(e));
}

// ── Add to invalid entity is no-op ──────────────────────────────

TEST(RegistryComponents, AddToInvalidEntityIsNoOp) {
    Registry r;
    r.add<Position>(kInvalidEntity, Position{1, 2, 3});
    r.add<Position>(EntityID{999, 1}, Position{1, 2, 3});
    EXPECT_FALSE(r.has<Position>(kInvalidEntity));
}

// ── Views: single component ─────────────────────────────────────

TEST(RegistryView, SingleComponentVisitsAllMatching) {
    Registry r;
    EntityID a = r.create_entity();
    EntityID b = r.create_entity();
    EntityID c = r.create_entity();
    r.add<Position>(a, Position{1, 0, 0});
    r.add<Position>(b, Position{2, 0, 0});
    r.add<Position>(c, Position{3, 0, 0});

    std::unordered_set<float> seen;
    for (auto [e, pos] : r.view<Position>()) {
        seen.insert(pos.x);
        EXPECT_TRUE(r.is_valid(e));
    }
    EXPECT_EQ(seen.size(), 3u);
    EXPECT_TRUE(seen.count(1.0f));
    EXPECT_TRUE(seen.count(2.0f));
    EXPECT_TRUE(seen.count(3.0f));
}

TEST(RegistryView, SingleComponentSizeMatchesIteration) {
    Registry r;
    for (int i = 0; i < 5; i++) {
        EntityID e = r.create_entity();
        r.add<Position>(e, Position{});
    }
    EXPECT_EQ(r.view<Position>().size(), 5u);
    size_t count = 0;
    for ([[maybe_unused]] auto entry : r.view<Position>()) ++count;
    EXPECT_EQ(count, 5u);
}

// ── Views: multi-component intersection ─────────────────────────

TEST(RegistryView, MultiComponentVisitsOnlyEntitiesWithAll) {
    Registry r;
    EntityID both = r.create_entity();
    EntityID pos_only = r.create_entity();
    EntityID vel_only = r.create_entity();
    EntityID neither = r.create_entity();
    (void)neither;  // exists but has no relevant components

    r.add<Position>(both, Position{99, 0, 0});
    r.add<Velocity>(both, Velocity{1, 0, 0});
    r.add<Position>(pos_only, Position{0, 0, 0});
    r.add<Velocity>(vel_only, Velocity{0, 0, 0});

    size_t count = 0;
    for (auto [e, pos, vel] : r.view<Position, Velocity>()) {
        ++count;
        EXPECT_EQ(e, both);
        EXPECT_FLOAT_EQ(pos.x, 99.0f);
        EXPECT_FLOAT_EQ(vel.dx, 1.0f);
    }
    EXPECT_EQ(count, 1u);
    EXPECT_EQ((r.view<Position, Velocity>().size()), 1u);
}

TEST(RegistryView, MultiComponentEmptyIntersectionIteratesZeroTimes) {
    Registry r;
    EntityID a = r.create_entity();
    EntityID b = r.create_entity();
    r.add<Position>(a, Position{});  // a has only Position
    r.add<Velocity>(b, Velocity{});  // b has only Velocity

    size_t count = 0;
    for ([[maybe_unused]] auto entry : r.view<Position, Velocity>()) ++count;
    EXPECT_EQ(count, 0u);
    EXPECT_TRUE((r.view<Position, Velocity>().empty()));
}

// ── Views: edge cases ───────────────────────────────────────────

TEST(RegistryView, ViewWithUnusedComponentTypeIsEmpty) {
    // No entity has ever had a Tag - the pool was never created.
    // The view must yield zero results, not crash.
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{});

    EXPECT_TRUE(r.view<Tag>().empty());
    EXPECT_EQ(r.view<Tag>().size(), 0u);

    size_t count = 0;
    for ([[maybe_unused]] auto entry : r.view<Position, Tag>()) ++count;
    EXPECT_EQ(count, 0u);
}

TEST(RegistryView, EmptyRegistryHasEmptyView) {
    Registry r;
    EXPECT_TRUE(r.view<Position>().empty());
    EXPECT_EQ(r.view<Position>().size(), 0u);

    size_t count = 0;
    for ([[maybe_unused]] auto entry : r.view<Position>()) ++count;
    EXPECT_EQ(count, 0u);
}

TEST(RegistryView, MutationThroughViewPersists) {
    // Critical: the references yielded by the view must point at
    // real storage, not copies. Mutating through them must persist.
    Registry r;
    EntityID a = r.create_entity();
    EntityID b = r.create_entity();
    r.add<Position>(a, Position{1, 0, 0});
    r.add<Position>(b, Position{2, 0, 0});

    for (auto [e, pos] : r.view<Position>()) {
        pos.x += 100.0f;
    }

    EXPECT_FLOAT_EQ(r.get<Position>(a).x, 101.0f);
    EXPECT_FLOAT_EQ(r.get<Position>(b).x, 102.0f);
}

TEST(RegistryView, YieldedEntityIDIsCurrentlyValid) {
    // The EntityID the view yields must include the current
    // generation, so callers can store it / compare it / pass it
    // back to other Registry methods.
    Registry r;
    EntityID e = r.create_entity();
    r.add<Position>(e, Position{});

    bool checked = false;
    for (auto [yielded, pos] : r.view<Position>()) {
        EXPECT_EQ(yielded, e);
        EXPECT_TRUE(r.is_valid(yielded));
        EXPECT_FLOAT_EQ(r.get<Position>(yielded).x, pos.x);
        checked = true;
    }
    EXPECT_TRUE(checked);
}

TEST(RegistryView, ViewSurvivesSlotReuse) {
    // Spawn-destroy-spawn pattern. The view should iterate the
    // current set of Position-having entities, including the
    // reused slot (with its new generation).
    Registry r;
    EntityID old = r.create_entity();
    r.add<Position>(old, Position{50, 0, 0});
    r.destroy_entity(old);  // strips Position too

    EntityID fresh = r.create_entity();  // reuses slot
    r.add<Position>(fresh, Position{99, 0, 0});

    size_t count = 0;
    for (auto [e, pos] : r.view<Position>()) {
        ++count;
        EXPECT_EQ(e, fresh);  // generation matches the new entity
        EXPECT_NE(e, old);    // old handle not valid anymore
        EXPECT_FLOAT_EQ(pos.x, 99.0f);
    }
    EXPECT_EQ(count, 1u);
}

TEST(RegistryView, ThreeComponentIntersection) {
    // Verifies the variadic expansion handles 3+ components correctly.
    Registry r;
    EntityID all = r.create_entity();
    EntityID two = r.create_entity();

    r.add<Position>(all, Position{1, 0, 0});
    r.add<Velocity>(all, Velocity{2, 0, 0});
    r.add<Tag>(all, Tag{});

    r.add<Position>(two, Position{0, 0, 0});
    r.add<Velocity>(two, Velocity{0, 0, 0});
    // intentionally no Tag on `two`

    size_t count = 0;
    for (auto [e, pos, vel, tag] : r.view<Position, Velocity, Tag>()) {
        ++count;
        EXPECT_EQ(e, all);
        EXPECT_FLOAT_EQ(pos.x, 1.0f);
        EXPECT_FLOAT_EQ(vel.dx, 2.0f);
        (void)tag;
    }
    EXPECT_EQ(count, 1u);
}
