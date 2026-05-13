// Integration tests for the physics module's simulation loop and
// body lifecycle. Drives kuma::physics directly with a fresh
// Registry per test - no window, no renderer, no SDL.

#include <gtest/gtest.h>

#include <kuma/ecs.h>
#include <kuma/physics.h>
#include <kuma/transform.h>

#include "test_helpers.h"

namespace {

// Fixed-step matching physics::PhysicsConfig::fixed_step. Tests
// call simulate(kStep, registry) repeatedly so the per-frame
// accumulator never carries leftover time between asserts.
constexpr float kStep = 1.0f / 60.0f;

// Common scene: a static floor box covering a generous area at y=0
// and a dynamic sphere falling from height. Registry can't be
// returned by value (non-copyable) so we take it by reference and
// hand back the two entity ids in a small struct.
struct FallingSphereEntities {
    kuma::EntityID floor;
    kuma::EntityID sphere;
};

FallingSphereEntities make_falling_sphere_scene(kuma::Registry& registry, float drop_height = 10.0f) {
    FallingSphereEntities ids;

    ids.floor = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({0.0f, 0.0f, 0.0f});
        registry.add(ids.floor, t);

        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Static;
        body.shape = kuma::physics::BodyShape::Box;
        body.dimensions = {50.0f, 0.5f, 50.0f};
        body.layer = kuma::physics::PhysicsLayer::StaticWorld;
        registry.add(ids.floor, body);
    }

    ids.sphere = registry.create_entity();
    {
        kuma::Transform t;
        t.set_position({0.0f, drop_height, 0.0f});
        registry.add(ids.sphere, t);

        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Dynamic;
        body.shape = kuma::physics::BodyShape::Sphere;
        body.dimensions = {0.5f, 0.0f, 0.0f};
        body.restitution = 0.3f;
        body.layer = kuma::physics::PhysicsLayer::Dynamic;
        registry.add(ids.sphere, body);
    }

    return ids;
}

class IntegrationPhysicsSimulation : public ::testing::Test {
protected:
    void SetUp() override {
        kuma::physics::init(kuma::physics::PhysicsConfig{});
    }
    void TearDown() override {
        kuma::physics::shutdown();
    }
};

}  // namespace

TEST_F(IntegrationPhysicsSimulation, BodyCountStartsZero) {
    EXPECT_EQ(kuma::physics::body_count(), 0u);
}

TEST_F(IntegrationPhysicsSimulation, BodiesGetCreatedOnFirstSimulate) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    (void)scene;
    EXPECT_EQ(kuma::physics::body_count(), 0u);
    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 2u);
}

TEST_F(IntegrationPhysicsSimulation, GravityPullsDynamicBodyDown) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry, 20.0f);
    const auto& transform = registry.get<kuma::Transform>(scene.sphere);
    const float start_y = transform.position().y;

    for (int i = 0; i < 30; ++i) {  // ~0.5s of sim
        kuma::physics::simulate(kStep, registry);
    }

    const float later_y = transform.position().y;
    EXPECT_LT(later_y, start_y) << "Dynamic body should fall under gravity";
}

TEST_F(IntegrationPhysicsSimulation, BodyDoesNotTunnelThroughFloor) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry, 8.0f);
    const auto& transform = registry.get<kuma::Transform>(scene.sphere);

    constexpr float kFloorTopY = 0.5f;
    constexpr float kSphereRadius = 0.5f;
    // Allow a small interpenetration tolerance for the contact-resolver slop.
    constexpr float kSlop = 0.1f;

    for (int i = 0; i < 240; ++i) {  // 4 seconds, generous landing time
        kuma::physics::simulate(kStep, registry);
        const float y = transform.position().y;
        EXPECT_GE(y, kFloorTopY + kSphereRadius - kSlop)
            << "Sphere tunneled through floor on step " << i;
    }
}

TEST_F(IntegrationPhysicsSimulation, BodyComesToRestOnFloor) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry, 5.0f);
    const auto& transform = registry.get<kuma::Transform>(scene.sphere);

    for (int i = 0; i < 300; ++i) {  // 5s should be ample for restitution=0.3 to settle
        kuma::physics::simulate(kStep, registry);
    }

    const kuma::Vec3 velocity = kuma::physics::get_linear_velocity(scene.sphere);
    EXPECT_NEAR(velocity.x, 0.0f, 0.05f);
    EXPECT_NEAR(velocity.y, 0.0f, 0.05f);
    EXPECT_NEAR(velocity.z, 0.0f, 0.05f);

    const float y = transform.position().y;
    EXPECT_GT(y, 0.0f) << "Sphere settled below floor";
    EXPECT_LT(y, 2.0f) << "Sphere not resting on floor";
}

