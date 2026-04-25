// ── Camera ──────────────────────────────────────────────────────
// Pure camera math plus the first sandbox-friendly free-fly controller.

#include <kuma/camera.h>
#include <kuma/input.h>
#include <kuma/time.h>

#include <algorithm>
#include <cmath>

namespace kuma {

namespace {

const Vec3 kWorldUp{0.0f, 1.0f, 0.0f};

}  // namespace

Vec3 Camera::position() const {
    return position_;
}

void Camera::set_position(const Vec3& position) {
    position_ = position;
}

void Camera::translate(const Vec3& delta) {
    position_ += delta;
}

float Camera::yaw() const {
    return yaw_radians_;
}

float Camera::pitch() const {
    return pitch_radians_;
}

void Camera::set_rotation(float yaw_radians, float pitch_radians) {
    yaw_radians_ = yaw_radians;
    pitch_radians_ = std::clamp(pitch_radians, -kMaxPitchRadians, kMaxPitchRadians);
}

void Camera::set_perspective(float fov_y_radians, float aspect, float near_plane, float far_plane) {
    fov_y_radians_ = fov_y_radians;
    aspect_ = aspect;
    near_plane_ = near_plane;
    far_plane_ = far_plane;
}

void Camera::set_aspect(float aspect) {
    aspect_ = aspect;
}

Vec3 Camera::forward() const {
    const float cos_pitch = std::cos(pitch_radians_);
    return normalize({std::cos(yaw_radians_) * cos_pitch, std::sin(pitch_radians_),
                      std::sin(yaw_radians_) * cos_pitch});
}

Vec3 Camera::right() const {
    return normalize(cross(forward(), kWorldUp));
}

Vec3 Camera::up() const {
    return cross(right(), forward());
}

Mat4 Camera::view() const {
    return Mat4::look_at(position_, position_ + forward(), kWorldUp);
}

Mat4 Camera::projection() const {
    return Mat4::perspective(fov_y_radians_, aspect_, near_plane_, far_plane_);
}

Mat4 Camera::view_projection() const {
    return projection() * view();
}

void FreeFlyCameraController::update(Camera& camera) const {
    Vec3 movement{};

    if (input::is_key_down(Key::W))
        movement += camera.forward();
    if (input::is_key_down(Key::S))
        movement += camera.forward() * -1.0f;
    if (input::is_key_down(Key::D))
        movement += camera.right();
    if (input::is_key_down(Key::A))
        movement += camera.right() * -1.0f;
    if (input::is_key_down(Key::E))
        movement += kWorldUp;
    if (input::is_key_down(Key::Q))
        movement += kWorldUp * -1.0f;

    if (dot(movement, movement) == 0.0f) {
        return;
    }

    camera.translate(normalize(movement) * (move_speed * time::delta()));
}

}  // namespace kuma
