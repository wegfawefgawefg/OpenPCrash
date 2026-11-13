#pragma once
// The particle pool (decomp docs/port/effects.md "Particles", src/od/u_05fbd0.c, include/effect.h):
// 0x90 emitters of 0x70 particles each, allocated by effect_spawn_emitter, stepped once a 30 Hz
// update by effect_update_emitter. Presentation: nothing in the fight reads a particle. It runs in
// the original's frame (left-handed, z not negated) on the original's per-update constants, and
// draws its own random numbers from a CRT-style generator per emitter, never the world's.
//
// Each update the particles' draws (particle_draw's billboard_push) go to `pushes` with the
// position before the step too, so a frame between two updates draws them interpolated.
#include "effects/effect_data.hpp"
#include "render/d3dx_math.hpp"

#include <cstdint>
#include <vector>

inline constexpr int emitter_count = 0x90;
inline constexpr int particle_count = 0x70;
inline constexpr float frame_cadence = 30; // updates a second in every state with effects

// Emitter.flags (EffectEmitter.flags), docs/port/effects.md "Emitter flags"
enum EmitterFlag : std::uint32_t {
    ef_particles = 0x2,      // needed to spawn
    ef_stealable = 0x4,      // a new emitter may take the slot
    ef_fill = 0x8,           // spawn along the emitter's motion
    ef_hollow = 0x10,        // tilt is exactly the spread
    ef_ring = 0x20,          // pushed out to burst_radius off the launch axis
    ef_shell = 0x40,         // a sphere shell of burst_radius
    ef_spin = 0x80,          // spin rand(+-0.2 pi) an update
    ef_streak15 = 0x100,     // billboard modes 3 and 4
    ef_streak3 = 0x200,      // modes 5 and 6
    ef_short_streak = 0x400, // modes 5 and 6 over 0.2 x velocity
    ef_cluster = 0x800,      // offset a random point within burst_radius
    ef_chain = 0x1000,       // the arcs: particles laid along the emitter matrix
    ef_world = 0x2000,       // particles don't ride the emitter
    ef_random_yaw = 0x4000,  // launch speed x rand(0.2, 1), force A's yaw re-rolled
    ef_gust = 0x8000,        // force A swings by two sines of the emitter age
    ef_sink = 0x10000,       // a particle dies near the emitter offset
    ef_paper = 0x20000,      // the ending's paper storm
    ef_toward = 0x40000,     // billboard mode 9, pulled 2 units toward the camera
    ef_protected = 0x80000,  // allocation passes it over
    ef_hidden = 0x100000,    // effect_hide
};

// Emitter.run_flags
enum RunFlag : int { run_emitting = 1, run_spawned = 2, run_finishing = 4 };

// a curve read while it plays (Particle's prev / next key pointers): indices into the record's list
struct CurveCursor {
    std::uint8_t prev = 0, next = 0;
};

struct Particle {
    bool active = false;
    int age = 0;
    int life = 0; // updates; life_hold waits for the emitter (a dome's parent)
    int emitter_age = 0;
    int billboard_mode = 0;
    int record = -1;
    CurveCursor colors[4], scale, force_b, force_a, mass, speed, speed_min, speed_max;
    Vec3f gravity, origin;
    Vec3f wander; // the emitter's rotation at spawn; paper: drift x, z and countdown y; gust: x
    Vec3f spawn_scale, emitter_velocity;
    Vec3f position, velocity;
    Vec3f before; // position before this update's step: the draw's interpolation start
    Vec3f curve_scale{1, 1, 1}, base_scale{1, 1, 1};
    std::uint32_t color = 0; // ARGB
    Mat4 spawn_transform;
    float spin = 0, angle = 0;
};

inline constexpr int life_hold = static_cast<int>(0x80000000u);

struct Emitter {
    bool active = false;
    int run_flags = 0;
    int list = -1;
    std::uint32_t flags = 0;
    int age = 0;
    Vec3f gravity; // a step squared
    float spawn_debt = 0;
    int record = -1;
    CurveCursor rate;
    Vec3f position, prev_position, spawn_scale, velocity, rotation, offset;
    Mat4 transform;
    int next_particle = 0;
    int count = 0; // live particles
    int owner = -1;
    std::uint32_t seed = 1; // the generator (rand_crt's LCG), seeded per effect
    float own_rate = -1;    // emitter_setup[9] for this emitter alone (get-item), -1 the record's
    Particle particles[particle_count];
};

