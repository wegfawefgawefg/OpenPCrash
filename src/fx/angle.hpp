#pragma once
// Binary angles and table trig. A turn is 2^32 units, so an angle wraps by itself (the original
// wraps its float radians to +-pi by hand) and the smallest step is 1.5e-9 rad: a 0.1 degree a
// tick turn rate times a stick of 0.01 is still 11,930 units. sin and cos come out within half a
// raw unit of the true value, atan2 within 1e-7 rad (docs/numbers.md). The tables are
// tools/fx_trig_tables.py's.
//
// Angle is its own type, not an integer, so a 16-bit angle or an Fx in radians can't be passed
// where one is meant; conversions are named.
#include "fx/fx.hpp"
#include "fx/fx_tables.hpp"

#include <bit>
#include <cstdint>

struct Angle {
    std::uint32_t raw = 0;
    friend constexpr bool operator==(Angle, Angle) = default;
};

inline constexpr std::uint32_t angle_quarter_raw = 1u << 30;
inline constexpr std::uint32_t angle_half_raw = 1u << 31;
inline constexpr Angle angle_quarter{angle_quarter_raw};
inline constexpr Angle angle_half{angle_half_raw};

// sums and differences wrap round the turn
constexpr Angle operator+(Angle a, Angle b) {
    return {a.raw + b.raw};
}
constexpr Angle operator-(Angle a, Angle b) {
    return {a.raw - b.raw};
}
constexpr Angle operator-(Angle a) {
    return {0u - a.raw};
}
constexpr Angle& operator+=(Angle& a, Angle b) {
    a = a + b;
    return a;
}
constexpr Angle& operator-=(Angle& a, Angle b) {
    a = a - b;
    return a;
}

// the angle as -half..half (units), the way the original keeps a heading in -pi..pi
constexpr std::int32_t angle_signed(Angle a) {
    return static_cast<std::int32_t>(a.raw);
}
// a signed angle times an Fx (a turn rate times the stick), nearest
constexpr Angle angle_scaled(Angle a, Fx factor) {
    const std::int64_t units =
        shift_round(static_cast<std::int64_t>(angle_signed(a)) * factor.raw, fx_shift);
    return {static_cast<std::uint32_t>(units)};
}
// a signed angle held to -limit..limit (limit under a half turn): the AI's and the homing turn
// clamps
constexpr Angle angle_clamp(Angle a, Angle limit) {
    const std::int32_t value = angle_signed(a), most = angle_signed(limit);
    if (value > most) {
        return limit;
    }
    if (value < -most) {
        return -limit;
    }
    return a;
}
// |a| as a signed angle, at most limit (the view cone and the 90 degree checks)
constexpr bool angle_within(Angle a, Angle limit) {
    const std::int64_t value = angle_signed(a);
    const std::int64_t size = value < 0 ? -value : value;
    return size <= angle_signed(limit);
}

// Constants from the original's radians and degrees, consteval: angle_f_radians(0.5235988f),
// angle_f_degrees(45.0). Rounded to nearest; wraps like any angle
inline constexpr double fx_pi_double = 3.14159265358979323846;
consteval Angle angle_f_turns(double turns) {
    const double units = turns * 4294967296.0;
    const bool negative = units < 0;
    const double size = negative ? -units : units;
    std::int64_t whole = static_cast<std::int64_t>(size);
    if (size - static_cast<double>(whole) >= 0.5) {
        whole += 1;
    }
    return {static_cast<std::uint32_t>(negative ? -whole : whole)};
}
consteval Angle angle_f_radians(double radians) {
    return angle_f_turns(radians / (2 * fx_pi_double));
}
consteval Angle angle_f_degrees(double degrees) {
    return angle_f_turns(degrees / 360.0);
}

// Fx radians or degrees to an angle and back (back as -pi..pi, -180..180), nearest
constexpr Angle angle_from_radians(Fx radians) {
    const std::int64_t units = shift_round(radians.raw * fx_radians_to_units_q16, 16);
    return {static_cast<std::uint32_t>(units)};
}
constexpr Angle angle_from_degrees(Fx degrees) {
    const std::int64_t units = shift_round(degrees.raw * fx_degrees_to_units_q22, 22);
    return {static_cast<std::uint32_t>(units)};
}
constexpr Fx angle_radians(Angle a) {
    return fx_raw(static_cast<std::int32_t>(
        shift_round(static_cast<std::int64_t>(angle_signed(a)) * fx_units_to_radians_q40, 40)));
}
// exact: units * 360 * 2^16 / 2^32 is units * 45 / 2^13
constexpr Fx angle_degrees(Angle a) {
    return fx_raw(static_cast<std::int32_t>(
        shift_round(static_cast<std::int64_t>(angle_signed(a)) * 45, 13)));
}

