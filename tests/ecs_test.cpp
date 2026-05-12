// Tests for kuma::Registry entity lifecycle.
//
// Components and views land in subsequent commits. This commit
// covers EntityID semantics and create/destroy/is_valid.

#include <kuma/ecs.h>

#include <gtest/gtest.h>

#include <unordered_set>

using kuma::EntityID;
using kuma::kInvalidEntity;
using kuma::Registry;

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
