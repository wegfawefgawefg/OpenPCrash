// fx_accuracy: src/fx against double precision on a few thousand fixed samples, printing the
// worst error of each function (docs/numbers.md quotes them). A tool to look at, not a test: it
// checks nothing and always exits 0. Floats and libm are fine here; this is not the world.
#include "fx/angle.hpp"
#include "fx/fx.hpp"
#include "fx/hit.hpp"
#include "fx/mat.hpp"
#include "fx/vec.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr double pi = 3.14159265358979323846;

// the original's constants, converted at compile time next to their values
static_assert(fx_f(9.8 / 900 * 2.5).raw == 1784);        // gravity a 30 Hz update, 0.027222
static_assert(fx_f(9.8 / 900 * 2.5 / 4).raw == 446);     // a 60 Hz step: accelerations quarter
static_assert(fx_f(0.985f) == fx_bits(0x3F7C28F6));      // air drag, literal and bits agree
static_assert(fx_bits(0x3E490FDB) == fx_f(0.19634955f)); // pi/16, the homing turn clamp
static_assert(fx_bits(0xC47A0000) == fx(-1000));         // -0x3B860000, the floor default
static_assert(angle_f_degrees(90) == angle_quarter);
static_assert(angle_degrees(angle_f_degrees(-45)) == fx(-45));

