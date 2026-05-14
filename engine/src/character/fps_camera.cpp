#include <kuma/fps_camera.h>

#include <kuma/camera.h>
#include <kuma/transform.h>

namespace kuma {

void FpsCameraController::update(Character& /*character*/,
                                  Transform& /*transform*/,
                                  Camera& /*camera*/) {
    // Real input wiring lands in the camera commit. For now this
    // stub exists so the public header has a definition the linker
    // can find and game code (and tests) can compile against.
}

}  // namespace kuma
