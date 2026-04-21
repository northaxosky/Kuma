// Tests for kuma::Vec3 and kuma::Mat4.
//
// Math code is the perfect first testing target for a game engine: pure,
// header-only, no GPU, no windowing, no globals. A broken Mat4 multiply
// corrupts every single rendered frame — exactly the kind of silent bug
// a unit test catches instantly and a visual inspection misses for weeks.

#include <kuma/math.h>

#include <gtest/gtest.h>

#include <cmath>

using kuma::Mat4;
using kuma::Vec3;

namespace {

// Floating-point comparisons are never exact. Use a small tolerance.
constexpr float kEps = 1e-5f;

// Helper: compare two Mat4s element-wise with tolerance.
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

// ── Vec3 ────────────────────────────────────────────────────────────

TEST(Vec3, DefaultConstructsToZero) {
    Vec3 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vec3, DotOfOrthogonalVectorsIsZero) {
    // x-axis and y-axis are perpendicular -> dot product must be 0.
    EXPECT_FLOAT_EQ(kuma::dot({1, 0, 0}, {0, 1, 0}), 0.0f);
}

TEST(Vec3, DotOfParallelVectorsIsLengthProduct) {
    // Same direction, scaled: dot = |a| * |b|.
    EXPECT_FLOAT_EQ(kuma::dot({2, 0, 0}, {3, 0, 0}), 6.0f);
}

TEST(Vec3, CrossProductMatchesRightHandRule) {
    // x × y = z in a right-handed coordinate system.
    Vec3 r = kuma::cross({1, 0, 0}, {0, 1, 0});
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 1.0f);
}

TEST(Vec3, CrossProductIsAntiCommutative) {
    // a × b = -(b × a) — catches sign errors in the cross formula.
    Vec3 ab = kuma::cross({1, 2, 3}, {4, 5, 6});
    Vec3 ba = kuma::cross({4, 5, 6}, {1, 2, 3});
    EXPECT_FLOAT_EQ(ab.x, -ba.x);
    EXPECT_FLOAT_EQ(ab.y, -ba.y);
    EXPECT_FLOAT_EQ(ab.z, -ba.z);
}

TEST(Vec3, NormalizeProducesUnitLength) {
    Vec3 n = kuma::normalize({3, 0, 4});  // length 5 -> should become length 1
    EXPECT_NEAR(std::sqrt(kuma::dot(n, n)), 1.0f, kEps);
}

TEST(Vec3, NormalizeZeroVectorIsSafe) {
    // Division by zero would be a crash. Must return zero vector instead.
    Vec3 n = kuma::normalize({0, 0, 0});
    EXPECT_FLOAT_EQ(n.x, 0.0f);
    EXPECT_FLOAT_EQ(n.y, 0.0f);
    EXPECT_FLOAT_EQ(n.z, 0.0f);
}

// ── Mat4 layout ─────────────────────────────────────────────────────

TEST(Mat4, IdentityHasOnesOnDiagonal) {
    Mat4 m = Mat4::identity();
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            EXPECT_FLOAT_EQ(m(r, c), (r == c) ? 1.0f : 0.0f);
        }
    }
}

TEST(Mat4, StorageIsColumnMajor) {
    // Indexing contract: data[col * 4 + row]. Confirm by writing a value
    // via (row, col) and reading it back from the flat array.
    Mat4 m{};
    m(1, 2) = 7.0f;  // row 1, col 2 -> data[2*4 + 1] = data[9]
    EXPECT_FLOAT_EQ(m.data[9], 7.0f);
}

TEST(Mat4, PtrReturnsDataPointer) {
    Mat4 m = Mat4::identity();
    // Shaders get the matrix via .ptr() — this must alias data[] directly
    // or uploads to the GPU would send garbage.
    EXPECT_EQ(m.ptr(), m.data);
}

// ── Mat4 factories ──────────────────────────────────────────────────

TEST(Mat4, TranslateSetsRightmostColumn) {
    Mat4 m = Mat4::translate(3.0f, 4.0f, 5.0f);
    EXPECT_FLOAT_EQ(m(0, 3), 3.0f);
    EXPECT_FLOAT_EQ(m(1, 3), 4.0f);
    EXPECT_FLOAT_EQ(m(2, 3), 5.0f);
    // Rest must still be identity.
    EXPECT_FLOAT_EQ(m(0, 0), 1.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 1.0f);
}

