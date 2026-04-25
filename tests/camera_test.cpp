// Tests for kuma::Camera.
//
// Camera math is a good pure-logic boundary: no Vulkan, no SDL window,
// no GPU. If these matrices are wrong, rendering can look "almost"
// correct while controls feel subtly broken.

#include <kuma/camera.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using kuma::Camera;
using kuma::Mat4;
using kuma::Vec3;

namespace {

constexpr float kEps = 1e-5f;
constexpr float kMaxPitchRadians = 1.55334306f;

::testing::AssertionResult MatricesNear(const Mat4& a, const Mat4& b, float eps = kEps) {
    for (int i = 0; i < 16; i++) {
        if (std::fabs(a.data[i] - b.data[i]) > eps) {
            return ::testing::AssertionFailure()
                << "Mismatch at data[" << i << "]: " << a.data[i] << " vs " << b.data[i];
        }
    }
    return ::testing::AssertionSuccess();
}

std::array<float, 4> TransformPoint(const Mat4& m, float x, float y, float z, float w) {
    return {
        m(0, 0) * x + m(0, 1) * y + m(0, 2) * z + m(0, 3) * w,
        m(1, 0) * x + m(1, 1) * y + m(1, 2) * z + m(1, 3) * w,
        m(2, 0) * x + m(2, 1) * y + m(2, 2) * z + m(2, 3) * w,
        m(3, 0) * x + m(3, 1) * y + m(3, 2) * z + m(3, 3) * w,
    };
}

}  // namespace

TEST(Camera, DefaultBasisLooksDownNegativeZ) {
    Camera camera;

    Vec3 forward = camera.forward();
    Vec3 right = camera.right();
    Vec3 up = camera.up();

    EXPECT_NEAR(forward.x, 0.0f, kEps);
    EXPECT_NEAR(forward.y, 0.0f, kEps);
    EXPECT_NEAR(forward.z, -1.0f, kEps);

    EXPECT_NEAR(right.x, 1.0f, kEps);
    EXPECT_NEAR(right.y, 0.0f, kEps);
    EXPECT_NEAR(right.z, 0.0f, kEps);

    EXPECT_NEAR(up.x, 0.0f, kEps);
    EXPECT_NEAR(up.y, 1.0f, kEps);
    EXPECT_NEAR(up.z, 0.0f, kEps);
}

TEST(Camera, ViewMatrixMovesWorldOppositeCameraPosition) {
    Camera camera;
    camera.set_position({0.0f, 0.0f, 5.0f});

    auto origin_in_view = TransformPoint(camera.view(), 0.0f, 0.0f, 0.0f, 1.0f);

    EXPECT_NEAR(origin_in_view[0], 0.0f, kEps);
    EXPECT_NEAR(origin_in_view[1], 0.0f, kEps);
    EXPECT_NEAR(origin_in_view[2], -5.0f, kEps);
    EXPECT_NEAR(origin_in_view[3], 1.0f, kEps);
}

TEST(Camera, TranslateAccumulatesPosition) {
    Camera camera;
    camera.translate({1.0f, 2.0f, -3.0f});
    camera.translate({4.0f, -2.0f, 1.0f});

    Vec3 p = camera.position();
    EXPECT_FLOAT_EQ(p.x, 5.0f);
    EXPECT_FLOAT_EQ(p.y, 0.0f);
    EXPECT_FLOAT_EQ(p.z, 0.0f);
}

TEST(Camera, YawZeroFacesPositiveX) {
    Camera camera;
    camera.set_rotation(0.0f, 0.0f);

    Vec3 forward = camera.forward();
    EXPECT_NEAR(forward.x, 1.0f, kEps);
    EXPECT_NEAR(forward.y, 0.0f, kEps);
    EXPECT_NEAR(forward.z, 0.0f, kEps);
}

TEST(Camera, PitchIsClampedAwayFromVerticalSingularity) {
    Camera camera;
    camera.set_rotation(0.0f, 3.14159265f);

    EXPECT_NEAR(camera.pitch(), kMaxPitchRadians, kEps);

    camera.set_rotation(0.0f, -3.14159265f);
    EXPECT_NEAR(camera.pitch(), -kMaxPitchRadians, kEps);
}

TEST(Camera, ProjectionUsesPerspectiveSettings) {
    Camera camera;
    camera.set_perspective(0.5f, 2.0f, 0.25f, 50.0f);

    EXPECT_TRUE(MatricesNear(camera.projection(), Mat4::perspective(0.5f, 2.0f, 0.25f, 50.0f)));
}

TEST(Camera, SetAspectUpdatesProjectionAspectOnly) {
    Camera camera;
    camera.set_perspective(0.5f, 1.0f, 0.25f, 50.0f);
    camera.set_aspect(2.0f);

    EXPECT_TRUE(MatricesNear(camera.projection(), Mat4::perspective(0.5f, 2.0f, 0.25f, 50.0f)));
}

TEST(Camera, ViewProjectionIsProjectionTimesView) {
    Camera camera;
    camera.set_position({1.0f, 2.0f, 3.0f});
    camera.set_rotation(-1.0f, 0.25f);
    camera.set_perspective(0.7f, 1.5f, 0.1f, 200.0f);

    EXPECT_TRUE(MatricesNear(camera.view_projection(), camera.projection() * camera.view()));
}
