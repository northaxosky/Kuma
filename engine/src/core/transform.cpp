#include <kuma/transform.h>

namespace kuma {

void Transform::set_position(const Vec3& position) {
    position_ = position;
}

void Transform::set_rotation(const Quat& rotation) {
    rotation_ = rotation;
}

void Transform::set_scale(const Vec3& scale) {
    scale_ = scale;
}

void Transform::set_scale(float uniform_scale) {
    scale_ = {uniform_scale, uniform_scale, uniform_scale};
}

void Transform::set_rotation_euler(float yaw, float pitch, float roll) {
    rotation_ = Quat::from_euler(yaw, pitch, roll);
}

void Transform::set_rotation_axis_angle(const Vec3& axis, float angle_radians) {
    rotation_ = Quat::from_axis_angle(axis, angle_radians);
}

const Vec3& Transform::position() const {
    return position_;
}

const Quat& Transform::rotation() const {
    return rotation_;
}

const Vec3& Transform::scale() const {
    return scale_;
}

Mat4 Transform::model_matrix() const {
    // M = T * R * S - read right-to-left: scale, then rotate, then translate.
    Mat4 t = Mat4::translate(position_.x, position_.y, position_.z);
    Mat4 r = rotation_.to_mat4();
    Mat4 s = Mat4::scale(scale_.x, scale_.y, scale_.z);

    return t * r * s;
}

}  // namespace kuma
