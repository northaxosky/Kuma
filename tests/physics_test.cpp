// Tests for kuma::physics module lifecycle and configuration.
//
// Body operations and the simulation loop are exercised by the
// integration tests in tests/integration/ once the body store
// lands in commit 3. These unit tests cover what we have today:
// PhysicsConfig defaults are sane, init/shutdown are idempotent,
// and the body count starts at zero.

#include <kuma/physics.h>

#include <gtest/gtest.h>

namespace {

// Each test gets a fresh init/shutdown pair so module state never
// leaks across tests. Jolt's process-globals are guarded by
// std::call_once so they survive multiple init cycles cleanly.
class PhysicsLifecycleTest : public ::testing::Test {
protected:
    void TearDown() override {
        kuma::physics::shutdown();
    }
};

}  // namespace

TEST(PhysicsConfigDefaults, GravityPointsDown) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_EQ(config.gravity.x, 0.0f);
    EXPECT_LT(config.gravity.y, 0.0f);
    EXPECT_EQ(config.gravity.z, 0.0f);
}

TEST(PhysicsConfigDefaults, FixedStepIs60Hz) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_FLOAT_EQ(config.fixed_step, 1.0f / 60.0f);
}

TEST(PhysicsConfigDefaults, MaxStepsPreventSpiral) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_GT(config.max_steps_per_frame, 0u);
    EXPECT_LE(config.max_steps_per_frame, 16u);
}

TEST(PhysicsConfigDefaults, BodyLimitsAreNonZero) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_GT(config.max_bodies, 0u);
    EXPECT_GT(config.max_body_pairs, 0u);
    EXPECT_GT(config.max_contact_constraints, 0u);
}

TEST(PhysicsConfigDefaults, TempAllocatorIsAtLeastOneMB) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_GE(config.temp_allocator_bytes, 1024u * 1024u);
}

TEST_F(PhysicsLifecycleTest, InitShutdownRoundtrip) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_TRUE(kuma::physics::init(config));
    EXPECT_EQ(kuma::physics::body_count(), 0u);
    kuma::physics::shutdown();
    EXPECT_EQ(kuma::physics::body_count(), 0u);
}

TEST_F(PhysicsLifecycleTest, DoubleInitWarnsButSucceeds) {
    kuma::physics::PhysicsConfig config{};
    EXPECT_TRUE(kuma::physics::init(config));
    EXPECT_TRUE(kuma::physics::init(config));
    EXPECT_EQ(kuma::physics::body_count(), 0u);
}

TEST_F(PhysicsLifecycleTest, ShutdownWithoutInitIsSafe) {
    kuma::physics::shutdown();
    kuma::physics::shutdown();
    SUCCEED();
}

TEST_F(PhysicsLifecycleTest, RepeatedInitShutdownCyclesAreIdempotent) {
    kuma::physics::PhysicsConfig config{};
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(kuma::physics::init(config));
        EXPECT_EQ(kuma::physics::body_count(), 0u);
        kuma::physics::shutdown();
    }
}

TEST(PhysicsHandle, DefaultIsInvalid) {
    kuma::physics::PhysicsHandle handle{};
    EXPECT_FALSE(handle.valid());
}

TEST(PhysicsHandle, NonSentinelIndexIsValid) {
    kuma::physics::PhysicsHandle handle{};
    handle.index = 0;
    EXPECT_TRUE(handle.valid());
}

TEST(PhysicsBody, DefaultIsDynamicSphere) {
    kuma::physics::PhysicsBody body{};
    EXPECT_EQ(body.type, kuma::physics::BodyType::Dynamic);
    EXPECT_EQ(body.shape, kuma::physics::BodyShape::Sphere);
    EXPECT_FALSE(body.created);
    EXPECT_FALSE(body.handle.valid());
    EXPECT_GT(body.mass, 0.0f);
    EXPECT_GE(body.restitution, 0.0f);
    EXPECT_LE(body.restitution, 1.0f);
}

TEST(PhysicsLayer, EnumIsContiguousAndCountIsLast) {
    EXPECT_LT(static_cast<uint8_t>(kuma::physics::PhysicsLayer::StaticWorld),
              static_cast<uint8_t>(kuma::physics::PhysicsLayer::Count));
    EXPECT_LT(static_cast<uint8_t>(kuma::physics::PhysicsLayer::Sensor),
              static_cast<uint8_t>(kuma::physics::PhysicsLayer::Count));
}
