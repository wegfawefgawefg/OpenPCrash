#include "effects/particles.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.1415927f;

// a seed for a new emitter's generator: the pool's seed and the owner serial, mixed
std::uint32_t emitter_seed(std::uint32_t pool_seed, int owner) {
    std::uint32_t h = pool_seed ^ (static_cast<std::uint32_t>(owner) * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}

// decomp: emitter_start 0x60087
void emitter_start(ParticlePool& pool, Emitter& e) {
    const ParticleRecord& r = pool.records[static_cast<std::size_t>(e.record)];
    e.run_flags = run_emitting;
    e.next_particle = 0;
    e.position = {r.emitter_setup[0], r.emitter_setup[1], r.emitter_setup[2]};
    e.prev_position = e.position;
    e.rotation = {r.emitter_setup[3], r.emitter_setup[4], r.emitter_setup[5]};
    e.spawn_scale = {r.emitter_setup[6], r.emitter_setup[7], r.emitter_setup[8]};
    e.velocity = {};
    e.offset = {r.offset[0], r.offset[1], r.offset[2]};
    e.transform = mat_identity();
    e.rate = {};
    e.age = 0;
    // gravity: (0, -1, 0) through RotX RotY RotZ, a second squared to an update squared
    if (!r.gravity_off) {
        const Mat4 turn =
            mat_multiply(mat_rotation_x(r.gravity[0]),
                         mat_multiply(mat_rotation_y(r.gravity[1]), mat_rotation_z(r.gravity[2])));
        e.gravity = vec_scale(transform_coord({0, -1, 0}, turn),
                              r.gravity[3] / (frame_cadence * frame_cadence));
    } else {
        e.gravity = {};
    }
    e.count = 0;
    for (Particle& p : e.particles) {
        p.active = false;
    }
    if (e.flags & ef_cluster) {
        const float yaw = rand_range(e.seed, 0, pi);
        const float roll = rand_range(e.seed, -pi, pi);
        const Vec3f out{r.burst_radius * rand_range(e.seed, 0.2f, 1.0f), 0, 0};
        e.offset = transform_coord(out, mat_multiply(mat_rotation_z(roll), mat_rotation_y(yaw)));
    }
    if (e.flags & ef_gust) {
        e.rotation.y = rand_range(e.seed, 0, pi);
    }
    e.spawn_debt = 0;
}

// particles owed this update
// decomp: emitter_spawn_rate 0x60958
float emitter_spawn_rate(ParticlePool& pool, Emitter& e) {
    const ParticleRecord& r = pool.records[static_cast<std::size_t>(e.record)];
    if (e.run_flags & run_finishing) {
        e.run_flags &= ~run_emitting;
        return 0;
    }
    float rate = 0;
    if (!r.rate_curve.empty()) {
        const std::vector<FloatKey>& keys = r.rate_curve;
        const float age = static_cast<float>(e.age);
        while (age > keys[e.rate.next].time && keys[e.rate.next].time >= 0 &&
               e.rate.next + 1u < keys.size()) {
            e.rate.prev = e.rate.next;
            e.rate.next += 1;
        }
        const FloatKey& prev = keys[e.rate.prev];
        const FloatKey& next = keys[e.rate.next];
        if (next.time >= 0 && next.time != prev.time) {
            rate = prev.value +
                   (next.value - prev.value) * ((age - prev.time) / (next.time - prev.time));
        } else {
            rate = prev.value;
            // past the last key with nothing to emit: the effect ends by itself
            if (next.time < 0 && rate <= 0) {
                e.run_flags &= ~run_emitting;
            }
        }
    } else {
        rate = (e.own_rate >= 0 ? e.own_rate : r.emitter_setup[9]) +
               r.emitter_setup[10] * rand_range(e.seed, -1, 1);
    }
    if (rate <= 0) {
        rate = 0;
    }
    return rate / frame_cadence;
}

// decomp: emitter_spawn_due 0x606FE
void emitter_spawn_due(ParticlePool& pool, Emitter& e) {
    e.spawn_debt += emitter_spawn_rate(pool, e);
    if (e.spawn_debt < 1) {
        return;
    }
    const int owed = static_cast<int>(e.spawn_debt);
    for (int i = 0; i < owed; ++i) {
        if (e.count >= 0x6F) {
            // at the cap the debt is dropped
            e.spawn_debt = 0;
            return;
        }
        Particle& p = e.particles[e.next_particle];
        p.active = true;
        p.emitter_age = e.age;
        p.record = e.record;
        p.gravity = e.gravity;
        p.emitter_velocity = e.velocity;
        p.origin = e.position;
        p.wander = e.rotation;
        p.spawn_scale = e.spawn_scale;
        p.spawn_transform = e.transform;
        e.next_particle = next_free_particle(e);
        e.count += 1;
        particle_start(pool, e, p);
        e.spawn_debt -= 1;
        e.run_flags |= run_spawned;
    }
}

// emitter_finished; one that stopped emitting before its first particle ends too (retail
// keeps it for the battle: docs/bugs.md)
// decomp: emitter_finished 0x65511
bool emitter_finished(Emitter& e) {
    if (!(e.run_flags & run_emitting) && e.count <= 0) {
        e.run_flags &= ~run_finishing;
        return true;
    }
    return false;
}

// decomp: effect_update_emitter 0x60488
void emitter_update(ParticlePool& pool, Emitter& e) {
    e.velocity = vec_sub(e.position, e.prev_position);
    e.prev_position = e.position;
    for (Particle& p : e.particles) {
        if (p.active) {
            particle_update(pool, e, p);
        }
    }
    emitter_spawn_due(pool, e);
    e.age += 1;
    if (emitter_finished(e)) {
        e.active = false;
    }
}

} // namespace

