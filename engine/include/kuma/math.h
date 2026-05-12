#pragma once

#include <cmath>
#include <cstring>

namespace kuma {

// ── Vec2 ────────────────────────────────────────────────────────
// 2D float vector. Used for anything screen-space: mouse position,
// mouse delta, UI coordinates, texture UVs, 2D sprites.

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }

    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }
};

inline bool operator==(const Vec2& a, const Vec2& b) {
    return a.x == b.x && a.y == b.y;
}

// ── Vec3 ────────────────────────────────────────────────────────

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }

    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }

    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
};

// Dot product: measures how parallel two vectors are
inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Cross product: gives a vector perpendicular to both inputs
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Normalize: scale a vector to length 1 (unit vector)
inline Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(dot(v, v));
    if (len == 0.0f)
        return {0.0f, 0.0f, 0.0f};
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

    // Non-uniform scale: stretches an object by (sx, sy, sz) along
    // each axis. A scale factor of 1 leaves that axis unchanged;
    // negative values mirror.
    static Mat4 scale(float sx, float sy, float sz) {
        Mat4 m = identity();
        m(0, 0) = sx;
        m(1, 1) = sy;
        m(2, 2) = sz;
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
        m(1, 1) = -1.0f / tan_half_fov;  // flip Y for Vulkan
        m(2, 2) = far / (near - far);    // maps z to 0..1 (reversed for -Z)
        m(2, 3) = (far * near) / (near - far);
        m(3, 2) = -1.0f;  // w_clip = -z_view (positive for -Z)
        return m;
    }

    // Look-at view matrix: positions and orients the camera.
    //   eye:    where the camera is
    //   target: what the camera is looking at
    //   up:     which direction is "up" (usually 0,1,0)
    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 forward = normalize(target - eye);      // camera looks this way
        Vec3 right = normalize(cross(forward, up));  // camera's right
        Vec3 cam_up = cross(right, forward);         // camera's true up

        // The view matrix uses -forward for the Z row because the camera
        // looks down -Z in Vulkan/OpenGL convention. Without the negation,
        // the image flips vertically.
        Mat4 m = identity();
        m(0, 0) = right.x;
        m(0, 1) = right.y;
        m(0, 2) = right.z;
        m(1, 0) = cam_up.x;
        m(1, 1) = cam_up.y;
        m(1, 2) = cam_up.z;
        m(2, 0) = -forward.x;
        m(2, 1) = -forward.y;
        m(2, 2) = -forward.z;

        m(0, 3) = -dot(right, eye);
        m(1, 3) = -dot(cam_up, eye);
        m(2, 3) = dot(forward, eye);

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

// ── Quat ────────────────────────────────────────────────────────
// Unit quaternion representing a 3D rotation. Storage is (x, y, z, w)
// matching GLM, Unity, and the glTF spec - w is the scalar part.
//
// Build via factories (from_axis_angle / from_euler), not by writing
// raw components. Multiplying composes rotations: (a * b) means
// "rotate by b, then by a" (same convention as Mat4).

struct Quat;
inline Quat operator*(const Quat& a, const Quat& b);

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;  // identity

    Quat() = default;
    Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    static Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    // Encodes "rotate by angle around axis" using the half-angle form.
    // Caller is responsible for passing a unit-length axis.
    static Quat from_axis_angle(const Vec3& axis, float angle_radians) {
        float half = angle_radians * 0.5f;
        float s = std::sin(half);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
    }

    // Yaw=Y, pitch=X, roll=Z. Composed q_yaw * q_pitch * q_roll, so the
    // applied order to vectors is roll -> pitch -> yaw (right-to-left).
    static Quat from_euler(float yaw, float pitch, float roll) {
        Quat qy = from_axis_angle({0.0f, 1.0f, 0.0f}, yaw);
        Quat qx = from_axis_angle({1.0f, 0.0f, 0.0f}, pitch);
        Quat qz = from_axis_angle({0.0f, 0.0f, 1.0f}, roll);
        return qy * qx * qz;
    }

    // Build the equivalent 3x3 rotation matrix, padded into a Mat4.
    // Assumes the quaternion is unit-length.
    Mat4 to_mat4() const {
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        Mat4 m = Mat4::identity();
        m(0, 0) = 1.0f - 2.0f * (yy + zz);
        m(0, 1) = 2.0f * (xy - wz);
        m(0, 2) = 2.0f * (xz + wy);
        m(1, 0) = 2.0f * (xy + wz);
        m(1, 1) = 1.0f - 2.0f * (xx + zz);
        m(1, 2) = 2.0f * (yz - wx);
        m(2, 0) = 2.0f * (xz - wy);
        m(2, 1) = 2.0f * (yz + wx);
        m(2, 2) = 1.0f - 2.0f * (xx + yy);
        return m;
    }

    // Rotate a vector directly. Equivalent to (q.to_mat4() * v) but
    // skips the matrix build. Uses the identity:
    //   v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    Vec3 rotate(const Vec3& v) const {
        Vec3 q_xyz{x, y, z};
        Vec3 t = cross(q_xyz, v) * 2.0f;
        return v + (t * w) + cross(q_xyz, t);
    }

    // Rescale to unit length. Float drift after many multiplies makes
    // this important to call periodically on long-lived rotations.
    Quat normalized() const {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len == 0.0f)
            return identity();
        float inv = 1.0f / len;
        return {x * inv, y * inv, z * inv, w * inv};
    }
};

// Hamilton product. (a * b) means "rotate by b, then by a".
inline Quat operator*(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// Spherical linear interpolation. Takes the shortest arc by flipping
// b if the dot product is negative (q and -q represent the same
// rotation, but the interpolation path between them isn't the same).
inline Quat slerp(const Quat& a, const Quat& b, float t) {
    float cos_theta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    Quat b_adj = b;
    if (cos_theta < 0.0f) {
        b_adj = {-b.x, -b.y, -b.z, -b.w};
        cos_theta = -cos_theta;
    }

    // Near-parallel: fall back to lerp + normalize to avoid divide-by-near-zero.
    if (cos_theta > 0.9995f) {
        Quat r{
            a.x + t * (b_adj.x - a.x),
            a.y + t * (b_adj.y - a.y),
            a.z + t * (b_adj.z - a.z),
            a.w + t * (b_adj.w - a.w),
        };
        return r.normalized();
    }

    float theta = std::acos(cos_theta);
    float sin_theta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sin_theta;
    float wb = std::sin(t * theta) / sin_theta;
    return {
        wa * a.x + wb * b_adj.x,
        wa * a.y + wb * b_adj.y,
        wa * a.z + wb * b_adj.z,
        wa * a.w + wb * b_adj.w,
    };
}

}  // namespace kuma
