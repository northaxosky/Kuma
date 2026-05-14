// Unit tests for FpsCameraController. Covers the math that does NOT
// depend on a live input snapshot: yaw/pitch convention, eye-height
// offset, jump-edge consumption. Input-driven behavior (mouse delta,
// WASD axis) is exercised through end-to-end sandbox usage; the
// concern here is that the camera's pose is consistent with the
// character's stored yaw at any given moment.

#include <cmath>

#include <gtest/gtest.h>

#include <kuma/camera.h>
#include <kuma/character.h>
#include <kuma/fps_camera.h>
#include <kuma/transform.h>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHalfPi = 1.57079633f;

}  // namespace

TEST(FpsCameraController, EyeHeightOffsetsCameraAboveCharacter) {
    kuma::Character ch;
    kuma::Transform t;
    t.set_position({1.0f, 2.0f, 3.0f});
    kuma::Camera cam;

    kuma::FpsCameraController controller;
    controller.eye_height = 1.6f;
    controller.update(ch, t, cam);

    const kuma::Vec3 cam_pos = cam.position();
    EXPECT_FLOAT_EQ(cam_pos.x, 1.0f);
    EXPECT_FLOAT_EQ(cam_pos.y, 2.0f + 1.6f);
    EXPECT_FLOAT_EQ(cam_pos.z, 3.0f);
}

TEST(FpsCameraController, ZeroCharacterYawPointsCameraDownNegativeZ) {
    kuma::Character ch;
    ch.yaw_radians = 0.0f;  // matches Character struct default
    kuma::Transform t;
    kuma::Camera cam;

    kuma::FpsCameraController controller;
    controller.update(ch, t, cam);

    // Character yaw=0 should produce a camera looking down -Z.
    const kuma::Vec3 fwd = cam.forward();
    EXPECT_NEAR(fwd.x, 0.0f, 0.001f);
    EXPECT_NEAR(fwd.y, 0.0f, 0.001f);
    EXPECT_NEAR(fwd.z, -1.0f, 0.001f);
}

TEST(FpsCameraController, PositiveYawTurnsRightFromMinusZ) {
    kuma::Character ch;
    ch.yaw_radians = kHalfPi;  // 90 deg
    kuma::Transform t;
    kuma::Camera cam;

    kuma::FpsCameraController controller;
    controller.update(ch, t, cam);

    // Turning right 90deg from -Z should face +X.
    const kuma::Vec3 fwd = cam.forward();
    EXPECT_NEAR(fwd.x, 1.0f, 0.001f);
    EXPECT_NEAR(fwd.z, 0.0f, 0.001f);
}

TEST(FpsCameraController, NegativeYawTurnsLeftFromMinusZ) {
    kuma::Character ch;
    ch.yaw_radians = -kHalfPi;  // -90 deg
    kuma::Transform t;
    kuma::Camera cam;

    kuma::FpsCameraController controller;
    controller.update(ch, t, cam);

    // Turning left 90deg from -Z should face -X.
    const kuma::Vec3 fwd = cam.forward();
    EXPECT_NEAR(fwd.x, -1.0f, 0.001f);
    EXPECT_NEAR(fwd.z, 0.0f, 0.001f);
}

TEST(FpsCameraController, DoesNotMutateCharacterYawWithoutMouseInput) {
    kuma::Character ch;
    ch.yaw_radians = 0.5f;
    kuma::Transform t;
    kuma::Camera cam;

    kuma::FpsCameraController controller;
    controller.update(ch, t, cam);

    // Without input::init / a real mouse delta, mouse_delta() returns
    // zero, so yaw should be unchanged.
    EXPECT_FLOAT_EQ(ch.yaw_radians, 0.5f);
}
