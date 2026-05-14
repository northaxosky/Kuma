// Integration tests for the character module's simulation loop and
// lifecycle. Drives kuma::character + kuma::physics directly with a
// fresh Registry per test - no window, no renderer, no SDL.

#include <gtest/gtest.h>

#include <kuma/character.h>
#include <kuma/ecs.h>
#include <kuma/physics.h>
#include <kuma/transform.h>

#include "test_helpers.h"

namespace {

constexpr float kStep = 1.0f / 60.0f;

// Wraps the character + physics init/shutdown pair. Order matters:
// physics first up, character first down. Always run via a fixture
// so a test that throws still tears state down cleanly.
class IntegrationCharacter : public ::testing::Test {
protected:
    void SetUp() override {
        kuma::physics::init(kuma::physics::PhysicsConfig{});
        kuma::character::init();
    }
    void TearDown() override {
        kuma::character::shutdown();
        kuma::physics::shutdown();
    }
};

// Common scene: a static floor box at y=0 (top surface at y=0.5)
// and one Character at the requested spawn position.
struct CharacterScene {
    kuma::EntityID floor;
    kuma::EntityID character;
};

CharacterScene make_scene(kuma::Registry& registry, float spawn_y = 5.0f) {
    CharacterScene s;

    s.floor = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({0.0f, 0.0f, 0.0f});
        registry.add(s.floor, t);

        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Static;
        body.shape = kuma::physics::BodyShape::Box;
        body.dimensions = {50.0f, 0.5f, 50.0f};
        body.layer = kuma::physics::PhysicsLayer::StaticWorld;
        registry.add(s.floor, body);
    }

    s.character = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({0.0f, spawn_y, 0.0f});
        registry.add(s.character, t);

        kuma::Character c;
        registry.add(s.character, c);
    }

    return s;
}

// Drive both simulations together, the way game code is supposed to:
// character first (so its pushes feed into the physics step), then
// physics. Repeat for n_steps fixed frames at 60 Hz.
void tick(kuma::Registry& registry, int n_steps) {
    for (int i = 0; i < n_steps; ++i) {
        kuma::character::simulate(kStep, registry);
        kuma::physics::simulate(kStep, registry);
    }
}

}  // namespace

TEST_F(IntegrationCharacter, CountStartsZero) {
    EXPECT_EQ(kuma::character::character_count(), 0u);
}

TEST_F(IntegrationCharacter, ControllerCreatedOnFirstSimulate) {
    kuma::Registry registry;
    auto scene = make_scene(registry);
    (void)scene;
    EXPECT_EQ(kuma::character::character_count(), 0u);
    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 1u);
}

TEST_F(IntegrationCharacter, GravityPullsCharacterDown) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 10.0f);
    const auto& transform = registry.get<kuma::Transform>(scene.character);
    const float start_y = transform.position().y;

    tick(registry, 10);

    EXPECT_LT(transform.position().y, start_y);
}

TEST_F(IntegrationCharacter, LandsAndDetectsGround) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 3.0f);

    // 3 seconds is more than enough to fall ~2.5m and settle.
    tick(registry, 180);

    const auto& c = registry.get<kuma::Character>(scene.character);
    EXPECT_TRUE(c.on_ground) << "Character should report grounded after settling";
}

TEST_F(IntegrationCharacter, DoesNotTunnelThroughFloor) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 4.0f);
    const auto& transform = registry.get<kuma::Transform>(scene.character);

    // Floor top surface is at y = 0.5. Character entity position is
    // at the capsule's bottom (we offset the shape upward), so the
    // entity y should never drop below the floor top.
    constexpr float kFloorTop = 0.5f;
    constexpr float kSlop = 0.05f;

    for (int i = 0; i < 240; ++i) {
        tick(registry, 1);
        EXPECT_GE(transform.position().y, kFloorTop - kSlop)
            << "Tunneled at step " << i;
    }
}

