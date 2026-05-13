#include "physics/jolt_globals.h"

#include <mutex>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

namespace kuma::physics::detail {

namespace {

std::once_flag g_init_once;

void init_jolt_globals_once() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

}  // namespace

void ensure_jolt_globals_initialized() {
    std::call_once(g_init_once, init_jolt_globals_once);
}

}  // namespace kuma::physics::detail
