// The pose's matrix pieces in Q16.16 (mech/pose_math.hpp): one matrix for a node's three turns,
// the IK solvers (decomp src/hand/beam_quad.c 0xAB9BE..0xAC3B2, as mech_ik.cpp has them in
// floats), and quaternions for the blends. The IK needs no angles: the aim is two ratios of the
// target's local offset, the bends are the law of cosines, their sines the exact other side.
#include "mech/pose_math.hpp"

namespace {

// a product of three Q16 values summed whole, Q48
std::int64_t q48(Fx a, Fx b, Fx c) {
    return static_cast<std::int64_t>(a.raw) * b.raw * c.raw;
}
std::int64_t q48(Fx a, Fx b) {
    return static_cast<std::int64_t>(a.raw) * b.raw * fx_one_raw;
}
Fx from_q48(std::int64_t value) {
    return fx_saturate(shift_round(value, 32));
}

FxMat3 rows(Fx xx, Fx xy, Fx xz, Fx yx, Fx yy, Fx yz, Fx zx, Fx zy, Fx zz) {
    return {{xx, xy, xz}, {yx, yy, yz}, {zx, zy, zz}};
}

// m turned so its x axis points at v, first about its z axis by the
// target's (x, y), then lifted toward its z; the distance to v
// decomp: matrix_aim_x_at 0xABFCE
Fx aim_x_at(FxVec3 v, const FxMat4& m, FxMat4& out) {
    const FxMat3 basis = mat4_basis(m);
    const FxVec3 local = transform(v - m.w, mat3_transpose(basis));
    const std::int64_t flat_q32 = static_cast<std::int64_t>(local.x.raw) * local.x.raw +
                                  static_cast<std::int64_t>(local.y.raw) * local.y.raw;
    const std::int64_t reach_q32 = flat_q32 + static_cast<std::int64_t>(local.z.raw) * local.z.raw;
    const Fx flat = fx_sqrt_q32(flat_q32);
    const Fx reach = fx_sqrt_q32(reach_q32);
    // the turn about z (cos, sin of (x, y)); retail divides by 0 when the target is on the z axis
    FxVec3 turn{fx(1), Fx{}, Fx{}};
    if (flat_q32 > 0) {
        turn = scaled_parts_to(local.x.raw, local.y.raw, 0, fx(1));
    }
    // the lift (flat, z) over the reach
    FxVec3 lift{fx(1), Fx{}, Fx{}};
    if (reach_q32 > 0) {
        lift = scaled_parts_to(flat.raw, local.z.raw, 0, fx(1));
    }
    const FxMat3 turned = rows(turn.x, turn.y, Fx{}, -turn.y, turn.x, Fx{}, Fx{}, Fx{}, fx(1));
    const FxMat3 lifted = rows(lift.x, Fx{}, lift.y, Fx{}, fx(1), Fx{}, -lift.y, Fx{}, lift.x);
    out = mat4_from(mat3_mul(lifted, mat3_mul(turned, basis)), m.w);
    return reach;
}

// the cosine and sine of the angle between sides a and c with side b opposite:
// (a^2 + c^2 - b^2) / 2ac, clamped to +-1 as retail clamps it; the sine from the exact other side
void law_of_cosines(Fx a, Fx c, Fx b, Fx& cosine, Fx& sine) {
    const std::int64_t num = static_cast<std::int64_t>(a.raw) * a.raw +
                             static_cast<std::int64_t>(c.raw) * c.raw -
                             static_cast<std::int64_t>(b.raw) * b.raw;
    const std::int64_t den = 2 * static_cast<std::int64_t>(a.raw) * c.raw;
    if (den <= 0 || num >= den) {
        cosine = fx(1);
        sine = Fx{};
        return;
    }
    if (num <= -den) {
        cosine = fx(-1);
        sine = Fx{};
        return;
    }
    cosine = fx_raw(static_cast<std::int32_t>(div_round(num << 16, den)));
    // sqrt(den^2 - num^2) / den, brought down 12 bits so the product fits 64 bits
    const std::int64_t n = shift_round(num, 12), d = shift_round(den, 12);
    const auto side = static_cast<std::int64_t>(isqrt_round(static_cast<std::uint64_t>((d - n) * (d + n))));
    sine = d > 0 ? fx_raw(static_cast<std::int32_t>(div_round(side << 16, d))) : Fx{};
}

Fx quarter(Fx value) {
    return fx_raw(static_cast<std::int32_t>(div_round(value.raw, 4)));
}

FxMat4 along_x(Fx length, const FxMat4& m) {
    return mat4_mul(mat4_translation({length, Fx{}, Fx{}}), m);
}

Fx quat_dot_raw(FxQuat a, FxQuat b) {
    return fx_from_q32(static_cast<std::int64_t>(a.x.raw) * b.x.raw +
                       static_cast<std::int64_t>(a.y.raw) * b.y.raw +
                       static_cast<std::int64_t>(a.z.raw) * b.z.raw +
                       static_cast<std::int64_t>(a.w.raw) * b.w.raw);
}

FxQuat quat_normalized(FxQuat q) {
    const std::int64_t squared = static_cast<std::int64_t>(q.x.raw) * q.x.raw +
                                 static_cast<std::int64_t>(q.y.raw) * q.y.raw +
                                 static_cast<std::int64_t>(q.z.raw) * q.z.raw +
                                 static_cast<std::int64_t>(q.w.raw) * q.w.raw;
    if (squared <= 0) {
        return {Fx{}, Fx{}, Fx{}, fx(1)};
    }
    // the length in Q24, so a part is divided by a root good to 2^-24
    const auto root = static_cast<std::int64_t>(isqrt_round(static_cast<std::uint64_t>(squared) << 16));
    const auto part = [&](Fx v) {
        return fx_raw(static_cast<std::int32_t>(div_round(static_cast<std::int64_t>(v.raw) << 24, root)));
    };
    return {part(q.x), part(q.y), part(q.z), part(q.w)};
}

} // namespace

