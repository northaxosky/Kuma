#pragma once

#include <kuma/ecs.h>
#include <kuma/material.h>
#include <kuma/math.h>

#include <cstdint>

namespace kuma {

// ── ParticleDrawOrder ───────────────────────────────────────────
// How the particles inside a single emitter are ordered when their
// instance data is uploaded to the GPU. Ordering happens via index
// permutation - the pool storage is never reordered.
//
// Index      cheapest. Particles draw in their slot order. Fine
//            when the visual is dominated by short, additive bursts
//            (impact spark, muzzle flash) or by a texture that is
//            mostly opaque-with-soft-edges.
// Lifetime   oldest first. Newer particles draw on top - useful
//            when freshly-spawned particles should appear over the
//            tail of a continuous stream.
// ViewDepth  back-to-front by camera distance. Required for proper
//            alpha blending when many particles overlap (smoke,
//            dense pickup glints).
enum class ParticleDrawOrder {
    Index,
    Lifetime,
    ViewDepth,
};

// ── ParticleEmitter ─────────────────────────────────────────────
// ECS component owning a fixed-capacity pool of particles laid out
// SoA. Particles spawn at the parent entity's Transform position
// and live in world space - subsequent transform changes do not
// drag the existing particles around (standard "fire and forget"
// behavior expected of FX particles).
//
// Mode is controlled by two orthogonal flags:
//   one_shot=false, emitting=true   continuous: spawn_rate per second
//   one_shot=false, emitting=false  continuous but currently paused
//   one_shot=true,  emitting=true   burst: fires burst_count immediately
//                                    next simulate, then auto-clears
//                                    emitting (and despawns the entity
//                                    if auto_despawn is set)
//
// For "trigger an effect right now" use cases prefer
// particles::spawn_burst() which sets up the entity and instantiates
// the first batch of particles in the same frame the call returns.
struct ParticleEmitter {
    static constexpr uint32_t kCapacity = 256;

    // ── mode ────────────────────────────────────────────────────
    bool one_shot     = false;
    bool emitting     = false;
    bool auto_despawn = true;

    // ── shared config ───────────────────────────────────────────
    float lifetime     = 1.0f;        // seconds per particle
    float spawn_rate   = 32.0f;       // continuous: particles per second
    uint32_t burst_count = 0;         // one_shot: particles per trigger

    Vec3  gravity      = {0.0f, -9.8f, 0.0f};
    Vec3  velocity_min = {-1.0f, 1.0f, -1.0f};   // initial velocity sampled uniformly
    Vec3  velocity_max = { 1.0f, 3.0f,  1.0f};   // inside this AABB
    float size_start   = 0.10f;
    float size_end     = 0.02f;
    Vec4  color_start  = {1.0f, 1.0f, 1.0f, 1.0f};   // RGBA at spawn
    Vec4  color_end    = {1.0f, 1.0f, 1.0f, 0.0f};   // RGBA at death

    // ── render config ───────────────────────────────────────────
    const Material*  material   = nullptr;                  // null -> particles::default_material()
    ParticleDrawOrder draw_order = ParticleDrawOrder::Index;

    // ── pool (SoA, fixed) ───────────────────────────────────────
    Vec3   positions  [kCapacity]{};
    Vec3   velocities [kCapacity]{};
    float  lifetimes  [kCapacity]{};      // seconds remaining
    float  start_lifes[kCapacity]{};      // for ramp lerp t = 1 - life/start
    bool   alive      [kCapacity]{};

    // ── runtime book-keeping ────────────────────────────────────
    uint32_t alive_count       = 0;
    float    spawn_accumulator = 0.0f;    // continuous spawn timer
    uint32_t pending_burst     = 0;       // burst particles waiting to fire next simulate
    uint32_t random_seed       = 0x9E3779B9u;  // per-emitter PRNG state (xorshift)
};

namespace particles {

// ── Module lifecycle (engine-internal) ──────────────────────────
// Game code does not call these. Engine::init/shutdown do.
bool init();
void shutdown();

// Walks every entity with a ParticleEmitter, advances simulation
// by dt, spawns / kills particles, and auto-despawns spent burst
// emitters whose pool has drained. Runs in the UPDATE phase of
// the frame contract.
void simulate(Registry& registry, float dt);

// Walks every entity with a ParticleEmitter and a Transform, sorts
// emitters back-to-front by camera distance, sorts particles
// within each emitter per its draw_order, uploads to the renderer's
// instance ring buffer, and issues one instanced draw per emitter.
//
// Must run AFTER all opaque draws so the depth values opaque
// geometry wrote are present when the particle pipeline (depth
// test on, depth write off) does its visibility checks.
void render(Registry& registry, const Mat4& view, const Mat4& view_projection);

// Default material the particle pipeline binds when an emitter's
// `material` field is null. Backed by the renderer's 1x1 white
// default texture - particles render as solid color_start_to_end.
const Material* default_material();

// ── Spawn helpers ───────────────────────────────────────────────
// Fire-and-forget burst: creates a new entity at `position` with
// the given preset configured for one_shot use, instantiates the
// initial particles into the pool RIGHT NOW (so the burst is
// visible the same frame this call returns instead of next frame),
// and returns the entity. With auto_despawn=true (the default for
// presets) the entity destroys itself once every particle dies.
EntityID spawn_burst(Registry& registry, const ParticleEmitter& preset,
                     const Vec3& position);

}  // namespace particles

// ── Built-in presets (impls in commit 4) ────────────────────────
ParticleEmitter make_muzzle_flash();
ParticleEmitter make_impact_spark();
ParticleEmitter make_blood_spatter();
ParticleEmitter make_pickup_sparkle();
ParticleEmitter make_death_poof();

}  // namespace kuma
