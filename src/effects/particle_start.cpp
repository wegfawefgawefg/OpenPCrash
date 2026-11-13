// particle_start with its shapes (particle_shape_table), the launch
// (particle_launch_velocity) and hsv_to_rgb: decomp src/od/u_05fbd0.c.
#include "effects/particles.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.1415927f;

// the shape's launch axis: 0 radial (the caller's), 1 the spawn matrix's up, other world up
Vec3f shape_axis(int axis, const Mat4& m, const Vec3f& radial) {
    if (axis == 0) {
        return radial;
    }
    if (axis == 1) {
        return transform_coord({0, 1, 0}, m);
    }
    return {0, 1, 0};
}

// particle_shape_point and the flat ones: axis 0 and 1 both take m x (the incoming axis)
Vec3f flat_axis(int axis, const Mat4& m, const Vec3f& in) {
    return axis == 0 || axis == 1 ? transform_coord(in, m) : Vec3f{0, 1, 0};
}

// particle_shape_table[kind][shape]: a start position (in unit space through m, from the
// origin) and the launch axis `a` (it comes in as (0, 1, 0))
void particle_shape(Particle& p, Emitter& e, const ParticleRecord& r, const Mat4& m, Vec3f& a) {
    std::uint32_t& seed = e.seed;
    const int shape = r.shape;
    const int kind = r.shape_kind;
    if (shape == 5) {
        // particle_shape_dome: the first on the emitter, the rest a view-space offset
        // around it on a dome of the particle's size
        float tilt = rand_range(seed, 0.1f, 1.0f);
        const float roll = rand_range(seed, -pi, pi);
        const float length = rand_range(seed, 0, 0.5f) + 0.75f;
        const Vec3f v{0, 0, p.curve_scale.z * p.base_scale.z * length * -1.0f};
        tilt = std::asin(tilt);
        if (e.count == 1) {
            p.position = e.position;
            return;
        }
        p.position =
            transform_coord(transform_coord(v, mat_rotation_y(tilt)), mat_rotation_z(roll));
        return;
    }
    if (shape == 4 || kind == 1 || kind < 0 || kind > 5 || shape < 0 || shape > 5) {
        return; // a null entry: the position stays as the slot last had it
    }
    Vec3f local;
    Vec3f face{0, 1, 0};
    switch (kind) {
    case 0: // particle_shape_point
        local = {};
        break;
    case 2: // the unit square: corners, edges, area
        if (shape == 0) {
            local.x = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
            local.z = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
        } else if (shape == 1) {
            if (crt_rand(seed) % 2) {
                local.x = rand_range(seed, -0.5f, 0.5f);
                local.z = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
            } else {
                local.x = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
                local.z = rand_range(seed, -0.5f, 0.5f);
            }
        } else {
            local.x = rand_range(seed, -0.5f, 0.5f);
            local.z = rand_range(seed, -0.5f, 0.5f);
        }
        break;
    case 3: { // particle_shape_disc: the rim, or inside it
        const float theta = rand_range(seed, -pi, pi);
        const float ch = std::cos(theta) * 0.5f, sh = std::sin(theta) * 0.5f;
        float radius = std::sqrt(ch * ch + sh * sh);
        if (shape) {
            radius = rand_range(seed, 0, 1) * radius;
        }
        local = {std::cos(theta) * radius, 0, std::sin(theta) * radius};
        break;
    }
    case 4: // the unit cube: corners, edges, faces, volume
        if (shape == 0) {
            local.x = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
            local.y = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
            local.z = static_cast<float>(crt_rand(seed) % 2) - 0.5f;
        } else if (shape == 1) {
            const int edge = crt_rand(seed) % 12;
            const float sx = edge % 2 ? -0.5f : 0.5f;
            const float sy = edge % 4 < 2 ? 0.5f : -0.5f;
            const float run = rand_range(seed, -0.5f, 0.5f);
            if (edge < 4) {
                local = {sx, sy, run};
            } else if (edge < 8) {
                local = {sx, run, edge < 6 ? 0.5f : -0.5f};
            } else {
                local = {run, edge % 2 ? -0.5f : 0.5f, edge < 10 ? 0.5f : -0.5f};
            }
        } else if (shape == 2) {
            const int side = crt_rand(seed) % 6;
            const float sign = side % 2 ? -1.0f : 1.0f;
            const float u = rand_range(seed, -0.5f, 0.5f);
            const float w = rand_range(seed, -0.5f, 0.5f);
            if (side < 2) {
                local = {0.5f * sign, u, w};
                face = {sign, 0, 0};
            } else if (side < 4) {
                local = {u, 0.5f * sign, w};
                face = {0, sign, 0};
            } else {
                local = {u, w, 0.5f * sign};
                face = {0, 0, sign};
            }
        } else {
            local.x = rand_range(seed, -0.5f, 0.5f);
            local.y = rand_range(seed, -0.5f, 0.5f);
            local.z = rand_range(seed, -0.5f, 0.5f);
        }
        break;
    case 5: { // particle_shape_sphere: on it, or inside (denser at the poles)
        const float yaw = rand_range(seed, 0, pi);
        const float roll = rand_range(seed, -pi, pi);
        float x = rand_range(seed, 0, 0.5f);
        if (shape != 3) {
            x = 0.5f;
        }
        const Vec3f out = transform_coord(
            transform_coord({x, 0, 0}, mat_multiply(mat_rotation_z(roll), mat_rotation_y(yaw))), m);
        p.position = vec_add(out, p.origin);
        if (r.shape_axis == 0) {
            a = shape == 3 ? transform_coord(a, m) : out;
        } else if (r.shape_axis == 1) {
            a = transform_coord({0, 1, 0}, m);
        } else {
            a = {0, 1, 0};
        }
        return;
    }
    default:
        return;
    }
    p.position = vec_add(transform_coord(local, m), p.origin);
    if (kind == 4) {
        // the cube's axes: 0 radial (corners, edges) or the face normal, 1 m's up
        if (shape == 3) {
            a = r.shape_axis == 0 ? transform_coord(a, m) : Vec3f{0, 1, 0};
        } else if (shape == 2) {
            a = r.shape_axis == 0   ? transform_coord(face, m)
                : r.shape_axis == 1 ? transform_coord({0, 1, 0}, m)
                                    : Vec3f{0, 1, 0};
        } else {
            a = shape_axis(r.shape_axis, m, vec_sub(p.position, p.origin));
        }
        return;
    }
    a = flat_axis(r.shape_axis, m, a);
}

