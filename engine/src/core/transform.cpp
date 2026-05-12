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
    // Build TRS by composing three matrices. Mat4 multiply is read
    // right-to-left, so this applies scale first, then rotate, then
    // translate - exactly the convention almost every engine uses.
    Mat4 t = Mat4::translate(position_.x, position_.y, position_.z);
    Mat4 r = rotation_.to_mat4();

    // No Mat4::scale factory yet - the diagonal form is a one-liner
    // and self-contained, so inline it instead of growing math.h
    // for a single user.
    Mat4 s = Mat4::identity();
    s(0, 0) = scale_.x;
    s(1, 1) = scale_.y;
    s(2, 2) = scale_.z;

    return t * r * s;
}

}  // namespace kuma
