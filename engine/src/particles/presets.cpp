// ── Particle Presets ────────────────────────────────────────────
// Pre-configured ParticleEmitter values for the standard FX-feedback
// set. Each function returns a fully-initialized component ready to
// drop into registry.add(entity, ...) or particles::spawn_burst().
//
// Presets leave `material` null so particles::render falls back to
// the default white texture - none of these need a custom asset.
// Game code can override any field after construction.
//
// Tuning notes are in the source so anyone re-tuning a preset can
// see what it's optimizing for at a glance.

#include <kuma/particles.h>

namespace kuma {

// ── Burst presets ──────────────────────────────────────────────
// All five share a "fire and forget" lifecycle: instantiate via
// particles::spawn_burst, the burst fires immediately, the entity
// auto-despawns once the pool drains.

ParticleEmitter make_muzzle_flash() {
    // Very short, very bright. The signature feedback for "I shot."
    // Lifetime kept under 100ms so consecutive shots don't build up
    // an unreadable smear at high fire rates. Low gravity because
    // muzzle puffs hang in the air briefly rather than falling.
    ParticleEmitter e{};
    e.lifetime     = 0.08f;
    e.burst_count  = 12;
    e.gravity      = {0.0f, -0.5f, 0.0f};
    e.velocity_min = {-1.0f, -0.5f, -1.0f};
    e.velocity_max = { 1.0f,  1.0f,  1.0f};
    e.size_start   = 0.18f;
    e.size_end     = 0.05f;
    e.color_start  = {1.0f, 0.95f, 0.6f, 1.0f};   // bright yellow-white
    e.color_end    = {1.0f, 0.45f, 0.1f, 0.0f};   // orange fade
    e.draw_order   = ParticleDrawOrder::Index;
    return e;
}

ParticleEmitter make_impact_spark() {
    // Bullet hit on a hard surface. Larger spread than muzzle
    // flash since the impact angle scatters in every direction,
    // and stronger gravity so sparks visibly fall instead of
    // floating like smoke. Hot white core fading to dark orange.
    ParticleEmitter e{};
    e.lifetime     = 0.6f;
    e.burst_count  = 32;
    e.gravity      = {0.0f, -8.0f, 0.0f};
    e.velocity_min = {-3.0f,  0.5f, -3.0f};
    e.velocity_max = { 3.0f,  4.0f,  3.0f};
    e.size_start   = 0.10f;
    e.size_end     = 0.02f;
    e.color_start  = {1.0f, 1.0f,  0.85f, 1.0f};
    e.color_end    = {0.9f, 0.35f, 0.05f, 0.0f};
    e.draw_order   = ParticleDrawOrder::Index;
    return e;
}

ParticleEmitter make_blood_spatter() {
    // Hit feedback on an enemy. Heavier than impact_spark so the
    // particles arc downward visibly. Dark-red to darker-red so the
    // effect reads as blood, not fire. Slightly longer lifetime so
    // the spatter is visible long enough to register at high speeds.
    ParticleEmitter e{};
    e.lifetime     = 0.9f;
    e.burst_count  = 24;
    e.gravity      = {0.0f, -12.0f, 0.0f};
    e.velocity_min = {-2.5f,  0.0f, -2.5f};
    e.velocity_max = { 2.5f,  3.0f,  2.5f};
    e.size_start   = 0.12f;
    e.size_end     = 0.04f;
    e.color_start  = {0.55f, 0.05f, 0.05f, 1.0f};
    e.color_end    = {0.20f, 0.02f, 0.02f, 0.0f};
    e.draw_order   = ParticleDrawOrder::Index;
    return e;
}

ParticleEmitter make_death_poof() {
    // "Thing died" smoke cloud. Slow upward drift (negative gravity
    // = buoyant smoke), grays fading to transparent. Long-ish life
    // so the poof has time to expand before vanishing. Larger
    // particle count so the cloud reads as substantial.
    ParticleEmitter e{};
    e.lifetime     = 1.4f;
    e.burst_count  = 48;
    e.gravity      = {0.0f, 1.5f, 0.0f};        // buoyancy, not gravity
    e.velocity_min = {-1.5f, 0.5f, -1.5f};
    e.velocity_max = { 1.5f, 1.8f,  1.5f};
    e.size_start   = 0.15f;
    e.size_end     = 0.45f;                      // expands as it rises
    e.color_start  = {0.7f, 0.7f, 0.7f, 0.9f};
    e.color_end    = {0.4f, 0.4f, 0.4f, 0.0f};
    e.draw_order   = ParticleDrawOrder::ViewDepth;  // overlapping cloud particles need it
    return e;
}

// ── Continuous preset ──────────────────────────────────────────
// Continuous emitters live with their parent entity until game
// code removes them. auto_despawn is false because the entity
// lifetime belongs to whoever owns it (the pickup, the aura source,
// the trail's anchor).

ParticleEmitter make_pickup_sparkle() {
    // Slow, gentle, gold. Designed to sit at an item-drop position
    // and twinkle continuously until the player picks the item up
    // (game code removes the entity at that point). Per-particle
    // lifetime is long so the cloud feels populated at low spawn
    // rate.
    ParticleEmitter e{};
    e.one_shot     = false;
    e.emitting     = true;
    e.auto_despawn = false;
    e.lifetime     = 1.5f;
    e.spawn_rate   = 12.0f;
    e.burst_count  = 0;
    e.gravity      = {0.0f, 0.6f, 0.0f};         // gentle upward drift
    e.velocity_min = {-0.3f, 0.2f, -0.3f};
    e.velocity_max = { 0.3f, 0.6f,  0.3f};
    e.size_start   = 0.06f;
    e.size_end     = 0.02f;
    e.color_start  = {1.0f, 0.85f, 0.3f, 1.0f};  // gold
    e.color_end    = {1.0f, 0.6f,  0.1f, 0.0f};
    e.draw_order   = ParticleDrawOrder::Index;
    return e;
}

}  // namespace kuma