FxMat3 mat3_turns(Angle x, Angle y, Angle z) {
    const Fx sx = fx_sin(x), cx = fx_cos(x), sy = fx_sin(y), cy = fx_cos(y), sz = fx_sin(z),
             cz = fx_cos(z);
    return rows(from_q48(q48(cy, cz)), from_q48(q48(cy, sz)), -sy,
                from_q48(q48(sx, sy, cz) - q48(cx, sz)), from_q48(q48(sx, sy, sz) + q48(cx, cz)),
                from_q48(q48(sx, cy)),
                from_q48(q48(cx, sy, cz) + q48(sx, sz)), from_q48(q48(cx, sy, sz) - q48(sx, cz)),
                from_q48(q48(cx, cy)));
}

// decomp: sv_ik_solve_three 0xAB9BE, ik_bend_two_bones 0xABBE4
void ik_solve_three(FxVec3 target, const FxMat4& top, Fx length_a, Fx length_b, bool positive,
                    FxMat4& joint, FxMat4& end, FxMat4& tip) {
    const Fx reach = aim_x_at(target, top, joint);
    if (length_a + length_b <= reach) {
        // out of reach: the chain straight along the aim
        end = along_x(length_a, joint);
        tip = along_x(length_b, end);
        return;
    }
    // the first link turned off the aim
    Fx cosine, sine;
    law_of_cosines(length_a, reach, length_b, cosine, sine);
    const FxMat3 bend = rows(cosine, positive ? -sine : sine, Fx{}, positive ? sine : -sine,
                             cosine, Fx{}, Fx{}, Fx{}, fx(1));
    joint = mat4_from(mat3_mul(bend, mat4_basis(joint)), joint.w);
    // the second link turned back at the knee, moved out along the first
    law_of_cosines(length_a, length_b, reach, cosine, sine);
    const FxMat3 knee = rows(-cosine, positive ? sine : -sine, Fx{}, positive ? -sine : sine,
                             -cosine, Fx{}, Fx{}, Fx{}, fx(1));
    end = mat4_mul(mat4_from(knee, {length_a, Fx{}, Fx{}}), joint);
    tip = along_x(length_b, end);
}

// decomp: sv_ik_solve_two 0xAC25D, ik_link_aim 0xAC3B2
void ik_solve_two(FxVec3 target, const FxMat4& top, Fx length, FxMat4& joint, FxMat4& end) {
    aim_x_at(target, top, joint);
    end = along_x(length, joint);
}

