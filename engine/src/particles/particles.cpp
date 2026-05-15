// ── Kuma Particles ──────────────────────────────────────────────
// CPU-driven billboard particles. Each emitter owns a fixed pool of
// particles laid out SoA. simulate() ticks them; render() (commit 3)
// uploads alive instances to the renderer's per-frame ring buffer
// and issues one instanced draw per emitter.
//
// Lifecycle rules:
//   - The pool lives inside the ECS component. No heap alloc per
//     emitter, no per-particle entities.
//   - simulate() runs in the UPDATE phase. Game code that triggers
//     bursts AFTER simulate has already run for the current frame
//     must use particles::spawn_burst() so the first batch lands
//     in the pool same-frame.
//   - Burst emitters with auto_despawn=true are destroyed once
//     their pool drains. Destruction is deferred to the end of the
//     simulate sweep to honor the ECS rule against mutating during
//     view iteration.

#include <kuma/log.h>
#include <kuma/material.h>
#include <kuma/particles.h>
#include <kuma/renderer.h>
#include <kuma/transform.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace kuma {

extern Renderer& get_renderer();  // defined in engine.cpp

namespace particles {

namespace {

struct State {
    bool initialized = false;

    // Cached default Material that wraps the renderer's 1x1 white
    // texture default. Built lazily on first request because the
    // renderer's material pool isn't ready until after particles::init.
    Material default_material;
    bool     default_material_ready = false;
};

State* g_state = nullptr;

// ── PRNG ────────────────────────────────────────────────────────
// Per-emitter xorshift32. Cheap, stateful, deterministic for a
// fixed seed - enough for visual variety in particle initial
// velocities and lifetimes.
inline uint32_t xorshift32(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Returns a uniform float in [0, 1).
inline float rand_unit(uint32_t& state) {
    return static_cast<float>(xorshift32(state) & 0x00FF'FFFFu)
         / static_cast<float>(0x0100'0000u);
}

// Returns a uniform float in [lo, hi).
inline float rand_range(uint32_t& state, float lo, float hi) {
    return lo + (hi - lo) * rand_unit(state);
}

// Returns a uniform Vec3 inside the AABB defined by lo/hi.
inline Vec3 rand_vec3(uint32_t& state, const Vec3& lo, const Vec3& hi) {
    return {
        rand_range(state, lo.x, hi.x),
        rand_range(state, lo.y, hi.y),
        rand_range(state, lo.z, hi.z),
    };
}

// Find the first dead slot in the pool. Linear scan is plenty fast
// for kCapacity=256 - validated against Godot's CPUParticles3D which
// uses the same approach. Returns -1 when the pool is full.
int find_dead_slot(const ParticleEmitter& e) {
    for (uint32_t i = 0; i < ParticleEmitter::kCapacity; ++i) {
        if (!e.alive[i]) return static_cast<int>(i);
    }
    return -1;
}

// Spawn one particle into the given slot. Caller is responsible for
// having checked that the slot is dead.
void spawn_into_slot(ParticleEmitter& e, uint32_t slot, const Vec3& origin) {
    e.positions[slot]   = origin;
    e.velocities[slot]  = rand_vec3(e.random_seed, e.velocity_min, e.velocity_max);
    e.lifetimes[slot]   = e.lifetime;
    e.start_lifes[slot] = e.lifetime;
    e.alive[slot]       = true;
}

// Spawn up to `count` new particles into dead slots, returning the
// number actually spawned (could be less if the pool fills up).
uint32_t spawn_burst_into_pool(ParticleEmitter& e, uint32_t count, const Vec3& origin) {
    uint32_t spawned = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const int slot = find_dead_slot(e);
        if (slot < 0) break;
        spawn_into_slot(e, static_cast<uint32_t>(slot), origin);
        ++spawned;
    }
    return spawned;
}

}  // namespace

bool init() {
    if (g_state != nullptr) return true;
    g_state = new State();
    g_state->initialized = true;
    kuma::log::info("Particles initialized");
    return true;
}

void shutdown() {
    if (g_state == nullptr) return;
    delete g_state;
    g_state = nullptr;
}

const Material* default_material() {
    if (g_state == nullptr) return nullptr;

    if (!g_state->default_material_ready) {
        // Allocate a one-slot Material backed entirely by the
        // renderer's default 1x1 white texture (and defaults for the
        // remaining four slots). Particles using this material render
        // as solid color modulated by the per-particle color attribute.
        const void* slot_ptrs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        void* set = get_renderer().create_material_descriptor_set(slot_ptrs);
        if (set == nullptr) return nullptr;

        g_state->default_material = Material{};
        g_state->default_material.descriptor_set = set;
        g_state->default_material_ready = true;
    }
    return &g_state->default_material;
}

void simulate(Registry& registry, float dt) {
    if (g_state == nullptr || dt <= 0.0f) return;

    // Collect entities marked for despawn during the sweep; honor
    // them after iteration so we don't mutate the registry during
    // view traversal (per ecs.h: "destroying entities during the
    // lifetime of a view() is undefined behavior").
    std::vector<EntityID> doomed;

    for (auto [entity, transform, emitter]
             : registry.view<Transform, ParticleEmitter>()) {

        // ── 1. spawn ───────────────────────────────────────────
        if (emitter.emitting) {
            // Continuous: time-based spawn accumulator. spawn_rate
            // is particles per second; accumulate fractional spawns
            // across frames so a 32/sec emitter at 60fps spawns
            // 32/60 each frame correctly on average.
            if (!emitter.one_shot && emitter.spawn_rate > 0.0f) {
                emitter.spawn_accumulator += emitter.spawn_rate * dt;
                while (emitter.spawn_accumulator >= 1.0f) {
                    const int slot = find_dead_slot(emitter);
                    if (slot < 0) {
                        // pool full - drop the spawn rather than
                        // recycle (recycling produces a visible
                        // "all the oldest particles vanish" pop)
                        emitter.spawn_accumulator = 0.0f;
                        break;
                    }
                    spawn_into_slot(emitter, static_cast<uint32_t>(slot),
                                    transform.position());
                    emitter.spawn_accumulator -= 1.0f;
                }
            }

            // Burst: pending_burst counts particles the spawn helper
            // wants the next simulate to fire. Drains across multiple
            // ticks if the pool fills.
            if (emitter.pending_burst > 0) {
                const uint32_t spawned = spawn_burst_into_pool(
                    emitter, emitter.pending_burst, transform.position());
                emitter.pending_burst -= spawned;
                // One-shot: once we've fired the configured burst
                // (no more pending), stop emitting so the next
                // simulate doesn't keep checking for spawns.
                if (emitter.one_shot && emitter.pending_burst == 0) {
                    emitter.emitting = false;
                }
            }
        }

        // ── 2. tick alive particles ─────────────────────────────
        emitter.alive_count = 0;
        for (uint32_t i = 0; i < ParticleEmitter::kCapacity; ++i) {
            if (!emitter.alive[i]) continue;

            emitter.lifetimes[i] -= dt;
            if (emitter.lifetimes[i] <= 0.0f) {
                emitter.alive[i] = false;
                continue;
            }

            // Semi-implicit Euler: velocity first, then position.
            // Same integrator the physics module uses for stable
            // small-step simulation.
            emitter.velocities[i].x += emitter.gravity.x * dt;
            emitter.velocities[i].y += emitter.gravity.y * dt;
            emitter.velocities[i].z += emitter.gravity.z * dt;
            emitter.positions[i].x  += emitter.velocities[i].x * dt;
            emitter.positions[i].y  += emitter.velocities[i].y * dt;
            emitter.positions[i].z  += emitter.velocities[i].z * dt;

            ++emitter.alive_count;
        }

        // ── 3. auto-despawn ─────────────────────────────────────
        // A burst emitter is "spent" when its pool has fully drained
        // AND it isn't still waiting to spawn more. Continuous
        // emitters never auto-despawn; game code owns their lifetime.
        if (emitter.auto_despawn && emitter.one_shot
            && emitter.alive_count == 0
            && emitter.pending_burst == 0
            && !emitter.emitting) {
            doomed.push_back(entity);
        }
    }

    for (EntityID e : doomed) {
        registry.destroy_entity(e);
    }
}

void render(Registry& registry, const Mat4& view, const Mat4& view_projection) {
    // The instance upload + draw flow ships in the next commit. For
    // now the function exists so engine.cpp can wire it into the
    // frame contract; calling it is a safe no-op.
    (void)registry;
    (void)view;
    (void)view_projection;
}

EntityID spawn_burst(Registry& registry, const ParticleEmitter& preset, const Vec3& position) {
    EntityID e = registry.create_entity();
    if (e == kInvalidEntity) return kInvalidEntity;

    Transform t;
    t.set_position(position);
    registry.add(e, t);

    // Copy the preset, force one_shot semantics, and fire the burst
    // immediately so render() can pick the particles up THIS frame
    // (the alternative - waiting for the next simulate - introduces
    // a one-frame delay that's visibly wrong for impact feedback).
    ParticleEmitter emitter = preset;
    emitter.one_shot = true;
    emitter.emitting = true;

    const uint32_t requested = emitter.burst_count > 0
                                   ? emitter.burst_count
                                   : static_cast<uint32_t>(emitter.spawn_rate);
    emitter.pending_burst = 0;
    emitter.alive_count   = spawn_burst_into_pool(emitter, requested, position);

    // If the configured burst exceeded the pool capacity, defer the
    // remainder for next simulate to drain naturally.
    if (emitter.alive_count < requested) {
        emitter.pending_burst = requested - emitter.alive_count;
    }
    // If the whole burst fit in the pool, this emitter has no more
    // particles to spawn ever - flip emitting off so simulate's
    // "burst path" stops doing pointless work each frame.
    if (emitter.pending_burst == 0) {
        emitter.emitting = false;
    }

    registry.add(e, emitter);
    return e;
}

}  // namespace particles
}  // namespace kuma
