#pragma once

#include <cmath>
#include <cstring>

namespace kuma {

// ── Vec3 ────────────────────────────────────────────────────────

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3 operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }
};

// Dot product: measures how parallel two vectors are
inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Cross product: gives a vector perpendicular to both inputs
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

// Normalize: scale a vector to length 1 (unit vector)
inline Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(dot(v, v));
    if (len == 0.0f) return {0.0f, 0.0f, 0.0f};
    return v * (1.0f / len);
}

// ── Mat4 ────────────────────────────────────────────────────────
// 4x4 matrix stored in column-major order (same as Vulkan/OpenGL).
//
// Column-major means columns are contiguous in memory:
//   data[0..3]   = column 0
//   data[4..7]   = column 1
//   data[8..11]  = column 2
//   data[12..15] = column 3
//
// Access: data[col * 4 + row]

struct Mat4 {
    float data[16] = {};

    // Element access: m(row, col)
    float& operator()(int row, int col) { return data[col * 4 + row]; }
    float operator()(int row, int col) const { return data[col * 4 + row]; }

    // Raw pointer for passing to Vulkan/shaders
    const float* ptr() const { return data; }

    // ── Factory functions ───────────────────────────────────────

    // Identity matrix: no transformation (like multiplying by 1)
    static Mat4 identity() {
        Mat4 m{};
        m(0, 0) = 1.0f;
        m(1, 1) = 1.0f;
        m(2, 2) = 1.0f;
        m(3, 3) = 1.0f;
        return m;
    }

    // Translation: moves an object to (tx, ty, tz)
    static Mat4 translate(float tx, float ty, float tz) {
        Mat4 m = identity();
        m(0, 3) = tx;
        m(1, 3) = ty;
        m(2, 3) = tz;
        return m;
    }

    // Perspective projection: 3D → 2D with depth.
    //   fov_radians: vertical field of view (e.g., 45 degrees = 0.785 radians)
    //   aspect:      width / height (e.g., 16/9 = 1.778)
    //   near/far:    clipping planes (objects outside this range are invisible)
    //
    // This version outputs Vulkan's clip space:
    //   x: -1 (left) to +1 (right)
    //   y: -1 (top)  to +1 (bottom)   ← Vulkan Y is flipped vs OpenGL
    //   z:  0 (near) to  1 (far)      ← Vulkan depth range is 0..1
    static Mat4 perspective(float fov_radians, float aspect, float near, float far) {
        float tan_half_fov = std::tan(fov_radians / 2.0f);

        // Right-handed, looking down -Z. Objects in front of the camera
        // have negative z in view space, so we negate m(3,2) to make
        // w_clip positive (required for correct clipping).
        Mat4 m{};
        m(0, 0) = 1.0f / (aspect * tan_half_fov);
        m(1, 1) = -1.0f / tan_half_fov;           // flip Y for Vulkan
        m(2, 2) = far / (near - far);              // maps z to 0..1 (reversed for -Z)
        m(2, 3) = (far * near) / (near - far);
        m(3, 2) = -1.0f;                           // w_clip = -z_view (positive for -Z)
        return m;
    }

    // Look-at view matrix: positions and orients the camera.
    //   eye:    where the camera is
    //   target: what the camera is looking at
    //   up:     which direction is "up" (usually 0,1,0)
    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 forward = normalize(target - eye);         // camera looks this way
        Vec3 right = normalize(cross(forward, up));     // camera's right
        Vec3 cam_up = cross(right, forward);            // camera's true up

        // The view matrix uses -forward for the Z row because the camera
        // looks down -Z in Vulkan/OpenGL convention. Without the negation,
        // the image flips vertically.
        Mat4 m = identity();
        m(0, 0) =  right.x;    m(0, 1) =  right.y;    m(0, 2) =  right.z;
        m(1, 0) =  cam_up.x;   m(1, 1) =  cam_up.y;   m(1, 2) =  cam_up.z;
        m(2, 0) = -forward.x;  m(2, 1) = -forward.y;  m(2, 2) = -forward.z;

        m(0, 3) = -dot(right, eye);
        m(1, 3) = -dot(cam_up, eye);
        m(2, 3) =  dot(forward, eye);

        return m;
    }
};

// Matrix multiplication: applies transformations in sequence.
// MVP = projection * view * model (read right to left)
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a(row, k) * b(k, col);
            }
            result(row, col) = sum;
        }
    }
    return result;
}

} // namespace kuma