FxQuat quat_from_mat3(const FxMat3& m) {
    const Fx trace = m.x.x + m.y.y + m.z.z;
    FxQuat q;
    if (trace > Fx{}) {
        const Fx s = fx_sqrt(trace + fx(1)) * 2; // 4w
        q = {fx_div_near(m.y.z - m.z.y, s), fx_div_near(m.z.x - m.x.z, s),
             fx_div_near(m.x.y - m.y.x, s), quarter(s)};
    } else if (m.x.x >= m.y.y && m.x.x >= m.z.z) {
        const Fx s = fx_sqrt(fx(1) + m.x.x - m.y.y - m.z.z) * 2; // 4x
        q = {quarter(s), fx_div_near(m.x.y + m.y.x, s), fx_div_near(m.x.z + m.z.x, s),
             fx_div_near(m.y.z - m.z.y, s)};
    } else if (m.y.y >= m.z.z) {
        const Fx s = fx_sqrt(fx(1) + m.y.y - m.x.x - m.z.z) * 2; // 4y
        q = {fx_div_near(m.x.y + m.y.x, s), quarter(s), fx_div_near(m.y.z + m.z.y, s),
             fx_div_near(m.z.x - m.x.z, s)};
    } else {
        const Fx s = fx_sqrt(fx(1) + m.z.z - m.x.x - m.y.y) * 2; // 4z
        q = {fx_div_near(m.x.z + m.z.x, s), fx_div_near(m.y.z + m.z.y, s), quarter(s),
             fx_div_near(m.x.y - m.y.x, s)};
    }
    return quat_normalized(q);
}

FxMat3 mat3_from_quat(FxQuat q) {
    // each entry 1 - 2(..) or 2(..) of Q32 products, rounded once
    const auto p = [](Fx a, Fx b) { return static_cast<std::int64_t>(a.raw) * b.raw; };
    const auto two = [](std::int64_t q32) { return fx_from_q32(2 * q32); };
    const auto one_less = [](std::int64_t q32) {
        return fx_from_q32((std::int64_t{1} << 32) - 2 * q32);
    };
    return rows(one_less(p(q.y, q.y) + p(q.z, q.z)), two(p(q.x, q.y) + p(q.z, q.w)),
                two(p(q.x, q.z) - p(q.y, q.w)), two(p(q.x, q.y) - p(q.z, q.w)),
                one_less(p(q.x, q.x) + p(q.z, q.z)), two(p(q.y, q.z) + p(q.x, q.w)),
                two(p(q.x, q.z) + p(q.y, q.w)), two(p(q.y, q.z) - p(q.x, q.w)),
                one_less(p(q.x, q.x) + p(q.y, q.y)));
}

FxQuat quat_slerp(FxQuat a, FxQuat b, Fx t) {
    if (quat_dot_raw(a, b) < Fx{}) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    // the angle between them from the chord, 2 asin(|b - a| / 2): good for small angles too
    const auto d = [](Fx u, Fx v) {
        const std::int64_t e = static_cast<std::int64_t>(v.raw) - u.raw;
        return e * e;
    };
    const Fx chord = fx_sqrt_q32(d(a.x, b.x) + d(a.y, b.y) + d(a.z, b.z) + d(a.w, b.w));
    const Angle half = angle_asin(fx_raw(static_cast<std::int32_t>(div_round(chord.raw, 2))));
    const Angle theta = half + half;
    const Fx sine = fx_sin(theta);
    Fx wa = fx(1) - t, wb = t;
    // under 0.05 rad the normalized lerp is the slerp to well under a raw unit of the result
    if (sine > fx_f(0.05)) {
        wa = fx_div_near(fx_sin(angle_scaled(theta, fx(1) - t)), sine);
        wb = fx_div_near(fx_sin(angle_scaled(theta, t)), sine);
    }
    const auto mix = [&](Fx u, Fx v) {
        return fx_from_q32(static_cast<std::int64_t>(u.raw) * wa.raw +
                           static_cast<std::int64_t>(v.raw) * wb.raw);
    };
    return quat_normalized({mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.w, b.w)});
}

FxMat4 mat4_blend(const FxMat4& a, const FxMat4& b, Fx t) {
    const FxVec3 at = a.w + (b.w - a.w) * t;
    const FxQuat q =
        quat_slerp(quat_from_mat3(mat4_basis(a)), quat_from_mat3(mat4_basis(b)), t);
    return mat4_from(mat3_from_quat(q), at);
}