// a billboard_push of this update, in the original's world frame (mode 2: a view-space offset)
struct BillboardPush {
    Vec3f position, before; // after and before the update's step
    float size = 0;
    float rgba[4] = {};
    int cell = 0;
    bool additive = false; // draw_flag nonzero: w = 0
    int mode = 0;
    float angle = 0;
};

// a handle: the slot effect_spawn_emitter returned and the owner serial it wrote
struct EffectHandle {
    int slot = -1;
    int owner = -1;
};

struct ParticlePool {
    std::vector<ParticleRecord> records; // a copy: get-item and hover wash write into them
    const EffectData* data = nullptr;
    int cursor = 0;
    int owner_serial = 0;
    std::uint32_t seed = 1; // seeds each new emitter's generator
    std::vector<Emitter> emitters = std::vector<Emitter>(emitter_count);
    std::vector<BillboardPush> pushes;
    std::vector<EffectHandle> spawned; // every emitter the last effect_spawn / set_play started
    // GameState's paper anchors as the ending's scene sets them (f_10efc, f_10f04)
    bool paper_near = false, paper_high = false;
};

// effect_pool_clear (at every battle start): empty, cursor and serial 0
void particle_pool_clear(ParticlePool& pool, const EffectData& data, std::uint32_t seed);
// effect_pool_update: one 30 Hz update of every emitter, the pushes rebuilt
void particle_pool_update(ParticlePool& pool);

// effect_spawn_emitter: each emitter row of list `list` into a slot; the last row's
// handle (slot -1 when it found none)
EffectHandle effect_spawn(ParticlePool& pool, int list, const Vec3f& pos, const Vec3f& prev);
// effect_set_play's emitters: every list of the set; the last emitter's handle. The
// set's sprite effects are the caller's (effect_set_sprites)
EffectHandle effect_set_play(ParticlePool& pool, int set, const Vec3f& pos, const Vec3f& prev);
// the handle's emitter when its slot is live and still the owner's, else null
Emitter* effect_emitter(ParticlePool& pool, EffectHandle handle);
bool effect_active(const ParticlePool& pool, EffectHandle handle); // emitters[h].active alone
void effect_set_position(ParticlePool& pool, EffectHandle handle, const Vec3f& pos);
void effect_set_prev_position(ParticlePool& pool, EffectHandle handle, const Vec3f& pos);
void effect_set_matrix(ParticlePool& pool, EffectHandle handle, const Mat4& matrix);
// every emitter the last play started (retail turns only the last: docs/bugs.md)
void effect_set_matrix_spawned(ParticlePool& pool, const Mat4& matrix);
void effect_set_offset(ParticlePool& pool, EffectHandle handle, const Vec3f& offset);
void effect_finish(ParticlePool& pool, EffectHandle handle);
void effect_resume(ParticlePool& pool, EffectHandle handle);
// effect_stop (owner -1 takes any owner); a slot of -1 is ignored, which retail lacks
void effect_stop(ParticlePool& pool, EffectHandle handle, bool any_owner = false);
void effect_show(ParticlePool& pool, int slot, bool shown);

// particle_start.cpp: particle_start (the new particle's first update included)
void particle_start(ParticlePool& pool, Emitter& emitter, Particle& particle);
// particle_step.cpp: particle_update (step, draw, age, death)
void particle_update(ParticlePool& pool, Emitter& emitter, Particle& particle);
// the generator: rand_crt, rand_range, rand_float_crt
int crt_rand(std::uint32_t& seed);
float rand_range(std::uint32_t& seed, float low, float high);
float rand_float_crt(std::uint32_t& seed, float range);
// D3DXMatrixRotationAxis
Mat4 mat_rotation_axis(const Vec3f& axis, float angle);
// curve_time
float curve_time(const Particle& particle, int kind);
// a FloatKey curve's value at t, walking the cursor forward as the original does; none: fallback
float curve_value(const std::vector<FloatKey>& keys, CurveCursor& cursor, float t, float fallback);
// emitter_next_free_particle
int next_free_particle(Emitter& emitter);
