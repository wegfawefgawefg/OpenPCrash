// particle_update, particle_step, particle_motion_forces and particle_draw
// (decomp src/od/u_05fbd0.c). Everything here is per 30 Hz update as retail tunes it:
// curve times, drag, spin, the gust and the paper.
#include "effects/particles.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.1415927f;

Mat4 turn_xyz(const float degrees[3]) {
    const float k = pi / 180.0f;
    return mat_multiply(
        mat_rotation_x(degrees[0] * k),
        mat_multiply(mat_rotation_y(degrees[1] * k), mat_rotation_z(degrees[2] * k)));
}

// gravity, force B, force A, the drag, the paper
// decomp: particle_motion_forces 0x63B96
void motion_forces(ParticlePool& pool, Emitter& e, Particle& p, const ParticleRecord& r) {
    std::uint32_t& seed = e.seed;
    const Vec3f at = p.position;
    float t = curve_time(p, r.mass_time);
    float mass = curve_value(r.mass_curve, p.mass, t, r.mass);
    Vec3f accel = e.flags & ef_paper ? vec_scale(p.gravity, 3) : p.gravity;
    const float per_update2 = frame_cadence * frame_cadence;
    if (!r.force_b_off) {
        const float jitter = curve_value(r.force_b_curve, p.force_b, t, r.forces[2]);
        float roll = rand_range(seed, -1, 1) * jitter + r.forces[0];
        roll = roll < 0 ? 0 : roll;
        const float strength = r.force_b * roll;
        Vec3f force{1, 0, 0};
        if (r.force_b_radial) {
            const Vec3f d = vec_sub(at, e.offset);
            force = vec_length(d) < r.force_b_range
                        ? vec_scale(vec_normalize(d), strength / (mass * per_update2))
                        : Vec3f{};
        } else {
            force = vec_scale(transform_coord(force, turn_xyz(r.force_b_rot)),
                              strength / (mass * per_update2));
        }
        accel = vec_add(accel, force);
    }
    if (!r.force_a_off) {
        const float jitter = curve_value(r.force_a_curve, p.force_a, t, r.forces[5]);
        float roll = rand_range(seed, -1, 1) * jitter + r.forces[3];
        roll = roll < 0 ? 0 : roll;
        const float strength = r.force_a * roll;
        Vec3f force{1, 0, 0};
        if (e.flags & ef_gust) {
            force = vec_scale(transform_coord(force, mat_rotation_y(p.wander.y)),
                              p.wander.x * strength / (mass * per_update2));
        } else {
            const float k = pi / 180.0f;
            float yaw = r.force_a_rot[1] * k;
            if (e.flags & ef_random_yaw) {
                yaw = rand_float_crt(seed, 6.2831855f) - pi;
            }
            const Mat4 turn = mat_multiply(
                mat_rotation_x(r.force_a_rot[0] * k),
                mat_multiply(mat_rotation_y(yaw), mat_rotation_z(r.force_a_rot[2] * k)));
            force = vec_scale(transform_coord(force, turn), strength / (mass * per_update2));
        }
        accel = vec_add(accel, force);
    }
    p.velocity = vec_add(p.velocity, accel);

    // the drag: force A's numbers over the mass, rolled every update, not scaled by the rate
    if (mass <= 0) {
        mass = 0.0001f;
    }
    float damp = (rand_range(seed, -1, 1) * r.forces[5] + r.forces[3]) / mass / 100.0f;
    damp = damp < 0 ? 0 : damp > 1 ? 1 : damp;
    p.velocity = vec_scale(p.velocity, 1 - damp * 0.5f);

    // the paper: drift above the flutter height, a sway below it
    if (e.flags & ef_paper) {
        const float flutter = pool.paper_high ? 9.0f : 6.0257f;
        p.wander.y -= 1;
        if (p.wander.y <= 0) {
            p.spin = rand_range(seed, -0.06283186f, 0.06283186f);
            const float drift = p.position.y > flutter ? 0.01f : 0.07f;
            p.wander.x = rand_range(seed, -1, 1) * drift;
            p.wander.z = rand_range(seed, -1, 1) * drift;
            p.wander.y = static_cast<float>(static_cast<int>(rand_range(seed, 26, 45))) * 1.01f;
            if (p.position.y <= flutter) {
                p.velocity = vec_scale(p.velocity, 0.1f);
            }
            return;
        }
        if (p.position.y > flutter) {
            p.velocity = vec_add(p.velocity, {p.wander.x, 0, p.wander.z});
            return;
        }
        // the countdown's roll is kept in its fraction (n x 1.01)
        const float wobble =
            (p.wander.y - static_cast<float>(static_cast<unsigned>(static_cast<int>(p.wander.y)))) *
            100.0f;
        const float sway = std::sin((p.wander.y + 1) * pi / wobble);
        p.velocity.x = sway * p.wander.x;
        p.velocity.z = sway * p.wander.z;
        const float c = std::cos(p.wander.y * pi / wobble);
        p.velocity.y = (1.4f - c * c) * (p.gravity.y * 15.0f) * 1.1f;
    }
}