std::uint64_t rng_state = 0x5EED5EED12345678ull;
std::uint64_t next_random() {
    std::uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
double uniform(double low, double high) {
    return low + (high - low) * static_cast<double>(next_random() >> 11) / 9007199254740992.0;
}
std::int32_t random_raw(int max_bits) {
    const int bits = 1 + static_cast<int>(next_random() % static_cast<std::uint64_t>(max_bits));
    const std::int64_t value =
        static_cast<std::int64_t>(next_random() % (std::uint64_t{1} << bits));
    return static_cast<std::int32_t>(next_random() & 1 ? -value : value);
}

double d(Fx v) {
    return v.raw / 65536.0;
}
double radians_of(Angle a) {
    return angle_signed(a) * (2 * pi / 4294967296.0);
}
Fx fx_of(double v) {
    return fx_raw(static_cast<std::int32_t>(std::llround(v * 65536.0)));
}
FxVec3 vec_of(double x, double y, double z) {
    return {fx_of(x), fx_of(y), fx_of(z)};
}
// one draw a statement: the order a call's arguments are worked out in is the compiler's
FxVec3 random_point(double range) {
    const double x = uniform(-range, range);
    const double y = uniform(-range, range);
    const double z = uniform(-range, range);
    return vec_of(x, y, z);
}

struct D3 {
    double x, y, z;
};
D3 d3(FxVec3 v) {
    return {d(v.x), d(v.y), d(v.z)};
}
D3 sub(D3 a, D3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double ddot(D3 a, D3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
D3 dcross(D3 a, D3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dlen(D3 a) {
    return std::sqrt(ddot(a, a));
}
double vec_error(FxVec3 v, D3 want) {
    return std::fmax(std::fabs(d(v.x) - want.x),
                     std::fmax(std::fabs(d(v.y) - want.y), std::fabs(d(v.z) - want.z)));
}

struct Worst {
    const char* name;
    const char* unit;
    double scale;
    double worst = 0;
    void see(double error) {
        worst = std::fmax(worst, std::fabs(error));
    }
    void print() const {
        std::printf("%-44s %12.4g %s\n", name, worst * scale, unit);
    }
};

void scalars() {
    Worst bits{"fx_bits vs exact rounding", "raw", 65536};
    for (int i = 0; i < 600; ++i) {
        const float value = static_cast<float>(
            uniform(-1, 1) * std::ldexp(1.0, static_cast<int>(next_random() % 30) - 14));
        bits.see(d(fx_bits(std::bit_cast<std::uint32_t>(value))) -
                 std::llround(static_cast<double>(value) * 65536.0) / 65536.0);
    }
    bits.print();
    Worst sqrt_error{"fx_sqrt", "raw", 65536};
    for (int i = 0; i < 1000; ++i) {
        const Fx value = fx_raw(random_raw(31) & 0x7FFFFFFF);
        sqrt_error.see(d(fx_sqrt(value)) - std::sqrt(d(value)));
    }
    sqrt_error.print();
}

void trig() {
    Worst sine{"fx_sin, fx_cos", "raw", 65536};
    for (int i = 0; i < 4000; ++i) {
        const Angle a{static_cast<std::uint32_t>(i < 64 ? i * (1u << 26) : next_random())};
        sine.see(d(fx_sin(a)) - std::sin(radians_of(a)));
        sine.see(d(fx_cos(a)) - std::cos(radians_of(a)));
    }
    sine.print();
    Worst arctan{"angle_atan2 (inputs 1 raw .. 32768)", "rad", 1};
    for (int i = 0; i < 4000; ++i) {
        const Fx y = fx_raw(random_raw(31)), x = fx_raw(random_raw(31));
        double error = radians_of(angle_atan2(y, x)) -
                       std::atan2(static_cast<double>(y.raw), static_cast<double>(x.raw));
        error = std::remainder(error, 2 * pi);
        arctan.see(error);
    }
    arctan.print();
    Worst inverse{"angle_acos, angle_asin", "rad", 1};
    for (int i = 0; i < 2000; ++i) {
        const Fx c = fx_raw(static_cast<std::int32_t>(next_random() % 131073) - 65536);
        inverse.see(radians_of(angle_acos(c)) - std::acos(d(c)));
        inverse.see(radians_of(angle_asin(c)) - std::asin(d(c)));
    }
    inverse.print();
    Worst convert{"angle_from_radians (|r| < 16), angle_radians", "raw", 65536};
    Worst units{"angle_from_degrees (|d| < 1024), in units", "units", 1};
    for (int i = 0; i < 2000; ++i) {
        const Fx radians = fx_raw(random_raw(20));
        const double want = d(radians) / (2 * pi);
        const double turns = radians_of(angle_from_radians(radians)) / (2 * pi);
        convert.see(std::remainder(turns - want, 1.0) * 2 * pi);
        const Angle a{static_cast<std::uint32_t>(next_random())};
        convert.see(d(angle_radians(a)) - radians_of(a));
        const Fx degrees = fx_raw(random_raw(26));
        const double got = angle_signed(angle_from_degrees(degrees));
        units.see(std::remainder(got - d(degrees) / 360.0 * 4294967296.0, 4294967296.0));
    }
    convert.print();
    units.print();
}

void vectors() {
    Worst lengths{"length (parts to 600 units)", "raw", 65536};
    Worst units{"normalized (parts 1 raw .. 600 units)", "raw", 65536};
    Worst unit_size{"|normalized(v)| - 1", "raw", 65536};
    for (int i = 0; i < 3000; ++i) {
        const int bits = 1 + i % 25;
        const FxVec3 v{fx_raw(random_raw(bits)), fx_raw(random_raw(bits)),
                       fx_raw(random_raw(bits))};
        const D3 dv = d3(v);
        const double size = dlen(dv);
        lengths.see(d(length(v)) - size);
        if (size == 0) {
            continue;
        }
        const FxVec3 n = normalized(v);
        units.see(vec_error(n, {dv.x / size, dv.y / size, dv.z / size}));
        unit_size.see(dlen(d3(n)) - 1);
    }
    lengths.print();
    units.print();
    unit_size.print();
    // angles between aim directions, many of them small (a fighter near the view cone's axis)
    Worst between{"angle_between (0 .. pi, many under 0.01)", "rad", 1};
    for (int i = 0; i < 3000; ++i) {
        const FxVec3 a = random_point(100);
        const FxVec3 offset = random_point(i % 2 ? 0.5 : 100);
        const FxVec3 b = a + offset;
        const D3 da = d3(a), db = d3(b);
        between.see(radians_of(angle_between(a, b)) -
                    std::atan2(dlen(dcross(da, db)), ddot(da, db)));
    }
    between.print();
}

void matrices() {
    Worst rotate{"rotation_y * point (300 units)", "raw", 65536};
    Worst inverse{"rigid inverse round trip (300 units)", "raw", 65536};
    Worst look{"look_at_lh view of a point (300 units)", "raw", 65536};
    for (int i = 0; i < 1000; ++i) {
        const Angle a{static_cast<std::uint32_t>(next_random())};
        const FxVec3 p = random_point(300);
        const D3 dp = d3(p);
        const double s = std::sin(radians_of(a)), c = std::cos(radians_of(a));
        rotate.see(vec_error(transform(p, mat3_rotation_y(a)),
                             {dp.x * c + dp.z * s, dp.y, -dp.x * s + dp.z * c}));
        const FxMat3 turn = mat3_mul(
            mat3_rotation_x(Angle{static_cast<std::uint32_t>(next_random())}), mat3_rotation_y(a));
        const FxMat4 place = mat4_from(turn, random_point(250));
        inverse.see(
            vec_error(transform_coord(transform_coord(p, place), mat4_rigid_inverse(place)), dp));
        const FxVec3 eye = random_point(250), at = random_point(250);
        const FxMat4 view = mat4_look_at_lh(eye, at, fx_vec3(0, 1, 0));
        const D3 z0 = sub(d3(at), d3(eye));
        const D3 z{z0.x / dlen(z0), z0.y / dlen(z0), z0.z / dlen(z0)};
        const D3 x0 = dcross({0, 1, 0}, z);
        const D3 x{x0.x / dlen(x0), x0.y / dlen(x0), x0.z / dlen(x0)};
        const D3 y = dcross(z, x);
        const D3 rel = sub(dp, d3(eye));
        look.see(vec_error(transform_coord(p, view), {ddot(rel, x), ddot(rel, y), ddot(rel, z)}));
    }
    rotate.print();
    inverse.print();
    look.print();
    // 10 minutes of 60 Hz heading steps of 0.1 deg/tick x stick 0.37: the step as Fx degrees,
    // as an angle scaled by the stick, and a matrix turned step by step (squared up every step)
    const Angle step_fx = angle_from_degrees(fx_mul_near(fx_f(0.1), fx_f(0.37)));
    const Angle step = angle_scaled(angle_f_degrees(0.1), fx_f(0.37));
    Angle heading_fx{}, heading{};
    FxMat3 turned = mat3_identity();
    for (int i = 0; i < 36000; ++i) {
        heading_fx += step_fx;
        heading += step;
        turned = mat3_axes_normalized(mat3_mul(turned, mat3_rotation_y(step)));
    }
    const double want = 36000 * 0.1 * 0.37 * pi / 180;
    const D3 forward = d3(transform({Fx{}, Fx{}, fx(-1)}, turned));
    std::printf("%-44s %12.4g rad\n", "heading after 36000 steps, step in Fx deg",
                std::remainder(radians_of(heading_fx) - want, 2 * pi));
    std::printf("%-44s %12.4g rad\n", "  step as angle_scaled(turn, stick)",
                std::remainder(radians_of(heading) - want, 2 * pi));
    std::printf("%-44s %12.4g rad\n", "  a matrix multiplied by each step",
                std::remainder(std::atan2(-forward.x, -forward.z) - want, 2 * pi));
}

void collision() {
    Worst height{"tri_frame height", "raw", 65536};
    Worst coords{"tri_frame u, w as a point on the plane", "raw", 65536};
    Worst crossing{"segment_tri_hit point", "raw", 65536};
    Worst entry{"segment_sphere_entry point", "raw", 65536};
    Worst floor{"tri_height_at", "raw", 65536};
    int hits = 0, entries = 0;
    for (int i = 0; i < 3000; ++i) {
        const FxVec3 v0 = random_point(250);
        const FxVec3 v1 = v0 + random_point(i % 3 == 0 ? 2 : 150);
        const FxVec3 v2 = v0 + random_point(150);
        // the normal as the file gives it, a float worked out apart (cross() of stage-sized
        // edges is past what Fx holds)
        const D3 e0 = sub(d3(v2), d3(v0)), e1 = sub(d3(v1), d3(v0));
        const D3 n0 = dcross(e1, e0);
        const double n0_size = dlen(n0);
        if (n0_size < 1e-6) {
            continue;
        }
        // against the exact normal: the Fx one's rounding is part of the error
        const D3 n{n0.x / n0_size, n0.y / n0_size, n0.z / n0_size};
        const FxTri tri{v0, v1, v2, vec_of(n.x, n.y, n.z)};
        const FxVec3 p = v0 + random_point(40);
        const D3 dp = sub(d3(p), d3(v0));
        // the original's inverse frame by Cramer's rule, rows e0, n, e1
        const double det = ddot(e0, dcross(n, e1));
        if (std::fabs(det) < 1e-3) {
            continue;
        }
        const FxTriFrame frame = tri_frame(tri, p);
        const double u_error = d(frame_u(frame)) - ddot(dp, dcross(n, e1)) / det;
        const double w_error = d(frame_w(frame)) - ddot(e0, dcross(n, dp)) / det;
        coords.see(dlen({u_error * e0.x + w_error * e1.x, u_error * e0.y + w_error * e1.y,
                         u_error * e0.z + w_error * e1.z}));
        height.see(d(frame.height) - ddot(e0, dcross(dp, e1)) / det);
        // a segment through the plane
        const FxVec3 to = p - tri.normal * fx(60);
        FxSegmentHit hit{};
        if (segment_tri_hit(tri, p, to, true, Fx{}, &hit)) {
            const D3 a = d3(p), b = d3(to);
            const double ha = ddot(n, sub(a, d3(v0))), hb = ddot(n, sub(b, d3(v0)));
            const double t = ha / (ha - hb);
            crossing.see(vec_error(
                hit.point, {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}));
            ++hits;
        }
        // a shot past a fighter's sphere
        const FxVec3 centre = p + random_point(3);
        const Fx radius = fx_of(uniform(0.3, 2.5));
        FxVec3 at{};
        if (segment_sphere_entry(p, to, centre, radius, &at)) {
            const D3 a = d3(p), b = d3(to), c = d3(centre);
            const D3 path = sub(b, a), m = sub(c, a);
            const double size = dlen(path), along = ddot(m, path) / size;
            const double off = ddot(m, m) - along * along, r = d(radius);
            const double chord = std::sqrt(std::fmax(0.0, r * r - off));
            const double s = along - chord >= 0 ? along - chord : along + chord;
            entry.see(vec_error(
                at, {a.x + path.x / size * s, a.y + path.y / size * s, a.z + path.z / size * s}));
            ++entries;
        }
        Fx floor_height{};
        if (tri.normal.y > fx_f(0.2) && tri_height_at(tri, p.x, p.z, &floor_height)) {
            floor.see(d(floor_height) -
                      (d(v0.y) - ((d(p.x) - d(v0.x)) * n.x + (d(p.z) - d(v0.z)) * n.z) / n.y));
        }
    }
    height.print();
    coords.print();
    crossing.print();
    entry.print();
    floor.print();
    std::printf("  (%d segment hits, %d sphere entries)\n", hits, entries);
}

void integration() {
    // a jump at 60 Hz: gravity 9.8/900 x 2.5 a 30 Hz update, a quarter a 60 Hz step, from 0.3
    // units a 60 Hz step up; the peak and the landing step against doubles
    const Fx gravity = fx_f(9.8 / 900 * 2.5 / 4);
    Fx vy = fx_f(0.3), y{};
    double dvy = d(vy), dy = 0, peak = 0, dpeak = 0;
    int steps = 0;
    while (y >= Fx{} && steps < 10000) {
        vy -= gravity;
        y += vy;
        dvy -= 9.8 / 900 * 2.5 / 4;
        dy += dvy;
        peak = std::fmax(peak, d(y));
        dpeak = std::fmax(dpeak, dy);
        ++steps;
    }
    std::printf("%-44s %12.4g units (%d steps)\n", "jump peak, fixed minus double", peak - dpeak,
                steps);
    // a knockback of -1 raw decayed by 0.8 an update: floored it never reaches 0
    const Fx decay = fx_f(0.8);
    Fx floored = fx_raw(-1000), toward_zero = fx_raw(-1000);
    int floored_steps = 0, zero_steps = 0;
    for (int i = 0; i < 200; ++i) {
        floored = floored * decay;
        toward_zero = fx_mul_trunc(toward_zero, decay);
        floored_steps += floored.raw != 0;
        zero_steps += toward_zero.raw != 0;
    }
    std::printf("%-44s %12d raw (non-zero %d of 200 steps)\n", "-1000 raw x 0.8 floored (gfxp *)",
                floored.raw, floored_steps);
    std::printf("%-44s %12d raw (non-zero %d of 200 steps)\n", "-1000 raw x 0.8 toward zero",
                toward_zero.raw, zero_steps);
}

// every function once on integer-made inputs, the raw results hashed: the same number from every
// compiler, standard library and instruction set is the determinism check (docs/numbers.md)
std::uint64_t fingerprint_hash = 0xCBF29CE484222325ull;
void mix(std::int64_t value) {
    fingerprint_hash = (fingerprint_hash ^ static_cast<std::uint64_t>(value)) * 0x100000001B3ull;
}
template <typename... Values> void mix_all(Values... values) {
    (mix(static_cast<std::int64_t>(values)), ...);
}
void mix(FxVec3 v) {
    mix(v.x.raw);
    mix(v.y.raw);
    mix(v.z.raw);
}
FxVec3 random_raw_point(int bits) {
    return {fx_raw(random_raw(bits)), fx_raw(random_raw(bits)), fx_raw(random_raw(bits))};
}
void fingerprint() {
    rng_state = 0x0123456789ABCDEFull;
    for (int i = 0; i < 20000; ++i) {
        const Fx a = fx_raw(random_raw(31)), b = fx_raw(random_raw(24));
        const Angle angle{static_cast<std::uint32_t>(next_random())};
        mix_all(fx_mul_near(a, b).raw, fx_mul_trunc(a, b).raw, fx_div_near(a, b).raw,
                fx_mul_div(a, b, b + fx(1)).raw);
        mix_all(fx_sqrt(a).raw, fx_bits(static_cast<std::uint32_t>(next_random())).raw,
                fx_pow_int(fx_raw(b.raw >> 6), 10).raw);
        mix_all(fx_sin(angle).raw, fx_cos(angle).raw, angle_radians(angle).raw,
                angle_degrees(angle).raw);
        mix_all(angle_atan2(a, b).raw, angle_acos(fx_raw(b.raw >> 8)).raw,
                angle_from_radians(b).raw, angle_from_degrees(b).raw);
        mix_all(angle_scaled(angle, b).raw, angle_clamp(angle, angle_f_degrees(30)).raw);
        const FxVec3 p = random_raw_point(25), q = random_raw_point(25), r = random_raw_point(20);
        mix_all(dot_q32(p, q), length(p).raw, dot(r, r).raw);
        mix(normalized(p));
        mix(cross(r, r + p));
        mix(clamp_length(r, fx(5)));
        mix(angle_between(p, r).raw);
        const FxMat4 place = mat4_from(mat3_rotation_axis(normalized(r), angle), q);
        mix(transform_coord(p, place));
        mix(transform_coord(p, mat4_rigid_inverse(place)));
        mix(transform_coord(p, mat4_look_at_lh(q, p, fx_vec3(0, 1, 0))));
        mix(mat3_from_dir_up(r, fx_vec3(0, 1, 0)).x);
        const FxTri tri{q, q + random_raw_point(23), q + random_raw_point(23),
                        normalized(random_raw_point(20))};
        const FxTriFrame frame = tri_frame(tri, p);
        mix_all(frame.height.raw, frame.u_num, frame.w_num, frame.den, frame_u(frame).raw,
                frame_inside(frame));
        FxSegmentHit hit{};
        if (segment_tri_hit(tri, p, q - r, i & 1, fx_f(0.05), &hit)) {
            mix(hit.point);
            mix(hit.along.raw);
        }
        FxVec3 entry{};
        const FxVec3 centre = p + random_raw_point(19);
        const Fx radius = fx_raw(random_raw(18) & 0x3FFFF);
        if (segment_sphere_entry(p, p + r, centre, radius, &entry)) {
            mix(entry);
        }
        Fx height{};
        mix(tri_height_at(tri, p.x, p.z, &height) ? height.raw : -1);
        mix(segment_param(p, q, r).raw);
        mix(closest_on_segment(p, q, r));
        mix(grid_cell(p.x, q.x, fx(7), 32));
    }
    std::printf("\nfingerprint of 20000 rounds of every function: %016llx\n",
                static_cast<unsigned long long>(fingerprint_hash));
}

} // namespace

int main() {
    std::printf("src/fx against double precision (worst error over the samples)\n\n");
    scalars();
    trig();
    vectors();
    matrices();
    collision();
    integration();
    fingerprint();
    return 0;
}