TEST_F(IntegrationCharacter, WalksHorizontallyOnGround) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 2.0f);

    // Let the character settle on the floor first.
    tick(registry, 60);

    const auto& transform = registry.get<kuma::Transform>(scene.character);
    const float start_x = transform.position().x;

    // Set wish_direction every frame - simulate consumes it each step.
    for (int i = 0; i < 60; ++i) {
        kuma::Character& c = registry.get<kuma::Character>(scene.character);
        c.wish_direction = {1.0f, 0.0f, 0.0f};
        tick(registry, 1);
    }

    EXPECT_GT(transform.position().x, start_x + 1.0f)
        << "Character should have moved several meters forward";
}

TEST_F(IntegrationCharacter, JumpProducesUpwardVelocity) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 2.0f);
    tick(registry, 120);  // settle on floor

    {
        kuma::Character& c = registry.get<kuma::Character>(scene.character);
        ASSERT_TRUE(c.on_ground);
        c.wish_jump = true;
    }

    // Single tick should consume the jump and lift off.
    tick(registry, 1);

    const auto& c = registry.get<kuma::Character>(scene.character);
    EXPECT_GT(c.velocity.y, 0.0f) << "Jump should produce upward velocity";
}

TEST_F(IntegrationCharacter, WishJumpDoesNotPersistWhenAirborne) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 3.0f);

    // Set wish_jump while airborne - simulate should consume + clear it,
    // and the character should NOT auto-jump on landing.
    kuma::Character& c = registry.get<kuma::Character>(scene.character);
    c.wish_jump = true;

    tick(registry, 1);
    EXPECT_FALSE(c.wish_jump) << "wish_jump should be cleared after consume";

    // Fall to the ground and let it settle.
    tick(registry, 180);

    EXPECT_TRUE(c.on_ground);
    // After settling, vertical velocity should be near zero - no auto-jump.
    EXPECT_LT(std::abs(c.velocity.y), 1.0f)
        << "Character auto-jumped on landing (wish_jump persisted)";
}

// Confidence sibling: same scene as LandsAndDetectsGround but checks
// velocity too. Catches the case where on_ground is correctly true
// but the integrated velocity field hasn't been clamped to zero.
TEST_F(IntegrationCharacter, BaselineSettlesFromSpawn3) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 3.0f);
    tick(registry, 180);
    const auto& c = registry.get<kuma::Character>(scene.character);
    EXPECT_TRUE(c.on_ground);
    EXPECT_LT(std::abs(c.velocity.y), 1.0f);
}

TEST_F(IntegrationCharacter, RemoveCharacterCleansUp) {
    kuma::Registry registry;
    auto scene = make_scene(registry);
    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 1u);

    kuma::character::remove_character(registry, scene.character);
    EXPECT_EQ(kuma::character::character_count(), 0u);

    const auto& c = registry.get<kuma::Character>(scene.character);
    EXPECT_FALSE(c.created);
}

TEST_F(IntegrationCharacter, RemovingCharacterTwiceIsSafe) {
    kuma::Registry registry;
    auto scene = make_scene(registry);
    tick(registry, 1);
    kuma::character::remove_character(registry, scene.character);
    kuma::character::remove_character(registry, scene.character);
    EXPECT_EQ(kuma::character::character_count(), 0u);
}

TEST_F(IntegrationCharacter, DestroyEntityHelperCleansUp) {
    kuma::Registry registry;
    auto scene = make_scene(registry);
    tick(registry, 1);

    kuma::character::destroy_entity(registry, scene.character);
    EXPECT_EQ(kuma::character::character_count(), 0u);
    EXPECT_FALSE(registry.is_valid(scene.character));
}

TEST_F(IntegrationCharacter, ValidateAliveSweepCatchesLeakedController) {
    kuma::Registry registry;
    auto scene = make_scene(registry);
    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 1u);

    // Direct destroy_entity bypasses character::remove_character.
    // The validate-alive sweep at the top of simulate should catch it.
    registry.destroy_entity(scene.character);
    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 0u);
}

