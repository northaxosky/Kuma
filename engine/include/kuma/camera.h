#pragma once

// ── Kuma Camera ─────────────────────────────────────────────────
// Math camera + optional free-fly controller. The camera itself does
// not read input; controllers/game code mutate it during UPDATE, then
// the renderer consumes its matrices during RENDER.

#include <kuma/math.h>

namespace kuma {

class Camera {
public:
    Camera() = default;

    Vec3 position() const;
    void set_position(const Vec3& position);
    void translate(const Vec3& delta);

    float yaw() const;
    float pitch() const;
    void set_rotation(float yaw_radians, float pitch_radians);

    void set_perspective(float fov_y_radians, float aspect, float near_plane, float far_plane);
    void set_aspect(float aspect);

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    Mat4 view() const;
    Mat4 projection() const;
    Mat4 view_projection() const;

private:
    static constexpr float kMaxPitchRadians = 1.55334306f;  // 89 degrees

    Vec3 position_ = {0.0f, 0.0f, 2.0f};
    float yaw_radians_ = -1.57079633f;  // -90 degrees: look down -Z.
    float pitch_radians_ = 0.0f;

    float fov_y_radians_ = 0.78539816f;  // 45 degrees, matching the old hardcoded renderer camera.
    float aspect_ = 16.0f / 9.0f;
    float near_plane_ = 0.1f;
    float far_plane_ = 100.0f;
};

class FreeFlyCameraController {
public:
    float move_speed = 3.0f;  // world units per second

    // Phase 3: UPDATE. Consumes the Phase 1 input snapshot and Phase 2
    // delta time, then mutates the camera before Phase 4 rendering.
    void update(Camera& camera) const;
};

}  // namespace kuma