TEST_F(IntegrationPhysicsSimulation, StaticBodyDoesNotMove) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    const auto& floor_transform = registry.get<kuma::Transform>(scene.floor);
    const kuma::Vec3 start = floor_transform.position();

    for (int i = 0; i < 60; ++i) {
        kuma::physics::simulate(kStep, registry);
    }

    const kuma::Vec3 end = floor_transform.position();
    EXPECT_FLOAT_EQ(start.x, end.x);
    EXPECT_FLOAT_EQ(start.y, end.y);
    EXPECT_FLOAT_EQ(start.z, end.z);
}

TEST_F(IntegrationPhysicsSimulation, ApplyImpulseAffectsVelocity) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);  // create the body

    kuma::physics::apply_impulse(scene.sphere, {5.0f, 0.0f, 0.0f});
    kuma::physics::simulate(kStep, registry);

    const kuma::Vec3 velocity = kuma::physics::get_linear_velocity(scene.sphere);
    EXPECT_GT(velocity.x, 0.0f) << "Impulse should produce +x velocity";
}

TEST_F(IntegrationPhysicsSimulation, RemoveBodyCleansUp) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 2u);

    kuma::physics::remove_body(scene.sphere);
    EXPECT_EQ(kuma::physics::body_count(), 1u);

    kuma::physics::remove_body(scene.floor);
    EXPECT_EQ(kuma::physics::body_count(), 0u);
}

TEST_F(IntegrationPhysicsSimulation, RemovingBodyTwiceIsSafe) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);
    kuma::physics::remove_body(scene.sphere);
    kuma::physics::remove_body(scene.sphere);
    EXPECT_EQ(kuma::physics::body_count(), 1u);
}

TEST_F(IntegrationPhysicsSimulation, DestroyEntityHelperCleansUpBody) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 2u);

    kuma::physics::destroy_entity(registry, scene.sphere);
    EXPECT_EQ(kuma::physics::body_count(), 1u);
    EXPECT_FALSE(registry.is_valid(scene.sphere));
}

TEST_F(IntegrationPhysicsSimulation, ValidateAliveSweepCatchesLeakedBody) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 2u);

    // Direct destroy_entity without remove_body - this is the "leak"
    // path that the validate-alive sweep is meant to catch.
    registry.destroy_entity(scene.sphere);
    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 1u);
}

TEST_F(IntegrationPhysicsSimulation, ManyBodiesCoexist) {
    kuma::Registry registry;

    // Floor
    kuma::EntityID floor = registry.create_entity();
    {
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

    // 50 spheres at different positions
    for (int i = 0; i < 50; ++i) {
        kuma::EntityID e = registry.create_entity();
        kuma::Transform t;
        t.set_position({static_cast<float>(i % 10) * 2.0f, 10.0f, static_cast<float>(i / 10) * 2.0f});
        registry.add(e, t);

        kuma::physics::PhysicsBody body;
        body.type = kuma::physics::BodyType::Dynamic;
        body.shape = kuma::physics::BodyShape::Sphere;
        body.dimensions = {0.5f, 0.0f, 0.0f};
        body.layer = kuma::physics::PhysicsLayer::Dynamic;
        registry.add(e, body);
    }

    kuma::physics::simulate(kStep, registry);
    EXPECT_EQ(kuma::physics::body_count(), 51u);

    for (int i = 0; i < 60; ++i) {
        kuma::physics::simulate(kStep, registry);
    }
    EXPECT_EQ(kuma::physics::body_count(), 51u);
}

TEST_F(IntegrationPhysicsSimulation, ZeroDtDoesNotAdvanceSimulation) {
    kuma::Registry registry;
    auto scene = make_falling_sphere_scene(registry);
    kuma::physics::simulate(kStep, registry);  // create body, run one step

    const auto& transform = registry.get<kuma::Transform>(scene.sphere);
    const kuma::Vec3 pos_before = transform.position();

    for (int i = 0; i < 10; ++i) {
        kuma::physics::simulate(0.0f, registry);
    }

    const kuma::Vec3 pos_after = transform.position();
    EXPECT_FLOAT_EQ(pos_before.x, pos_after.x);
    EXPECT_FLOAT_EQ(pos_before.y, pos_after.y);
    EXPECT_FLOAT_EQ(pos_before.z, pos_after.z);
}