// decomp: particle_step 0x61CB5
void particle_step(ParticlePool& pool, Emitter& e, Particle& p) {
    const ParticleRecord& r = pool.records[static_cast<std::size_t>(p.record)];
    // the speed held between its limits (a second, so over the rate)
    const float speed = vec_length(p.velocity);
    float target = curve_value(r.speed_curve, p.speed, curve_time(p, 0), speed);
    const float low =
        curve_value(r.speed_min_curve, p.speed_min, curve_time(p, r.speed_min_time), r.speed_min);
    const float high =
        curve_value(r.speed_max_curve, p.speed_max, curve_time(p, r.speed_max_time), r.speed_max);
    if (low / frame_cadence > target) {
        target = low / frame_cadence;
    }
    if (high / frame_cadence < target) {
        target = high / frame_cadence;
    }
    p.velocity = vec_scale(p.velocity, speed == 0 ? 0 : target / speed);

    // the move: forces, then the velocity, then the emitter's (from the second update on)
    if (!(e.flags & ef_chain)) {
        if ((e.flags & ef_paper) && p.position.y <= 0.05f) {
            p.position.y = 0.05f;
            p.spin = 0;
        } else {
            motion_forces(pool, e, p, r);
            p.position = vec_add(p.position, p.velocity);
            if (p.age && !(e.flags & ef_world)) {
                p.position = vec_add(p.position, e.velocity);
            }
        }
    }

    // the colour bytes from their curves, in integer steps
    for (int i = 0; i < 4; ++i) {
        const std::vector<IntKey>& keys = r.color_curves[i];
        if (keys.empty()) {
            continue;
        }
        const float t = curve_time(p, i == 3 ? r.alpha_time : r.color_time);
        CurveCursor& c = p.colors[i];
        while (t > keys[c.next].time && keys[c.next].time >= 0 && c.next + 1u < keys.size()) {
            c.prev = c.next;
            c.next += 1;
        }
        const IntKey& prev = keys[c.prev];
        const IntKey& next = keys[c.next];
        const int from = static_cast<std::uint8_t>(prev.value);
        std::int16_t channel = static_cast<std::int16_t>(from);
        if (next.time >= 0 && next.time != prev.time) {
            const float slope = (static_cast<float>(static_cast<std::uint8_t>(next.value)) -
                                 static_cast<float>(from)) /
                                (next.time - prev.time);
            channel = static_cast<std::int16_t>(
                from + static_cast<std::int16_t>(static_cast<int>((t - prev.time) * slope)));
        }
        if (channel < 0) {
            channel = 0;
        }
        const int shift = i * 8;
        p.color = (p.color & ~(0xFFu << shift)) |
                  static_cast<std::uint32_t>(static_cast<std::uint8_t>(channel)) << shift;
    }
    if (!r.scale_curve.empty()) {
        const float s = curve_value(r.scale_curve, p.scale, curve_time(p, r.scale_time), 1);
        p.curve_scale = {s, s, s};
    }

    // the turn, kept within +-pi by whole turns (retail subtracts pi, so a sprite crossing it
    // snapped half a turn: docs/bugs.md)
    p.angle += p.spin;
    while (p.angle < -pi || p.angle > pi) {
        p.angle += p.angle < -pi ? 2 * pi : -2 * pi;
    }
}

// this update's billboards, with the position before the step
// decomp: particle_draw 0x62564
void particle_draw(ParticlePool& pool, const Emitter& e, const Particle& p) {
    if (e.flags & ef_hidden) {
        return;
    }
    const ParticleRecord& r = pool.records[static_cast<std::size_t>(p.record)];
    BillboardPush push;
    push.rgba[0] = static_cast<float>(p.color >> 16 & 0xFF) / 255.0f;
    push.rgba[1] = static_cast<float>(p.color >> 8 & 0xFF) / 255.0f;
    push.rgba[2] = static_cast<float>(p.color & 0xFF) / 255.0f;
    push.rgba[3] = static_cast<float>(p.color >> 24 & 0xFF) / 255.0f;
    push.size = p.curve_scale.x * p.base_scale.x * 0.5f;
    push.cell = r.texture;
    push.additive = static_cast<std::uint8_t>(r.draw_flag) != 0;
    push.angle = p.angle;
    push.position = p.position;
    // a dome child's position is an offset from its parent: nothing to interpolate
    push.before = p.billboard_mode == 2 ? p.position : p.before;
    // streaks: the head, then a tail one velocity (a fifth of one for 0x400) back
    if (e.flags & (ef_streak15 | ef_streak3 | ef_short_streak)) {
        const bool fifteen = e.flags & ef_streak15;
        const bool short_tail = !fifteen && !(e.flags & ef_streak3);
        const Vec3f back = vec_scale(p.velocity, short_tail ? 0.2f : 1.0f);
        push.mode = fifteen ? 3 : 5;
        pool.pushes.push_back(push);
        push.position = vec_sub(push.position, back);
        push.before = vec_sub(push.before, back);
        push.mode += 1;
        pool.pushes.push_back(push);
        return;
    }
    if (e.flags & ef_paper) {
        // GameState +0x10EFC set or +0x10F04 clear draws the paper smaller
        push.size *= pool.paper_near || !pool.paper_high ? 0.6f : 1.7f;
    }
    push.mode = p.billboard_mode;
    pool.pushes.push_back(push);
}

} // namespace

// decomp: particle_update 0x61B0F
void particle_update(ParticlePool& pool, Emitter& e, Particle& p) {
    p.before = p.position;
    particle_step(pool, e, p);
    particle_draw(pool, e, p);
    p.emitter_age += 1;
    p.age += 1;
    // past its life, or a dome's parent left alone with no children (its held life steps once)
    if (p.age > p.life || (p.billboard_mode == 1 && e.count == 1)) {
        if (p.life == life_hold) {
            p.life = 0x7FFFFFFF; // life_hold - 1
        } else {
            p.active = false;
            e.count -= 1;
        }
    }
    if ((e.flags & ef_paper) && p.active && p.position.y < -3.0f) {
        p.active = false;
        e.count -= 1;
    }
    if ((e.flags & ef_sink) && p.active) {
        const Vec3f d = vec_sub(p.position, e.offset);
        if (2.0f > vec_dot(d, d)) {
            p.active = false;
            e.count -= 1;
        }
    }
}