int crt_rand(std::uint32_t& seed) {
    seed = seed * 214013u + 2531011u;
    return static_cast<int>(seed >> 16 & 0x7FFF);
}

// decomp: rand_range 0x64B77
float rand_range(std::uint32_t& seed, float low, float high) {
    const float value = static_cast<float>(crt_rand(seed)) * 3.051851e-05f * (high - low) + low;
    return value > high ? high : value < low ? low : value;
}

// decomp: rand_float_crt 0xD3E3A
float rand_float_crt(std::uint32_t& seed, float range) {
    return static_cast<float>(crt_rand(seed)) / 32768.0f * range;
}

Mat4 mat_rotation_axis(const Vec3f& axis, float angle) {
    const Vec3f n = vec_normalize(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    Mat4 m;
    m.m[0][0] = t * n.x * n.x + c;
    m.m[0][1] = t * n.x * n.y + s * n.z;
    m.m[0][2] = t * n.x * n.z - s * n.y;
    m.m[1][0] = t * n.x * n.y - s * n.z;
    m.m[1][1] = t * n.y * n.y + c;
    m.m[1][2] = t * n.y * n.z + s * n.x;
    m.m[2][0] = t * n.x * n.z + s * n.y;
    m.m[2][1] = t * n.y * n.z - s * n.x;
    m.m[2][2] = t * n.z * n.z + c;
    return m;
}

// decomp: curve_time 0x64B0A
float curve_time(const Particle& p, int kind) {
    switch (kind) {
    case 0:
        return 0;
    case 1:
        return static_cast<float>(p.age);
    case 2:
        return static_cast<float>(p.emitter_age);
    default:
        return p.life > 0 ? static_cast<float>(p.age) * 100.0f / static_cast<float>(p.life)
                          : 100.0f;
    }
}

float curve_value(const std::vector<FloatKey>& keys, CurveCursor& cursor, float t, float fallback) {
    if (keys.empty()) {
        return fallback;
    }
    while (t > keys[cursor.next].time && keys[cursor.next].time >= 0 &&
           cursor.next + 1u < keys.size()) {
        cursor.prev = cursor.next;
        cursor.next += 1;
    }
    const FloatKey& prev = keys[cursor.prev];
    const FloatKey& next = keys[cursor.next];
    if (next.time >= 0 && next.time != prev.time) {
        return prev.value + (next.value - prev.value) * ((t - prev.time) / (next.time - prev.time));
    }
    return prev.value;
}

// decomp: emitter_next_free_particle 0x608DD
int next_free_particle(Emitter& e) {
    e.next_particle = (e.next_particle + 1) % particle_count;
    int next = e.next_particle;
    for (int i = 0; i < particle_count; ++i) {
        if (!e.particles[next].active) {
            break;
        }
        next = (next + 1) % particle_count;
    }
    return next;
}

// decomp: effect_pool_clear 0x5FBD0
void particle_pool_clear(ParticlePool& pool, const EffectData& data, std::uint32_t seed) {
    pool.data = &data;
    pool.records = data.records;
    pool.cursor = 0;
    pool.owner_serial = 0;
    pool.seed = seed;
    for (Emitter& e : pool.emitters) {
        e.active = false;
        e.owner = -1;
        e.flags = 0;
    }
    pool.pushes.clear();
}

// decomp: effect_pool_update 0x5FC87
void particle_pool_update(ParticlePool& pool) {
    pool.pushes.clear();
    for (Emitter& e : pool.emitters) {
        if (e.active) {
            emitter_update(pool, e);
        }
    }
}

// decomp: effect_spawn_emitter 0x5FD28
EffectHandle effect_spawn(ParticlePool& pool, int list, const Vec3f& pos, const Vec3f& prev) {
    EffectHandle last;
    if (!pool.data || list < 0 || list >= static_cast<int>(pool.data->lists.size())) {
        return last;
    }
    for (const EmitterRow& row : pool.data->lists[static_cast<std::size_t>(list)].rows) {
        if (!(row.flags & ef_particles) || row.record < 0 ||
            row.record >= static_cast<int>(pool.records.size())) {
            continue;
        }
        // a busy slot: walk on to a free or stealable one; protected ones are passed over
        int at = pool.cursor;
        Emitter* e = &pool.emitters[static_cast<std::size_t>(at)];
        if (e->active) {
            unsigned count = 0;
            if (!(e->flags & ef_stealable)) {
                for (;;) {
                    pool.cursor = (pool.cursor + 1) % emitter_count;
                    at = pool.cursor;
                    e = &pool.emitters[static_cast<std::size_t>(at)];
                    if (!e->active) {
                        break;
                    }
                    if (e->flags & ef_protected) {
                        count++;
                        continue;
                    }
                    if (e->flags & ef_stealable) {
                        break;
                    }
                    if (++count >= 100) {
                        break;
                    }
                }
            }
            if (count >= 0x8F) {
                for (int i = 0; i < emitter_count; ++i) {
                    pool.cursor = (pool.cursor + 1) % emitter_count;
                    at = pool.cursor;
                    e = &pool.emitters[static_cast<std::size_t>(at)];
                    if (!(e->flags & ef_protected)) {
                        break;
                    }
                }
            }
            if (e->active && (e->flags & ef_stealable)) {
                e->run_flags &= ~run_emitting;
                e->active = false;
            }
        }
        if (!e->active || (e->flags & ef_protected)) {
            e->active = true;
            e->list = list;
            e->flags = row.flags;
            e->record = row.record;
            e->owner = pool.owner_serial++;
            e->seed = emitter_seed(pool.seed, e->owner);
            e->own_rate = -1;
            emitter_start(pool, *e);
            e->position = pos;
            e->prev_position = prev;
            last = {at, e->owner};
            pool.spawned.push_back(last);
        } else {
            last = {};
        }
        pool.cursor = (pool.cursor + 1) % emitter_count;
    }
    return last;
}

// decomp: effect_set_play 0x576E0
EffectHandle effect_set_play(ParticlePool& pool, int set, const Vec3f& pos, const Vec3f& prev) {
    pool.spawned.clear();
    EffectHandle last;
    if (!pool.data || set < 0 || set >= static_cast<int>(pool.data->sets.size())) {
        return last;
    }
    for (int list : pool.data->sets[static_cast<std::size_t>(set)].effects) {
        last = effect_spawn(pool, list, pos, prev);
    }
    return last;
}

Emitter* effect_emitter(ParticlePool& pool, EffectHandle handle) {
    if (handle.slot < 0 || handle.slot >= emitter_count) {
        return nullptr;
    }
    Emitter& e = pool.emitters[static_cast<std::size_t>(handle.slot)];
    return e.owner == handle.owner && e.active ? &e : nullptr;
}

bool effect_active(const ParticlePool& pool, EffectHandle handle) {
    return handle.slot >= 0 && handle.slot < emitter_count &&
           pool.emitters[static_cast<std::size_t>(handle.slot)].active;
}

// decomp: effect_set_position 0x64BCB
void effect_set_position(ParticlePool& pool, EffectHandle handle, const Vec3f& pos) {
    if (Emitter* e = effect_emitter(pool, handle)) {
        e->position = pos;
    }
}

// decomp: effect_set_prev_position 0x64C55
void effect_set_prev_position(ParticlePool& pool, EffectHandle handle, const Vec3f& pos) {
    if (Emitter* e = effect_emitter(pool, handle)) {
        e->prev_position = pos;
    }
}

// decomp: effect_set_matrix 0x64FD8
void effect_set_matrix(ParticlePool& pool, EffectHandle handle, const Mat4& matrix) {
    if (Emitter* e = effect_emitter(pool, handle)) {
        e->transform = matrix;
    }
}

void effect_set_matrix_spawned(ParticlePool& pool, const Mat4& matrix) {
    for (EffectHandle h : pool.spawned) {
        effect_set_matrix(pool, h, matrix);
    }
}

// decomp: effect_set_offset 0x6505C
void effect_set_offset(ParticlePool& pool, EffectHandle handle, const Vec3f& offset) {
    if (Emitter* e = effect_emitter(pool, handle)) {
        e->offset = offset;
    }
}

// decomp: effect_finish 0x650EC
void effect_finish(ParticlePool& pool, EffectHandle handle) {
    Emitter* e = effect_emitter(pool, handle);
    if (e && (e->run_flags & run_spawned)) {
        e->run_flags = (e->run_flags | run_finishing) & ~run_emitting;
    }
}

// decomp: effect_resume 0x65187
void effect_resume(ParticlePool& pool, EffectHandle handle) {
    Emitter* e = effect_emitter(pool, handle);
    if (e && (e->run_flags & run_spawned)) {
        e->run_flags = (e->run_flags & ~run_finishing) | run_emitting;
    }
}

// decomp: effect_stop 0x60013
void effect_stop(ParticlePool& pool, EffectHandle handle, bool any_owner) {
    // retail stops emitters[-1] (inside replay_inputs) for a handle of -1 (docs/bugs.md)
    if (handle.slot < 0 || handle.slot >= emitter_count) {
        return;
    }
    Emitter& e = pool.emitters[static_cast<std::size_t>(handle.slot)];
    if (any_owner || e.owner == handle.owner) {
        e.run_flags &= ~run_emitting;
        e.active = false;
    }
}

// decomp: effect_show 0x65467
void effect_show(ParticlePool& pool, int slot, bool shown) {
    if (slot < 0 || slot >= emitter_count) {
        return;
    }
    Emitter& e = pool.emitters[static_cast<std::size_t>(slot)];
    e.flags = shown ? e.flags & ~ef_hidden : e.flags | ef_hidden;
}
