#pragma once
// Fixed-point scalars for the deterministic world: gfxp's Q16.16 (Fx), conversions from the
// original's float constants, rounding variants, 64-bit helpers and integer square roots. Integer
// only, so GCC and Clang on x86-64 and ARM64 compute the same bits. docs/numbers.md has the ranges
// (where Q16.16 holds, where the 64-bit helpers are needed) and the rounding rules.
//
// Rounding: gfxp's a * b floors (rounds toward minus infinity) and a / b truncates; both wrap when
// the result is past +-32768. Everything else in src/fx rounds to nearest with halves away from
// zero, so mirrored inputs give mirrored results (a mech strafing left moves exactly as far as one
// strafing right). Use fx_mul_near where that matters and fx_mul_trunc for decays (a value scaled
// by 0.8 every step reaches 0 from either side; floored, a negative one sticks a few raw below).
#include <array>
#include <bit>
#include <cstdint>
#include <gfxp/fixed.hpp>

using Fx = gfxp::Fixed16;

inline constexpr int fx_shift = Fx::frac_bits;
inline constexpr std::int32_t fx_one_raw = Fx::scale;
inline constexpr std::int64_t fx_raw_max = INT32_MAX;
inline constexpr std::int64_t fx_raw_min = INT32_MIN;

// Fx{n} sets the raw value (n / 65536): say which is meant with fx, fx_raw or fx_f
constexpr Fx fx_raw(std::int32_t raw) {
    return Fx::from_raw(raw);
}
constexpr Fx fx(int whole) {
    return fx_raw(static_cast<std::int32_t>(static_cast<std::int64_t>(whole) * fx_one_raw));
}
// a raw value past what Fx holds comes out at the most it holds, its sign kept
constexpr Fx fx_saturate(std::int64_t raw) {
    return fx_raw(static_cast<std::int32_t>(raw > fx_raw_max   ? fx_raw_max
                                            : raw < fx_raw_min ? fx_raw_min
                                                               : raw));
}

// value / 2^shift, nearest, halves away from zero (shift 1..62)
constexpr std::int64_t shift_round(std::int64_t value, int shift) {
    const std::int64_t half = std::int64_t{1} << (shift - 1);
    if (value >= 0) {
        return (value + half) >> shift;
    }
    return -((-value + half) >> shift);
}
// numerator / denominator, nearest, halves away from zero (denominator not 0)
constexpr std::int64_t div_round(std::int64_t numerator, std::int64_t denominator) {
    const std::int64_t quotient = numerator / denominator;
    std::int64_t rest = numerator % denominator;
    std::int64_t size = denominator;
    rest = rest < 0 ? -rest : rest;
    size = size < 0 ? -size : size;
    if (rest >= size - rest) {
        return (numerator < 0) != (denominator < 0) ? quotient - 1 : quotient + 1;
    }
    return quotient;
}

// Constants from the original's float literals, written next to the value they replace:
// fx_f(0.985f), fx_f(9.8 / 900 * 2.5). consteval, so no float reaches the running world; the
// float is scaled by 2^16 (exact) and rounded to nearest, halves away from zero, the same as
// fx_bits on the float's bits. A constant past +-32768 stops the build.
void fx_f_constant_out_of_range(); // not constexpr: reaching it in fx_f is a compile error
consteval Fx fx_f(double value) {
    const double scaled = value * fx_one_raw;
    if (!(scaled > -2147483648.0 && scaled < 2147483647.0)) {
        fx_f_constant_out_of_range();
    }
    const bool negative = scaled < 0;
    const double size = negative ? -scaled : scaled;
    std::int64_t whole = static_cast<std::int64_t>(size);
    if (size - static_cast<double>(whole) >= 0.5) {
        whole += 1;
    }
    return fx_raw(static_cast<std::int32_t>(negative ? -whole : whole));
}

// An IEEE-754 single given by its bits (the decomp's *(int *)& constants like 0x3E490FDB, and
// every float the loaders read from the disc or the XBE) as Fx, integer only: nearest, halves away
// from zero, past the range saturated, NaN as 0
constexpr Fx fx_bits(std::uint32_t bits) {
    const bool negative = (bits >> 31) != 0;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF);
    std::int64_t mantissa = bits & 0x7FFFFF;
    if (exponent == 0xFF) {
        if (mantissa != 0) {
            return Fx{};
        }
        return fx_saturate(negative ? fx_raw_min : fx_raw_max);
    }
    int power = exponent;
    if (exponent == 0) {
        power = 1;
    } else {
        mantissa |= 0x800000;
    }
    // value = mantissa * 2^(power - 150), so raw = mantissa * 2^(power - 134)
    const int shift = power - 134;
    std::int64_t raw = 0;
    if (shift > 8) {
        raw = fx_raw_max + 1;
    } else if (shift >= 0) {
        raw = mantissa << shift;
    } else if (shift >= -40) {
        raw = shift_round(mantissa, -shift);
    }
    return fx_saturate(negative ? -raw : raw);
}

// numerator / denominator, nearest (fx_ratio(1, 3) is 21845 raw)
constexpr Fx fx_ratio(std::int64_t numerator, std::int64_t denominator) {
    return fx_saturate(div_round(numerator * fx_one_raw, denominator));
}

// presentation only: the world never reads a float back
constexpr float fx_float(Fx value) {
    return value.to_float();
}

