#pragma once

// ── Kuma FPS Camera Controller ──────────────────────────────────
// Reads input + a Character + its Transform, writes character.yaw,
// character.wish_direction / wish_jump, and the camera's pose.
//
// Mirrors the FreeFlyCameraController pattern: the controller
// itself owns no state about the camera or character - it just
// reads the latest input snapshot and mutates whatever is passed in.
//
// Input mapping:
//   W/S       forward / back along character yaw
//   A/D       strafe perpendicular to yaw
//   Space     wish_jump (one-frame edge)
//   Mouse X   character yaw
//   Mouse Y   camera pitch (independent of character)
//
// Yaw and pitch are clamped: pitch saturates at +/- 89 degrees so
// the camera can't gimbal-flip; yaw wraps freely.

#include <kuma/character.h>
#include <kuma/math.h>

namespace kuma {

class Camera;
class Transform;

class FpsCameraController {
public:
    float mouse_sensitivity = 0.0025f;   // radians per pixel
    float eye_height = 1.6f;             // m above the character's transform position

    // Reads the current input snapshot, writes character + camera.
    // Call once per frame in the update phase, BEFORE
    // character::simulate so the wish inputs land in the right step.
    void update(Character& character, Transform& transform, Camera& camera);

private:
    float pitch_radians_ = 0.0f;
};

}  // namespace kuma