// the axis turned about a perpendicular by the tilt and about
// itself by rand(+-pi), times speed
// decomp: particle_launch_velocity 0x60555
Vec3f launch_velocity(std::uint32_t& seed, const Vec3f& axis, float spread_deg, float speed,
                      std::uint32_t flags) {
    Vec3f out{axis.x + 1, -axis.y + 1, axis.z - 1};
    if (vec_length(out) == 0) {
        out.x += 1;
    }
    out = vec_cross(axis, out);
    const float spread = spread_deg * 0.017453292f;
    const Mat4 about_axis = mat_rotation_axis(axis, rand_range(seed, -pi, pi));
    const Mat4 tilt =
        mat_rotation_axis(out, flags & ef_hollow ? spread : rand_range(seed, 0, spread));
    out = vec_normalize(transform_coord(axis, mat_multiply(tilt, about_axis)));
    if (flags & ef_random_yaw) {
        speed = rand_range(seed, 0.2f, 1.0f) * speed;
    }
    return vec_scale(out, speed);
}

// hsv_to_rgb's red output: all particle_start uses (as a brightness jitter)
// decomp: hsv_to_rgb 0x6477E
float hsv_red(int hue, int saturation, int value) {
    float h = static_cast<float>(static_cast<std::int16_t>(hue)) / 360.0f;
    float s = static_cast<float>(static_cast<std::uint8_t>(saturation)) / 100.0f;
    float v = static_cast<float>(static_cast<std::uint8_t>(value)) / 100.0f;
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    s = s < 0 ? 0 : s > 1 ? 1 : s;
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    s = 1 - s;
    float d = h > 0.5f ? h - 1 : h;
    float r = 2 - std::fabs(d) * 6;
    r = r < 0 ? 0 : r > 1 ? 1 : r;
    return ((1 - r) * s + r) * v;
}

} // namespace