constexpr Fx fx_abs(Fx a) {
    return a.abs();
}
constexpr Fx fx_min(Fx a, Fx b) {
    return gfxp::min(a, b);
}
constexpr Fx fx_max(Fx a, Fx b) {
    return gfxp::max(a, b);
}
constexpr Fx fx_clamp(Fx value, Fx low, Fx high) {
    return gfxp::clamp(value, low, high);
}
constexpr int fx_sign(Fx a) {
    return a.sign();
}

// a * b rounded to nearest, halves away from zero: fx_mul_near(-a, b) is -fx_mul_near(a, b)
constexpr Fx fx_mul_near(Fx a, Fx b) {
    return fx_saturate(shift_round(static_cast<std::int64_t>(a.raw) * b.raw, fx_shift));
}
// a * b rounded toward zero: for decays and drags, which then reach 0 from either side
constexpr Fx fx_mul_trunc(Fx a, Fx b) {
    const std::int64_t product = static_cast<std::int64_t>(a.raw) * b.raw;
    return fx_saturate(product >= 0 ? product >> fx_shift : -((-product) >> fx_shift));
}
// a / b rounded to nearest; past the range (b near 0) saturated, x / 0 the most of x's sign: a
// ray all but along a face meets its plane far off, not somewhere near
constexpr Fx fx_div_near(Fx a, Fx b) {
    if (b.raw == 0) {
        return fx_saturate(a.raw >= 0 ? fx_raw_max : fx_raw_min);
    }
    return fx_saturate(div_round(static_cast<std::int64_t>(a.raw) * fx_one_raw, b.raw));
}
// a * b / c in one rounding (the product kept whole in 64 bits), saturated; c 0 as fx_div_near
constexpr Fx fx_mul_div(Fx a, Fx b, Fx c) {
    const std::int64_t product = static_cast<std::int64_t>(a.raw) * b.raw;
    if (c.raw == 0) {
        return fx_saturate(product >= 0 ? fx_raw_max : fx_raw_min);
    }
    return fx_saturate(div_round(product, c.raw));
}
// (int)(a / b) as the original casts a float quotient: truncated toward zero (grid cells)
constexpr int fx_trunc_div(Fx a, Fx b) {
    return static_cast<int>(a.raw / b.raw);
}
constexpr Fx fx_lerp(Fx a, Fx b, Fx t) {
    return a + fx_mul_near(b - a, t);
}
// base^power by repeated multiplication, each step rounded to nearest (the overweight cliff's
// (capacity / weight)^10)
constexpr Fx fx_pow_int(Fx base, int power) {
    Fx result = fx(1);
    for (int i = 0; i < power; ++i) {
        result = fx_mul_near(result, base);
    }
    return result;
}

// Q32: a raw product of two Fx kept whole in 64 bits (squared lengths, dot products of
// stage-sized vectors). Back to Fx rounded to nearest, saturated
constexpr Fx fx_from_q32(std::int64_t q32) {
    return fx_saturate(shift_round(q32, fx_shift));
}

// Integer Newton iteration from any start at or above the root, stopping at the exact floor
// (including the two-cycle just below a perfect square)
constexpr std::uint64_t isqrt_from(std::uint64_t value, std::uint64_t root) {
    for (;;) {
        const std::uint64_t next = (root + value / root) / 2;
        if (next >= root) {
            return root;
        }
        root = next;
    }
}

// for top, the ten highest bits of a value: just over the root of (top + 1) times 2^22, scaled
// 2^11 (isqrt64's start; Mans In My House's)
inline constexpr auto isqrt_starts = [] {
    std::array<std::uint32_t, 1024> starts{};
    for (std::uint64_t top = 0; top < 1024; ++top) {
        const std::uint64_t scaled = (top + 1) << 22;
        const std::uint64_t first = std::uint64_t{1} << ((std::bit_width(scaled) + 1) / 2);
        starts[top] = static_cast<std::uint32_t>(isqrt_from(scaled, first) + 1);
    }
    return starts;
}();

// floor(sqrt(value)), exact for every 64-bit value: started just over the root by its ten highest
// bits, two or three Newton steps
constexpr std::uint64_t isqrt64(std::uint64_t value) {
    if (value == 0) {
        return 0;
    }
    const int bits = std::bit_width(value);
    const int shift = bits > 10 ? (bits - 10 + 1) & ~1 : 0;
    const std::uint64_t top = value >> shift;
    const std::uint64_t start =
        (static_cast<std::uint64_t>(isqrt_starts[top]) << (shift / 2)) >> 11;
    return isqrt_from(value, start + 1);
}

// the whole number nearest the root
constexpr std::uint64_t isqrt_round(std::uint64_t value) {
    const std::uint64_t root = isqrt64(value);
    return value - root * root > root ? root + 1 : root;
}

// sqrt of a Q32 (a squared length): an Fx, nearest
constexpr Fx fx_sqrt_q32(std::int64_t q32) {
    if (q32 <= 0) {
        return Fx{};
    }
    return fx_saturate(static_cast<std::int64_t>(isqrt_round(static_cast<std::uint64_t>(q32))));
}
constexpr Fx fx_sqrt(Fx value) {
    if (value.raw <= 0) {
        return Fx{};
    }
    return fx_sqrt_q32(static_cast<std::int64_t>(value.raw) << fx_shift);
}
