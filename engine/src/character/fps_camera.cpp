#include <kuma/fps_camera.h>

#include <algorithm>
#include <cmath>

#include <kuma/camera.h>
#include <kuma/input.h>
#include <kuma/transform.h>

namespace kuma {

namespace {

constexpr float kMaxPitchRadians = 1.55334306f;  // 89 degrees - matches Camera

float read_axis_input(Key positive, Key negative) {
    float v = 0.0f;
    if (input::is_key_down(positive)) v += 1.0f;
    if (input::is_key_down(negative)) v -= 1.0f;
    return v;
}

}  // namespace

void FpsCameraController::update(Character& character,
                                  Transform& transform,
                                  Camera& camera) {
    // ── Mouse → yaw (character) + pitch (camera) ────────────────
    const Vec2 mouse = input::mouse_delta();
    character.yaw_radians -= mouse.x * mouse_sensitivity;
    pitch_radians_ -= mouse.y * mouse_sensitivity;
    pitch_radians_ = std::clamp(pitch_radians_, -kMaxPitchRadians, kMaxPitchRadians);

    // ── WASD → wish_direction in the character's yaw frame ──────
    // Convention: character.yaw = 0 faces world -Z (matches the
    // Camera's default forward). Yaw rotates around +Y in the
    // standard right-handed sense.
    //   forward = ( sin yaw, 0, -cos yaw)
    //   right   = ( cos yaw, 0,  sin yaw)
    const float forward_input = read_axis_input(Key::W, Key::S);
    const float strafe_input = read_axis_input(Key::D, Key::A);

    const float cy = std::cos(character.yaw_radians);
    const float sy = std::sin(character.yaw_radians);
    const Vec3 forward{sy, 0.0f, -cy};
    const Vec3 right{cy, 0.0f, sy};

    Vec3 wish{
        forward.x * forward_input + right.x * strafe_input,
        0.0f,
        forward.z * forward_input + right.z * strafe_input,
    };
    // character::simulate normalizes internally, but normalizing here
    // too lets game code peek at character.wish_direction and see a
    // proper direction vector regardless of input combos.
    const float len_sq = wish.x * wish.x + wish.z * wish.z;
    if (len_sq > 1.0f) {
        const float inv = 1.0f / std::sqrt(len_sq);
        wish.x *= inv;
        wish.z *= inv;
    }
    character.wish_direction = wish;

    // ── Jump (edge - one frame on key down) ─────────────────────
    if (input::was_key_pressed(Key::Space)) {
        character.wish_jump = true;
    }

    // ── Camera pose: position at character + eye_height, rotation ──
    // from the character's yaw plus the controller's pitch.
    //
    // Camera::set_rotation expects the camera's own yaw convention,
    // where forward = (cos cam_yaw, ..., sin cam_yaw). To make the
    // camera look in the same direction as the character, we offset:
    //   character forward (sin y, 0, -cos y) == camera forward
    //   (cos cy, 0, sin cy)  iff  cy = y - pi/2.
    const Vec3 cam_position{
        transform.position().x,
        transform.position().y + eye_height,
        transform.position().z,
    };
    camera.set_position(cam_position);
    camera.set_rotation(character.yaw_radians - 1.57079633f, pitch_radians_);
}

}  // namespace kuma
