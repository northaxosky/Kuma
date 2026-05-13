#pragma once

// Process-global Jolt initialization. Jolt's Factory and type
// registration are process-globals; calling them more than once
// leaks or fails. Guarded with std::call_once so multiple
// PhysicsSystem instances (e.g. across unit tests) share one init.

namespace kuma::physics::detail {

// Idempotent. Safe to call from any thread; subsequent calls are
// cheap atomic checks. Never paired with an explicit teardown -
// the OS reclaims the Jolt factory at process exit, which is more
// robust than racing against destruction order.
void ensure_jolt_globals_initialized();

}  // namespace kuma::physics::detail
