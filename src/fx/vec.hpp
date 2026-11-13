#pragma once
// Fixed-point vectors. Products accumulate whole in 64 bits and round once, to nearest. A stage is
// up to 530 units across, so a squared length or a dot product of two stage-sized vectors is past
// what Fx holds (181 units squared is 32768): the _q32 forms keep those whole in 64 bits, and
// length, normalized and the comparisons go through them (docs/numbers.md).
#include "fx/angle.hpp"
#include "fx/fx.hpp"

#include <bit>
#include <cstdint>

// the floor plane (x, z), as the original's 2D floor and point-in-triangle tests use it
struct FxVec2 {
    Fx x, z;
    friend constexpr bool operator==(FxVec2, FxVec2) = default;
};
struct FxVec3 {
    Fx x, y, z;
    friend constexpr bool operator==(FxVec3, FxVec3) = default;
};

constexpr FxVec3 fx_vec3(int x, int y, int z) {
    return {fx(x), fx(y), fx(z)};
}

constexpr FxVec3 operator+(FxVec3 a, FxVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr FxVec3 operator-(FxVec3 a, FxVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr FxVec3 operator-(FxVec3 a) {
    return {-a.x, -a.y, -a.z};
}
// scaled, each part rounded to nearest (sign-symmetric)
constexpr FxVec3 operator*(FxVec3 a, Fx s) {
    return {fx_mul_near(a.x, s), fx_mul_near(a.y, s), fx_mul_near(a.z, s)};
}
constexpr FxVec3 operator*(Fx s, FxVec3 a) {
    return a * s;
}
constexpr FxVec3& operator+=(FxVec3& a, FxVec3 b) {
    a = a + b;
    return a;
}
constexpr FxVec3& operator-=(FxVec3& a, FxVec3 b) {
    a = a - b;
    return a;
}
// scaled toward zero: for decays (knockback 0.8, drag 0.985, dodge 0.99 an update), which then
// reach zero from either side instead of sticking a few raw units below it
constexpr FxVec3 scaled_toward_zero(FxVec3 a, Fx s) {
    return {fx_mul_trunc(a.x, s), fx_mul_trunc(a.y, s), fx_mul_trunc(a.z, s)};
}

constexpr FxVec2 operator+(FxVec2 a, FxVec2 b) {
    return {a.x + b.x, a.z + b.z};
}
constexpr FxVec2 operator-(FxVec2 a, FxVec2 b) {
    return {a.x - b.x, a.z - b.z};
}
constexpr FxVec2 operator*(FxVec2 a, Fx s) {
    return {fx_mul_near(a.x, s), fx_mul_near(a.z, s)};
}

constexpr FxVec2 flat(FxVec3 v) {
    return {v.x, v.z};
}
constexpr FxVec3 unflat(FxVec2 v, Fx y) {
    return {v.x, y, v.z};
}

// exact dot products: raw products summed whole (Q32). Exact while every part is under 16384
// units, which every stage and fighter position is by far
constexpr std::int64_t dot_q32(FxVec3 a, FxVec3 b) {
    return static_cast<std::int64_t>(a.x.raw) * b.x.raw +
           static_cast<std::int64_t>(a.y.raw) * b.y.raw +
           static_cast<std::int64_t>(a.z.raw) * b.z.raw;
}
constexpr std::int64_t dot_q32(FxVec2 a, FxVec2 b) {
    return static_cast<std::int64_t>(a.x.raw) * b.x.raw +
           static_cast<std::int64_t>(a.z.raw) * b.z.raw;
}
// the 2D cross (a.x b.z - a.z b.x), exact; its sign says which side of a b is
constexpr std::int64_t cross_q32(FxVec2 a, FxVec2 b) {
    return static_cast<std::int64_t>(a.x.raw) * b.z.raw -
           static_cast<std::int64_t>(a.z.raw) * b.x.raw;
}
// dot products as Fx, rounded once; saturated past +-32768 (use dot_q32 for stage-sized vectors)
constexpr Fx dot(FxVec3 a, FxVec3 b) {
    return fx_from_q32(dot_q32(a, b));
}
constexpr Fx dot(FxVec2 a, FxVec2 b) {
    return fx_from_q32(dot_q32(a, b));
}
// D3DXVec3Cross: each part's two products summed whole, rounded once, saturated
constexpr FxVec3 cross(FxVec3 a, FxVec3 b) {
    return {fx_from_q32(static_cast<std::int64_t>(a.y.raw) * b.z.raw -
                        static_cast<std::int64_t>(a.z.raw) * b.y.raw),
            fx_from_q32(static_cast<std::int64_t>(a.z.raw) * b.x.raw -
                        static_cast<std::int64_t>(a.x.raw) * b.z.raw),
            fx_from_q32(static_cast<std::int64_t>(a.x.raw) * b.y.raw -
                        static_cast<std::int64_t>(a.y.raw) * b.x.raw)};
}

// squared lengths, exact (Q32)
constexpr std::int64_t length_q32(FxVec3 v) {
    return dot_q32(v, v);
}
constexpr std::int64_t length_q32(FxVec2 v) {
    return dot_q32(v, v);
}
constexpr std::int64_t distance_q32(FxVec3 a, FxVec3 b) {
    return length_q32(a - b);
}
// lengths, nearest
constexpr Fx length(FxVec3 v) {
    return fx_sqrt_q32(length_q32(v));
}
constexpr Fx length(FxVec2 v) {
    return fx_sqrt_q32(length_q32(v));
}
constexpr Fx distance(FxVec3 a, FxVec3 b) {
    return length(a - b);
}

// |v| > r, |v| < r and |v| <= r on the exact squared length, without a root
constexpr bool longer_than(FxVec3 v, Fx r) {
    return r.raw < 0 || length_q32(v) > static_cast<std::int64_t>(r.raw) * r.raw;
}
constexpr bool shorter_than(FxVec3 v, Fx r) {
    return r.raw > 0 && length_q32(v) < static_cast<std::int64_t>(r.raw) * r.raw;
}
constexpr bool within(FxVec3 v, Fx r) {
    return r.raw >= 0 && length_q32(v) <= static_cast<std::int64_t>(r.raw) * r.raw;
}
constexpr bool longer_than(FxVec2 v, Fx r) {
    return r.raw < 0 || length_q32(v) > static_cast<std::int64_t>(r.raw) * r.raw;
}
constexpr bool shorter_than(FxVec2 v, Fx r) {
    return r.raw > 0 && length_q32(v) < static_cast<std::int64_t>(r.raw) * r.raw;
}

// (x, y, z) scaled to length size, each part rounded to nearest; zero stays zero. The parts are
// any whole numbers in one scale with magnitudes under 2^30 (raw Fx under 16384 units, or exact
// Q32 products brought down). The length is worked out with as many extra bits as fit, so a short
// vector (a slow velocity, a few raw units a part) is scaled by its true length, not by a root
// rounded to a whole raw unit
constexpr FxVec3 scaled_parts_to(std::int64_t x, std::int64_t y, std::int64_t z, Fx size) {
    const std::int64_t squared = x * x + y * y + z * z;
    if (squared == 0) {
        return {};
    }
    const int bits = std::bit_width(static_cast<std::uint64_t>(squared));
    const int extra = bits >= 62 ? 0 : (62 - bits) / 2;
    const auto root =
        static_cast<std::int64_t>(isqrt_round(static_cast<std::uint64_t>(squared) << (2 * extra)));
    // each part shifted by extra is at most the root, under 2^31, so times size fits
    const auto part = [&](std::int64_t p) {
        return fx_saturate(div_round((p << extra) * size.raw, root));
    };
    return {part(x), part(y), part(z)};
}
// v with length size (D3DXVec3Normalize for size 1)
constexpr FxVec3 scaled_to(FxVec3 v, Fx size) {
    return scaled_parts_to(v.x.raw, v.y.raw, v.z.raw, size);
}
constexpr FxVec3 normalized(FxVec3 v) {
    return scaled_to(v, fx(1));
}
constexpr FxVec2 scaled_to(FxVec2 v, Fx size) {
    const FxVec3 scaled = scaled_to(unflat(v, Fx{}), size);
    return flat(scaled);
}
constexpr FxVec2 normalized(FxVec2 v) {
    return scaled_to(v, fx(1));
}
// v cut to length most when longer (the integrate's velocity, push and impulse caps of 5)
constexpr FxVec3 clamp_length(FxVec3 v, Fx most) {
    return longer_than(v, most) ? scaled_to(v, most) : v;
}

constexpr FxVec3 lerp(FxVec3 a, FxVec3 b, Fx t) {
    return a + (b - a) * t;
}

// a x b's three exact Q32 parts, brought down together until each is under 2^30 (exact while a
// and b's parts are under 16384 units)
struct FxCrossParts {
    std::int64_t x, y, z;
    int shift;
};
constexpr FxCrossParts cross_parts(FxVec3 a, FxVec3 b) {
    FxCrossParts c{
        static_cast<std::int64_t>(a.y.raw) * b.z.raw - static_cast<std::int64_t>(a.z.raw) * b.y.raw,
        static_cast<std::int64_t>(a.z.raw) * b.x.raw - static_cast<std::int64_t>(a.x.raw) * b.z.raw,
        static_cast<std::int64_t>(a.x.raw) * b.y.raw - static_cast<std::int64_t>(a.y.raw) * b.x.raw,
        0};
    const auto size = [](std::int64_t v) { return static_cast<std::uint64_t>(v < 0 ? -v : v); };
    const std::uint64_t most = size(c.x) | size(c.y) | size(c.z);
    c.shift = std::bit_width(most) > 30 ? std::bit_width(most) - 30 : 0;
    if (c.shift > 0) {
        c.x = shift_round(c.x, c.shift);
        c.y = shift_round(c.y, c.shift);
        c.z = shift_round(c.z, c.shift);
    }
    return c;
}
// normalize(a x b) from the exact cross, not from a cross rounded to Fx first: the camera's and
// the look-at's side axis stays good when the view is near the up axis (the cross is short)
constexpr FxVec3 unit_cross(FxVec3 a, FxVec3 b) {
    const FxCrossParts c = cross_parts(a, b);
    return scaled_parts_to(c.x, c.y, c.z, fx(1));
}

// vec3_angle_between (acos of the dot over the lengths), 0..pi, 0 for a zero vector:
// taken as atan2(|a x b|, a . b) on the exact products, which stays good for small angles where
// an acos of a rounded cosine is 0.0055 rad off at 1 raw unit under 1
// decomp: vec3_angle_between 0xD3C40
constexpr Angle angle_between(FxVec3 a, FxVec3 b) {
    const FxCrossParts c = cross_parts(a, b);
    const std::int64_t dot_whole = dot_q32(a, b);
    const std::int64_t along = c.shift > 0 ? shift_round(dot_whole, c.shift) : dot_whole;
    const auto across = static_cast<std::int64_t>(
        isqrt_round(static_cast<std::uint64_t>(c.x * c.x + c.y * c.y + c.z * c.z)));
    return angle_atan2_wide(across, along);
}