// sin in Q30, the quarter-wave table read with linear interpolation (2^20 units a segment)
constexpr std::int64_t angle_sin_q30(Angle a) {
    const std::uint32_t quadrant = a.raw >> 30;
    std::uint32_t within = a.raw & (angle_quarter_raw - 1);
    if (quadrant & 1) {
        within = angle_quarter_raw - within;
    }
    const std::uint32_t index = within >> 20, fraction = within & 0xFFFFF;
    std::int64_t value = fx_sine_q30[index];
    if (fraction != 0) {
        value += ((fx_sine_q30[index + 1] - value) * fraction + (1 << 19)) >> 20;
    }
    return quadrant & 2 ? -value : value;
}
constexpr Fx fx_sin(Angle a) {
    return fx_raw(static_cast<std::int32_t>(shift_round(angle_sin_q30(a), 14)));
}
constexpr Fx fx_cos(Angle a) {
    return fx_sin(a + angle_quarter);
}

// The angle of (x, y) as C's atan2(y, x) gives it: 0 along +x, a quarter turn along +y, -pi..pi.
// y and x in any one scale (raw Fx, Q32 products...); (0, 0) is 0
constexpr Angle angle_atan2_wide(std::int64_t y, std::int64_t x) {
    if (x == 0 && y == 0) {
        return {};
    }
    const std::uint64_t ax =
        x < 0 ? 0 - static_cast<std::uint64_t>(x) : static_cast<std::uint64_t>(x);
    const std::uint64_t ay =
        y < 0 ? 0 - static_cast<std::uint64_t>(y) : static_cast<std::uint64_t>(y);
    const bool steep = ay > ax;
    std::uint64_t low = steep ? ax : ay, high = steep ? ay : ax;
    // the ratio low / high in Q30, high brought under 2^32 first so low << 30 can't wrap
    const int over = std::bit_width(high) - 32;
    if (over > 0) {
        low >>= over;
        high >>= over;
    }
    const std::uint64_t ratio = (low << 30) / high;
    const std::uint64_t index = ratio >> 20, fraction = ratio & 0xFFFFF;
    std::int64_t octant = fx_atan_units[index];
    if (fraction != 0) {
        octant += ((fx_atan_units[index + 1] - octant) * static_cast<std::int64_t>(fraction) +
                   (1 << 19)) >>
                  20;
    }
    std::uint32_t angle = static_cast<std::uint32_t>(octant);
    if (steep) {
        angle = angle_quarter_raw - angle;
    }
    if (x < 0) {
        angle = angle_half_raw - angle;
    }
    if (y < 0) {
        angle = 0u - angle;
    }
    return {angle};
}
constexpr Angle angle_atan2(Fx y, Fx x) {
    return angle_atan2_wide(y.raw, x.raw);
}

// acos and asin through atan2, with the other side sqrt(1 - c^2) worked out in Q31. Near +-1
// the answer is only as good as the input: a cosine 1 raw unit under 1 is already 0.0055 rad off
// 0 (fx/vec.hpp's angle_between avoids that)
constexpr std::int64_t angle_other_side_q31(Fx c) {
    const std::int64_t size = c.raw < 0 ? -static_cast<std::int64_t>(c.raw) : c.raw;
    const std::int64_t held = size > fx_one_raw ? fx_one_raw : size;
    const std::uint64_t rest = (std::uint64_t{1} << 32) - static_cast<std::uint64_t>(held * held);
    return static_cast<std::int64_t>(isqrt_round(rest << 30));
}
constexpr Angle angle_acos(Fx c) {
    const Fx held = fx_clamp(c, fx(-1), fx(1));
    return angle_atan2_wide(angle_other_side_q31(held), static_cast<std::int64_t>(held.raw) << 15);
}
constexpr Angle angle_asin(Fx s) {
    const Fx held = fx_clamp(s, fx(-1), fx(1));
    return angle_atan2_wide(static_cast<std::int64_t>(held.raw) << 15, angle_other_side_q31(held));
}