// decomp: particle_start 0x60AFC
void particle_start(ParticlePool& pool, Emitter& e, Particle& p) {
    const ParticleRecord& r = pool.records[static_cast<std::size_t>(p.record)];
    std::uint32_t& seed = e.seed;

    // life in updates; the curves at their starts
    p.age = 0;
    p.life = static_cast<int>((rand_range(seed, -1, 1) * r.life_jitter + r.life) * frame_cadence);
    for (CurveCursor& c : p.colors) {
        c = {};
    }
    p.scale = p.force_b = p.force_a = p.mass = p.speed = p.speed_min = p.speed_max = {};
    p.curve_scale = {1, 1, 1};

    // the colour: a brightness jitter on r, g, b; the alpha bias truncated to an integer
    const float jitter_range = hsv_red(static_cast<int>(r.hsv_spread[0] * 360.0f),
                                       static_cast<int>(r.hsv_spread[1] * 100.0f),
                                       static_cast<int>(r.hsv_spread[2] * 100.0f));
    const float jitter = rand_range(seed, -jitter_range, jitter_range);
    const auto clamp01 = [](float x) { return x < 0 ? 0.0f : x > 1 ? 1.0f : x; };
    const std::uint32_t red =
        static_cast<std::uint8_t>(static_cast<int>(clamp01(r.color[0] + jitter) * 255.0f));
    const std::uint32_t green =
        static_cast<std::uint8_t>(static_cast<int>(clamp01(r.color[1] + jitter) * 255.0f));
    const std::uint32_t blue =
        static_cast<std::uint8_t>(static_cast<int>(clamp01(r.color[2] + jitter) * 255.0f));
    // the alpha bias truncated to an integer (kept), its sum clamped (retail's carry is lost)
    std::uint32_t alpha = static_cast<std::uint8_t>(static_cast<int>(r.color[3] * 255.0f)) +
                          static_cast<std::uint8_t>(static_cast<int>(r.alpha_bias));
    alpha = alpha > 255 ? 255 : alpha;
    p.color = alpha << 24 | red << 16 | green << 8 | blue;

    // size, speed, spread
    if (r.scale_curve.empty()) {
        const float s = rand_range(seed, -r.scale_jitter, r.scale_jitter) + r.scale;
        p.base_scale = {s, s, s};
    } else {
        p.base_scale = {1, 1, 1};
    }
    const float speed =
        (rand_range(seed, -r.speed_jitter, r.speed_jitter) + r.speed) / frame_cadence;
    const float spread = rand_range(seed, -r.spread_jitter, r.spread_jitter) + r.spread_deg;

    // the shape through the spawn matrix (scaled unless the scale is zero)
    Mat4 m = p.spawn_transform;
    if (p.spawn_scale.x != 0 || p.spawn_scale.y != 0 || p.spawn_scale.z != 0) {
        m = mat_multiply(mat_scaling(p.spawn_scale.x, p.spawn_scale.y, p.spawn_scale.z), m);
    }
    Vec3f a{0, 1, 0};
    particle_shape(p, e, r, m, a);
    if (e.flags & ef_shell) {
        const float yaw = rand_range(seed, 0, pi);
        const float roll = rand_range(seed, -pi, pi);
        const Vec3f out = transform_coord(
            transform_coord({r.burst_radius, 0, 0},
                            mat_multiply(mat_rotation_z(roll), mat_rotation_y(yaw))),
            m);
        p.position = vec_add(out, p.origin);
    }
    if (e.flags & ef_cluster) {
        const float yaw = rand_range(seed, 0, pi);
        const float roll = rand_range(seed, -pi, pi);
        const Vec3f out = transform_coord(
            transform_coord({r.burst_radius * 0.3f, 0, 0},
                            mat_multiply(mat_rotation_z(roll), mat_rotation_y(yaw))),
            m);
        p.position = vec_add(vec_add(e.offset, p.origin), out);
    }
    p.velocity = launch_velocity(seed, a, spread, speed, e.flags);
    if (e.flags & ef_ring) {
        const Vec3f along = vec_scale(a, vec_dot(a, p.velocity));
        p.position = vec_add(p.position,
                             vec_scale(vec_normalize(vec_sub(p.velocity, along)), r.burst_radius));
    }
    if (e.flags & ef_fill) {
        p.position = vec_add(p.position, vec_scale(e.velocity, rand_range(seed, 0, 1) * -1.0f));
    }

    // the billboard mode: a dome's parent and children, pulled toward the camera, or plain
    if (r.shape == 5) {
        p.billboard_mode = 2;
        if (e.count == 1) {
            p.life = life_hold;
            p.billboard_mode = 1;
        }
    } else {
        p.billboard_mode = e.flags & ef_toward ? 9 : 0;
    }

    // angle and spin; the paper's drift and countdown; the gust's strength
    p.angle = rand_range(seed, -pi, pi);
    if (e.flags & ef_spin) {
        p.spin = rand_range(seed, -0.62831855f, 0.62831855f);
    } else if (e.flags & ef_paper) {
        p.spin = rand_range(seed, -0.06283186f, 0.06283186f);
        p.wander.x = rand_range(seed, -1, 1) * 0.01f;
        p.wander.z = rand_range(seed, -1, 1) * 0.01f;
        p.wander.y = static_cast<float>(static_cast<int>(rand_range(seed, 26, 45))) * 1.01f;
    } else if (e.flags & ef_gust) {
        float g1 = static_cast<float>(e.age) * pi * 0.08f + p.wander.y;
        float g2 = static_cast<float>(e.age) * pi * 0.0577f + p.wander.y * 3.2f;
        while (g1 > pi) {
            g1 -= pi;
        }
        while (g2 > pi) {
            g2 -= pi;
        }
        p.wander.x = std::sin(g1) * std::sin(g2);
        // retail leaves the spin the slot last had (docs/bugs.md): none
        p.spin = 0;
    } else {
        p.spin = 0;
    }

    // the chain (the arcs): along the emitter matrix's row 1 from row 0, copies 2..9 jittered on
    // rows 2 and 3, each with its first update at once
    if (e.flags & ef_chain) {
        const Mat4& t = e.transform;
        const Vec3f start{t.m[0][0], t.m[0][1], t.m[0][2]};
        const Vec3f step = vec_scale({t.m[1][0], t.m[1][1], t.m[1][2]}, 0.1f);
        const Vec3f side_a = vec_scale({t.m[2][0], t.m[2][1], t.m[2][2]}, 0.1f * r.burst_radius);
        const Vec3f side_b = vec_scale({t.m[3][0], t.m[3][1], t.m[3][2]}, 0.1f * r.burst_radius);
        Vec3f jitter{};
        p.velocity = step;
        p.position = vec_add(start, step);
        for (int i = 2; i < 10; ++i) {
            if (e.count >= 0x6F) {
                continue;
            }
            Particle& copy = e.particles[e.next_particle];
            e.next_particle = next_free_particle(e);
            e.count += 1;
            copy = p;
            copy.velocity = vec_add(step, jitter);
            const float ja = rand_range(seed, -1, 1);
            jitter = vec_add(vec_scale(side_a, ja), vec_scale(side_b, rand_range(seed, -1, 1)));
            copy.position = vec_add(vec_add(start, vec_scale(step, static_cast<float>(i))), jitter);
            copy.angle = rand_range(seed, -pi, pi);
            particle_update(pool, e, copy);
        }
    }
    particle_update(pool, e, p);
}