TEST_F(IntegrationCharacter, MultipleCharactersCoexist) {
    kuma::Registry registry;

    // Floor
    {
        kuma::EntityID floor = registry.create_entity();
        kuma::Transform t;
        t.set_position({0.0f, 0.0f, 0.0f});
        registry.add(floor, t);
        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Static;
        body.shape = kuma::physics::BodyShape::Box;
        body.dimensions = {50.0f, 0.5f, 50.0f};
        body.layer = kuma::physics::PhysicsLayer::StaticWorld;
        registry.add(floor, body);
    }

    // 5 characters at different positions
    for (int i = 0; i < 5; ++i) {
        kuma::EntityID e = registry.create_entity();
        kuma::Transform t;
        t.set_position({static_cast<float>(i) * 4.0f, 5.0f, 0.0f});
        registry.add(e, t);
        kuma::Character c;
        registry.add(e, c);
    }

    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 5u);

    tick(registry, 120);
    EXPECT_EQ(kuma::character::character_count(), 5u);
}

TEST_F(IntegrationCharacter, ZeroDtDoesNotAdvance) {
    kuma::Registry registry;
    auto scene = make_scene(registry, 2.0f);
    tick(registry, 60);  // settle

    const auto& transform = registry.get<kuma::Transform>(scene.character);
    const kuma::Vec3 before = transform.position();

    for (int i = 0; i < 10; ++i) {
        kuma::character::simulate(0.0f, registry);
        kuma::physics::simulate(0.0f, registry);
    }

    EXPECT_FLOAT_EQ(transform.position().x, before.x);
    EXPECT_FLOAT_EQ(transform.position().y, before.y);
    EXPECT_FLOAT_EQ(transform.position().z, before.z);
}

TEST_F(IntegrationCharacter, YawWritesIntoTransformRotation) {
    kuma::Registry registry;
    auto scene = make_scene(registry);

    kuma::Character& c = registry.get<kuma::Character>(scene.character);
    c.yaw_radians = 1.5707963f;  // 90 degrees

    tick(registry, 1);

    const auto& transform = registry.get<kuma::Transform>(scene.character);
    // Quat from 90deg around Y has w = cos(45deg) ~= 0.707, y = sin(45deg) ~= 0.707
    EXPECT_NEAR(transform.rotation().w, 0.7071f, 0.01f);
    EXPECT_NEAR(transform.rotation().y, 0.7071f, 0.01f);
}

TEST_F(IntegrationCharacter, InvalidAuthoringClampedNotCrashed) {
    kuma::Registry registry;
    kuma::EntityID e = registry.create_entity();
    kuma::Transform t;
    t.set_position({0.0f, 5.0f, 0.0f});
    registry.add(e, t);
    kuma::Character c;
    c.radius = -1.0f;       // invalid
    c.half_height = 0.0f;   // invalid
    c.mass = -50.0f;        // invalid
    registry.add(e, c);

    // Floor
    {
        kuma::EntityID floor = registry.create_entity();
        kuma::Transform ft;
        ft.set_position({0.0f, 0.0f, 0.0f});
        registry.add(floor, ft);
        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Static;
        body.shape = kuma::physics::BodyShape::Box;
        body.dimensions = {50.0f, 0.5f, 50.0f};
        body.layer = kuma::physics::PhysicsLayer::StaticWorld;
        registry.add(floor, body);
    }

    tick(registry, 1);
    EXPECT_EQ(kuma::character::character_count(), 1u);
    const auto& clamped = registry.get<kuma::Character>(e);
    EXPECT_GT(clamped.radius, 0.0f);
    EXPECT_GT(clamped.half_height, 0.0f);
    EXPECT_GT(clamped.mass, 0.0f);
}
