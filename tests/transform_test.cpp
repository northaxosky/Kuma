// Tests for kuma::Transform.
//
// Transform composes Vec3/Quat/Mat4 - already covered by math_test.cpp -
// so these tests focus on the wiring: defaults, setter behavior, and
// the all-important T*R*S composition order.

#include <kuma/transform.h>

#include <gtest/gtest.h>

#include <cmath>

using kuma::Mat4;
using kuma::Quat;
using kuma::Transform;
using kuma::Vec3;

namespace {

constexpr float kEps = 1e-5f;
constexpr float kHalfPi = 1.57079633f;

::testing::AssertionResult MatricesNear(const Mat4& a, const Mat4& b, float eps = kEps) {
    for (int i = 0; i < 16; i++) {
        if (std::fabs(a.data[i] - b.data[i]) > eps) {
            return ::testing::AssertionFailure()
                << "Mismatch at data[" << i << "]: " << a.data[i] << " vs " << b.data[i];
        }
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

TEST(Transform, DefaultsToIdentityMatrix) {
    Transform t;
    EXPECT_TRUE(MatricesNear(t.model_matrix(), Mat4::identity()));
}

TEST(Transform, DefaultPositionIsOrigin) {
    Transform t;
    EXPECT_FLOAT_EQ(t.position().x, 0.0f);
    EXPECT_FLOAT_EQ(t.position().y, 0.0f);
    EXPECT_FLOAT_EQ(t.position().z, 0.0f);
}

TEST(Transform, DefaultScaleIsOne) {
    Transform t;
    EXPECT_FLOAT_EQ(t.scale().x, 1.0f);
    EXPECT_FLOAT_EQ(t.scale().y, 1.0f);
    EXPECT_FLOAT_EQ(t.scale().z, 1.0f);
}

TEST(Transform, TranslationOnlyMatchesMat4Translate) {
    Transform t;
    t.set_position({3.0f, -2.0f, 5.5f});
    EXPECT_TRUE(MatricesNear(t.model_matrix(), Mat4::translate(3.0f, -2.0f, 5.5f)));
}

TEST(Transform, ScaleOnlyMatchesDiagonal) {
    Transform t;
    t.set_scale({2.0f, 3.0f, 4.0f});

    Mat4 expected = Mat4::identity();
    expected(0, 0) = 2.0f;
    expected(1, 1) = 3.0f;
    expected(2, 2) = 4.0f;
    EXPECT_TRUE(MatricesNear(t.model_matrix(), expected));
}

TEST(Transform, UniformScaleSetterMatchesVec3Form) {
    Transform a;
    Transform b;
    a.set_scale(2.5f);
    b.set_scale({2.5f, 2.5f, 2.5f});
    EXPECT_TRUE(MatricesNear(a.model_matrix(), b.model_matrix()));
}

TEST(Transform, RotationOnlyMatchesQuatToMat4) {
    Quat q = Quat::from_axis_angle({0, 1, 0}, kHalfPi);
    Transform t;
    t.set_rotation(q);
    EXPECT_TRUE(MatricesNear(t.model_matrix(), q.to_mat4()));
}

TEST(Transform, EulerSetterMatchesQuatFromEuler) {
    Transform a;
    Transform b;
    a.set_rotation_euler(0.6f, -0.3f, 1.2f);
    b.set_rotation(Quat::from_euler(0.6f, -0.3f, 1.2f));
    EXPECT_TRUE(MatricesNear(a.model_matrix(), b.model_matrix()));
}

TEST(Transform, AxisAngleSetterMatchesQuatFromAxisAngle) {
    Transform a;
    Transform b;
    a.set_rotation_axis_angle({0, 1, 0}, kHalfPi);
    b.set_rotation(Quat::from_axis_angle({0, 1, 0}, kHalfPi));
    EXPECT_TRUE(MatricesNear(a.model_matrix(), b.model_matrix()));
}

TEST(Transform, AppliesScaleBeforeRotationBeforeTranslation) {
    // Critical correctness test: M = T * R * S means scale is applied
    // first, then rotation, then translation. We verify by transforming
    // a point and checking the order of operations matches expectation.
    //
    // Setup: scale (1,2,1) makes a unit vector along Y twice as long.
    //        Then rotate 90 around Z (rotates +Y to -X).
    //        Then translate by (10,0,0).
    //
    // Point (0,1,0) should travel:
    //   scale     -> (0, 2, 0)        (Y doubled)
    //   rotate Z  -> (-2, 0, 0)       (Y becomes -X)
    //   translate -> (8, 0, 0)        (-2 + 10)
    Transform t;
    t.set_scale({1.0f, 2.0f, 1.0f});
    t.set_rotation_axis_angle({0, 0, 1}, kHalfPi);
    t.set_position({10.0f, 0.0f, 0.0f});

    Mat4 m = t.model_matrix();
    float x = m(0, 0) * 0 + m(0, 1) * 1 + m(0, 2) * 0 + m(0, 3) * 1;
    float y = m(1, 0) * 0 + m(1, 1) * 1 + m(1, 2) * 0 + m(1, 3) * 1;
    float z = m(2, 0) * 0 + m(2, 1) * 1 + m(2, 2) * 0 + m(2, 3) * 1;

    EXPECT_NEAR(x, 8.0f, 1e-4f);
    EXPECT_NEAR(y, 0.0f, 1e-4f);
    EXPECT_NEAR(z, 0.0f, 1e-4f);
}

TEST(Transform, NonUniformScaleAndRotationDoNotCommute) {
    // T*R*S differs from T*S*R when scale is non-uniform - if our
    // composition were wrong (e.g., R*S instead of S then R), this
    // sanity check would fire.
    Transform t;
    t.set_scale({1.0f, 5.0f, 1.0f});
    t.set_rotation_axis_angle({0, 0, 1}, kHalfPi);

    // After scale-then-rotate: (1,1,0) becomes (1,5,0) then (-5,1,0).
    Mat4 m = t.model_matrix();
    float x = m(0, 0) * 1 + m(0, 1) * 1 + m(0, 2) * 0;
    float y = m(1, 0) * 1 + m(1, 1) * 1 + m(1, 2) * 0;

    EXPECT_NEAR(x, -5.0f, 1e-4f);
    EXPECT_NEAR(y, 1.0f, 1e-4f);
}
