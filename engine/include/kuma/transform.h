#pragma once

// ── Kuma Transform ──────────────────────────────────────────────
// A 3D transform: position, rotation, and scale. Produces a model
// matrix on demand (M = T * R * S - scale first, then rotate, then
// translate). Pure value type with no engine dependencies; the
// renderer consumes its model_matrix() during Phase 4.

#include <kuma/math.h>

namespace kuma {

class Transform {
public:
    Transform() = default;

    void set_position(const Vec3& position);
    void set_rotation(const Quat& rotation);
    void set_scale(const Vec3& scale);
    void set_scale(float uniform_scale);

    // Forward to Quat factories so callers can stay in human-friendly units
    // without including <kuma/math.h> just to build a rotation.
    void set_rotation_euler(float yaw, float pitch, float roll);
    void set_rotation_axis_angle(const Vec3& axis, float angle_radians);

    const Vec3& position() const;
    const Quat& rotation() const;
    const Vec3& scale() const;

    // M = T * R * S. Recomputed on every call - cheap enough for now;
    // a dirty-bit cache can slot in here later without changing the API.
    Mat4 model_matrix() const;

private:
    Vec3 position_{0.0f, 0.0f, 0.0f};
    Quat rotation_{};                // identity
    Vec3 scale_{1.0f, 1.0f, 1.0f};   // unit scale
};

}  // namespace kuma
