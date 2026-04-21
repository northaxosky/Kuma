// Tests for kuma::Vec3 and kuma::Mat4.
//
// Math code is the perfect first testing target for a game engine: pure,
// header-only, no GPU, no windowing, no globals. A broken Mat4 multiply
// corrupts every single rendered frame — exactly the kind of silent bug
// a unit test catches instantly and a visual inspection misses for weeks.

#include <kuma/math.h>

#include <gtest/gtest.h>

#include <array>
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

// Transforms a homogeneous point (x,y,z,w) through a Mat4. Mirrors what
// the GPU does in a vertex shader: clip = M * vertex. Use w=1 for points
// and w=0 for directions (so translation is ignored).
std::array<float, 4> TransformPoint(const Mat4& m, float x, float y, float z, float w) {
    return {
        m(0, 0) * x + m(0, 1) * y + m(0, 2) * z + m(0, 3) * w,
        m(1, 0) * x + m(1, 1) * y + m(1, 2) * z + m(1, 3) * w,
        m(2, 0) * x + m(2, 1) * y + m(2, 2) * z + m(2, 3) * w,
        m(3, 0) * x + m(3, 1) * y + m(3, 2) * z + m(3, 3) * w,
    };
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

TEST(Vec3, ConstructFromValues) {
    // Explicit three-arg constructor. Trivial but pins the ctor contract:
    // arguments map to x/y/z in order, not swapped.
    Vec3 v{1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3, SubtractionIsComponentwise) {
    // Used inside look_at (target - eye). A sign flip here silently
    // inverts the camera's forward direction.
    Vec3 diff = Vec3{5, 7, 9} - Vec3{1, 2, 3};
    EXPECT_FLOAT_EQ(diff.x, 4.0f);
    EXPECT_FLOAT_EQ(diff.y, 5.0f);
    EXPECT_FLOAT_EQ(diff.z, 6.0f);
}

TEST(Vec3, ScalarMultiplyScalesComponents) {
    // Used inside normalize. A bug here silently corrupts every
    // normalized vector in the engine.
    Vec3 scaled = Vec3{1, 2, 3} * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x, 2.0f);
    EXPECT_FLOAT_EQ(scaled.y, 4.0f);
    EXPECT_FLOAT_EQ(scaled.z, 6.0f);
}

TEST(Vec3, ScalarMultiplyByZeroGivesZero) {
    Vec3 zero = Vec3{1, 2, 3} * 0.0f;
    EXPECT_FLOAT_EQ(zero.x, 0.0f);
    EXPECT_FLOAT_EQ(zero.y, 0.0f);
    EXPECT_FLOAT_EQ(zero.z, 0.0f);
}

TEST(Vec3, DotIsCommutative) {
    // dot(a, b) == dot(b, a). Must hold regardless of input values.
    Vec3 a{1, 2, 3};
    Vec3 b{4, -5, 6};
    EXPECT_FLOAT_EQ(kuma::dot(a, b), kuma::dot(b, a));
}

TEST(Vec3, DotOfSelfEqualsSquaredLength) {
    // |v|^2 == dot(v, v). For v = (3, 4, 12), length = 13, so dot = 169.
    // This is the identity normalize() implicitly relies on.
    EXPECT_FLOAT_EQ(kuma::dot({3, 4, 12}, {3, 4, 12}), 169.0f);
}

TEST(Vec3, DotOfAntiParallelIsNegative) {
    // Opposing vectors -> negative dot. Sanity check against an
    // accidental abs() or squaring of components.
    EXPECT_FLOAT_EQ(kuma::dot({1, 0, 0}, {-2, 0, 0}), -2.0f);
}

TEST(Vec3, CrossCoversAllThreeAxisPairs) {
    // Right-handed basis: x×y=z, y×z=x, z×x=y. Testing all three together
    // catches any axis-swap or component-swap bug in the cross formula.
    Vec3 xy = kuma::cross({1, 0, 0}, {0, 1, 0});
    Vec3 yz = kuma::cross({0, 1, 0}, {0, 0, 1});
    Vec3 zx = kuma::cross({0, 0, 1}, {1, 0, 0});
    EXPECT_FLOAT_EQ(xy.z, 1.0f);
    EXPECT_FLOAT_EQ(yz.x, 1.0f);
    EXPECT_FLOAT_EQ(zx.y, 1.0f);
    // And the other components must be zero.
    EXPECT_FLOAT_EQ(xy.x, 0.0f);
    EXPECT_FLOAT_EQ(xy.y, 0.0f);
    EXPECT_FLOAT_EQ(yz.y, 0.0f);
    EXPECT_FLOAT_EQ(yz.z, 0.0f);
    EXPECT_FLOAT_EQ(zx.x, 0.0f);
    EXPECT_FLOAT_EQ(zx.z, 0.0f);
}

TEST(Vec3, CrossOfParallelVectorsIsZero) {
    // v × v = 0 for any v. By extension, v × (k*v) = 0.
    Vec3 r = kuma::cross({1, 2, 3}, {1, 2, 3});
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

TEST(Vec3, CrossIsPerpendicularToBothOperands) {
    // The defining property of the cross product: a × b is perpendicular
    // to both a and b. This single invariant catches almost any formula
    // bug regardless of which terms are wrong.
    Vec3 a{1, 2, 3};
    Vec3 b{-4, 5, -6};
    Vec3 c = kuma::cross(a, b);
    EXPECT_NEAR(kuma::dot(c, a), 0.0f, kEps);
    EXPECT_NEAR(kuma::dot(c, b), 0.0f, kEps);
}

TEST(Vec3, NormalizeOfUnitVectorIsUnchanged) {
    Vec3 u = kuma::normalize({0, 1, 0});
    EXPECT_FLOAT_EQ(u.x, 0.0f);
    EXPECT_FLOAT_EQ(u.y, 1.0f);
    EXPECT_FLOAT_EQ(u.z, 0.0f);
}

TEST(Vec3, NormalizePreservesDirectionForPositiveScale) {
    // normalize(k * v) == normalize(v) for any k > 0. If this fails,
    // direction is being lost in the magnitude calculation.
    Vec3 a = kuma::normalize({1, 2, 3});
    Vec3 b = kuma::normalize({5, 10, 15});  // same direction, 5x scale
    EXPECT_NEAR(a.x, b.x, kEps);
    EXPECT_NEAR(a.y, b.y, kEps);
    EXPECT_NEAR(a.z, b.z, kEps);
}

TEST(Vec3, NormalizeOfNonAxisAlignedVector) {
    // v = (1, 2, 2) has length sqrt(1+4+4) = 3. Exercising all three
    // components in one case rules out per-component asymmetric bugs
    // that axis-aligned tests can't see.
    Vec3 n = kuma::normalize({1, 2, 2});
    EXPECT_NEAR(n.x, 1.0f / 3.0f, kEps);
    EXPECT_NEAR(n.y, 2.0f / 3.0f, kEps);
    EXPECT_NEAR(n.z, 2.0f / 3.0f, kEps);
}

// ── Mat4 layout ─────────────────────────────────────────────────────

TEST(Mat4, DefaultConstructsToZero) {
    // `Mat4 m{}` must zero-initialize every cell. If not, factory functions
    // like translate() that only write specific cells would leak garbage
    // into the untouched ones.
    Mat4 m{};
    for (int i = 0; i < 16; i++) {
        EXPECT_FLOAT_EQ(m.data[i], 0.0f);
    }
}

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

TEST(Mat4, TranslateMovesOrigin) {
    // translate(3,4,5) applied to the origin point (0,0,0,1) should yield
    // (3,4,5,1). Tests translate as a *transformation*, not just cell layout.
    auto p = TransformPoint(Mat4::translate(3, 4, 5), 0, 0, 0, 1);
    EXPECT_FLOAT_EQ(p[0], 3.0f);
    EXPECT_FLOAT_EQ(p[1], 4.0f);
    EXPECT_FLOAT_EQ(p[2], 5.0f);
    EXPECT_FLOAT_EQ(p[3], 1.0f);
}

TEST(Mat4, TranslateMovesArbitraryPoint) {
    // translate(1,2,3) * (10,20,30,1) = (11,22,33,1).
    auto p = TransformPoint(Mat4::translate(1, 2, 3), 10, 20, 30, 1);
    EXPECT_FLOAT_EQ(p[0], 11.0f);
    EXPECT_FLOAT_EQ(p[1], 22.0f);
    EXPECT_FLOAT_EQ(p[2], 33.0f);
    EXPECT_FLOAT_EQ(p[3], 1.0f);
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

TEST(Mat4, MultiplyWithNonTrivialMatricesMatchesHandComputed) {
    // All previous multiply tests use translate matrices whose upper-left
    // 3x3 is identity — which means a transposed-index bug in the inner
    // loop can hide. Two dense matrices where every cell matters exercise
    // the real column-major contract: C(r,c) = sum_k A(r,k) * B(k,c).
    Mat4 a{};
    a(0, 0) = 1;   a(0, 1) = 2;   a(0, 2) = 3;   a(0, 3) = 4;
    a(1, 0) = 5;   a(1, 1) = 6;   a(1, 2) = 7;   a(1, 3) = 8;
    a(2, 0) = 9;   a(2, 1) = 10;  a(2, 2) = 11;  a(2, 3) = 12;
    a(3, 0) = 13;  a(3, 1) = 14;  a(3, 2) = 15;  a(3, 3) = 16;

    Mat4 b{};
    b(0, 0) = 17;  b(0, 1) = 18;  b(0, 2) = 19;  b(0, 3) = 20;
    b(1, 0) = 21;  b(1, 1) = 22;  b(1, 2) = 23;  b(1, 3) = 24;
    b(2, 0) = 25;  b(2, 1) = 26;  b(2, 2) = 27;  b(2, 3) = 28;
    b(3, 0) = 29;  b(3, 1) = 30;  b(3, 2) = 31;  b(3, 3) = 32;

    Mat4 c = a * b;

    // Spot-check one cell from row 0, one from the middle, one from row 3.
    // If any of these are wrong the multiply is structurally broken.
    //   c(0,0) = 1*17 + 2*21 + 3*25 + 4*29 = 250
    //   c(1,2) = 5*19 + 6*23 + 7*27 + 8*31 = 670
    //   c(3,3) = 13*20 + 14*24 + 15*28 + 16*32 = 1528
    EXPECT_FLOAT_EQ(c(0, 0), 250.0f);
    EXPECT_FLOAT_EQ(c(1, 2), 670.0f);
    EXPECT_FLOAT_EQ(c(3, 3), 1528.0f);
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

TEST(Mat4, PerspectiveMapsNearPlaneToZeroDepth) {
    // Vulkan convention: a point on the near plane (z = -near in view space,
    // since the camera looks down -Z) must produce clip-space z/w == 0.
    // This is the defining contract between the projection matrix and the
    // depth buffer — get it wrong and every 3D frame has broken depth.
    const float near = 0.5f;
    const float far  = 100.0f;
    Mat4 p = Mat4::perspective(1.5708f, 16.0f / 9.0f, near, far);

    auto c = TransformPoint(p, 0, 0, -near, 1);
    ASSERT_GT(std::fabs(c[3]), kEps);  // guard against div-by-zero
    EXPECT_NEAR(c[2] / c[3], 0.0f, kEps);
}

TEST(Mat4, PerspectiveMapsFarPlaneToUnitDepth) {
    // Mirror of the near-plane test: z = -far maps to clip-space z/w == 1.
    const float near = 0.5f;
    const float far  = 100.0f;
    Mat4 p = Mat4::perspective(1.5708f, 16.0f / 9.0f, near, far);

    auto c = TransformPoint(p, 0, 0, -far, 1);
    ASSERT_GT(std::fabs(c[3]), kEps);
    EXPECT_NEAR(c[2] / c[3], 1.0f, kEps);
}

TEST(Mat4, PerspectivePointOnCameraAxisMapsToScreenCenter) {
    // A point exactly on the camera's viewing axis (x=0, y=0) at any depth
    // in range should land at NDC (0, 0). If X or Y leaks through the
    // projection, the view is skewed off-center.
    Mat4 p = Mat4::perspective(1.5708f, 16.0f / 9.0f, 0.5f, 100.0f);
    auto c = TransformPoint(p, 0, 0, -5, 1);
    ASSERT_GT(std::fabs(c[3]), kEps);
    EXPECT_NEAR(c[0] / c[3], 0.0f, kEps);
    EXPECT_NEAR(c[1] / c[3], 0.0f, kEps);
}

// ── Look-at ─────────────────────────────────────────────────────────

TEST(Mat4, LookAtFromOriginDownNegativeZProducesIdentityRotation) {
    // Camera at origin, looking toward -Z, up is +Y. This is the canonical
    // "do nothing" view — the rotation block should be identity. Translation
    // is zero because the eye is at the origin.
    Mat4 v = Mat4::look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0});

    EXPECT_NEAR(v(0, 0), 1.0f, kEps);
    EXPECT_NEAR(v(1, 1), 1.0f, kEps);
    EXPECT_NEAR(v(2, 2), 1.0f, kEps);  // m(2,2) = -forward.z = -(-1) = 1
    EXPECT_NEAR(v(0, 3), 0.0f, kEps);
    EXPECT_NEAR(v(1, 3), 0.0f, kEps);
    EXPECT_NEAR(v(2, 3), 0.0f, kEps);
}

TEST(Mat4, LookAtTranslatesEyeToOrigin) {
    // The eye always maps to the origin in camera space. Simple axis-aligned
    // case: eye on +X, looking at origin.
    Mat4 v = Mat4::look_at({5, 0, 0}, {0, 0, 0}, {0, 1, 0});
    auto p = TransformPoint(v, 5, 0, 0, 1);
    EXPECT_NEAR(p[0], 0.0f, kEps);
    EXPECT_NEAR(p[1], 0.0f, kEps);
    EXPECT_NEAR(p[2], 0.0f, kEps);
    EXPECT_NEAR(p[3], 1.0f, kEps);
}

TEST(Mat4, LookAtMapsArbitraryEyeToOrigin) {
    // Same invariant as above, but with an eye/target/up that have no
    // accidental axis alignment. A bug that happens to cancel in the
    // simple case will still be caught here.
    Vec3 eye{3, 4, 5};
    Mat4 v = Mat4::look_at(eye, {1, 2, 3}, {0, 1, 0});
    auto p = TransformPoint(v, eye.x, eye.y, eye.z, 1);
    EXPECT_NEAR(p[0], 0.0f, kEps);
    EXPECT_NEAR(p[1], 0.0f, kEps);
    EXPECT_NEAR(p[2], 0.0f, kEps);
    EXPECT_NEAR(p[3], 1.0f, kEps);
}

TEST(Mat4, LookAtRotationBlockIsOrthonormal) {
    // The upper-left 3x3 of a view matrix is a pure rotation. Its rows
    // (right, cam_up, -forward) must be unit vectors and pairwise
    // perpendicular. One invariant that catches a huge class of bugs.
    Mat4 v = Mat4::look_at({3, 4, 5}, {1, 2, 3}, {0, 1, 0});

    Vec3 r0{v(0, 0), v(0, 1), v(0, 2)};
    Vec3 r1{v(1, 0), v(1, 1), v(1, 2)};
    Vec3 r2{v(2, 0), v(2, 1), v(2, 2)};

    EXPECT_NEAR(kuma::dot(r0, r0), 1.0f, kEps);
    EXPECT_NEAR(kuma::dot(r1, r1), 1.0f, kEps);
    EXPECT_NEAR(kuma::dot(r2, r2), 1.0f, kEps);
    EXPECT_NEAR(kuma::dot(r0, r1), 0.0f, kEps);
    EXPECT_NEAR(kuma::dot(r0, r2), 0.0f, kEps);
    EXPECT_NEAR(kuma::dot(r1, r2), 0.0f, kEps);
}

TEST(Mat4, LookAtForwardDirectionMapsToNegativeZ) {
    // The world-space direction from eye toward target, transformed by the
    // view matrix (as a direction — w=0, so translation is ignored), must
    // come out pointing down -Z. That's the very definition of "the camera
    // looks down -Z in view space."
    Mat4 v = Mat4::look_at({10, 0, 0}, {0, 0, 0}, {0, 1, 0});
    // forward = normalize(target - eye) = (-1, 0, 0)
    auto d = TransformPoint(v, -1, 0, 0, 0);
    EXPECT_NEAR(d[0], 0.0f, kEps);
    EXPECT_NEAR(d[1], 0.0f, kEps);
    EXPECT_NEAR(d[2], -1.0f, kEps);
    EXPECT_NEAR(d[3], 0.0f, kEps);
}

// ── MVP composition ─────────────────────────────────────────────────
// End-to-end test of `projection * view * model * vertex` — the exact
// computation every vertex shader performs. Each component is tested
// elsewhere; this test verifies they compose correctly.

TEST(Mat4, MvpOfOriginVertexProducesExpectedClipSpace) {
    // Setup: identity model; camera at (0,0,5) looking at the origin;
    // perspective with 90° fov, aspect 1, near 1, far 10.
    //
    // Hand-derived expected values for a vertex at (0,0,0,1):
    //   view-space:  (0, 0, -5, 1)
    //   clip-space:  (0, 0, 40/9, 5)  via perspective cells
    //   NDC:         (0, 0, 8/9)
    Mat4 model = Mat4::identity();
    Mat4 view  = Mat4::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    Mat4 proj  = Mat4::perspective(1.5708f, 1.0f, 1.0f, 10.0f);
    Mat4 mvp   = proj * view * model;

    auto c = TransformPoint(mvp, 0, 0, 0, 1);
    ASSERT_GT(std::fabs(c[3]), kEps);
    EXPECT_NEAR(c[0] / c[3], 0.0f, kEps);
    EXPECT_NEAR(c[1] / c[3], 0.0f, kEps);
    EXPECT_NEAR(c[2] / c[3], 8.0f / 9.0f, kEps);
}