// ── Mat4 multiplication ─────────────────────────────────────────────

TEST(Mat4, MultiplyByIdentityIsNoOp) {
    // A * I == A. Core invariant — if this fails, every shader will show
    // either a black screen or corrupted geometry.
    Mat4 t = Mat4::translate(1, 2, 3);
    Mat4 i = Mat4::identity();
    EXPECT_TRUE(MatricesNear(t * i, t));
    EXPECT_TRUE(MatricesNear(i * t, t));
}

TEST(Mat4, MultiplyIsAssociative) {
    // (A * B) * C == A * (B * C). Catches row/col transposition bugs.
    Mat4 a = Mat4::translate(1, 0, 0);
    Mat4 b = Mat4::translate(0, 2, 0);
    Mat4 c = Mat4::translate(0, 0, 3);
    EXPECT_TRUE(MatricesNear((a * b) * c, a * (b * c)));
}

TEST(Mat4, TranslatesCompose) {
    // translate(a) * translate(b) should equal translate(a+b).
    Mat4 composed = Mat4::translate(1, 2, 3) * Mat4::translate(4, 5, 6);
    Mat4 expected = Mat4::translate(5, 7, 9);
    EXPECT_TRUE(MatricesNear(composed, expected));
}

// ── Perspective ─────────────────────────────────────────────────────

TEST(Mat4, PerspectiveMatchesExpectedCells) {
    // Pin the five non-zero cells to their formulas at known inputs.
    // If any drift, every 3D frame is subtly wrong.
    const float fov    = 1.5708f;  // ~90 degrees
    const float aspect = 16.0f / 9.0f;
    const float near   = 0.1f;
    const float far    = 100.0f;

    Mat4 p = Mat4::perspective(fov, aspect, near, far);
    const float t = std::tan(fov / 2.0f);

    EXPECT_NEAR(p(0, 0), 1.0f / (aspect * t), kEps);
    EXPECT_NEAR(p(1, 1), -1.0f / t, kEps);  // Vulkan Y-flip
    EXPECT_NEAR(p(2, 2), far / (near - far), kEps);
    EXPECT_NEAR(p(2, 3), (far * near) / (near - far), kEps);
    EXPECT_NEAR(p(3, 2), -1.0f, kEps);
}

// ── Look-at ─────────────────────────────────────────────────────────

TEST(Mat4, LookAtFromOriginDownNegativeZProducesIdentityRotation) {
    // Camera at origin, looking toward -Z, up is +Y. This is the canonical
    // "do nothing" view — the rotation block should be identity. Translation
    // is zero because the eye is at the origin.
    Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0});

    EXPECT_NEAR(v(0, 0), 1.0f, kEps);
    EXPECT_NEAR(v(1, 1), 1.0f, kEps);
    EXPECT_NEAR(v(2, 2), 1.0f, kEps);  // -(-forward.z) = -(-1) wait: forward=(0,0,-1), m(2,2) = -forward.z = 1
    EXPECT_NEAR(v(0, 3), 0.0f, kEps);
    EXPECT_NEAR(v(1, 3), 0.0f, kEps);
    EXPECT_NEAR(v(2, 3), 0.0f, kEps);
}

TEST(Mat4, LookAtTranslatesEyeToOrigin) {
    // Moving the eye +5 units along +X should put a translation of -5 in
    // the camera-space X column (the view matrix inverts camera position).
    Mat4 v = Mat4::look_at({5, 0, 0}, {0, 0, 0}, {0, 1, 0});
    // Eye is at +5 on X looking toward origin (-X direction). Camera's
    // right becomes -Z, up stays +Y. The translated eye must land at origin
    // in camera space, so applying v to (5,0,0,1) should give ~(0,0,0,1).
    float ex = v(0, 0) * 5 + v(0, 3);
    float ey = v(1, 0) * 5 + v(1, 3);
    float ez = v(2, 0) * 5 + v(2, 3);
    EXPECT_NEAR(ex, 0.0f, kEps);
    EXPECT_NEAR(ey, 0.0f, kEps);
    EXPECT_NEAR(ez, 0.0f, kEps);
}
