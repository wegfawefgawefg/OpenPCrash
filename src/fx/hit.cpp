#include "fx/hit.hpp"

#include <bit>

namespace {

constexpr std::int64_t magnitude(std::int64_t value) {
    return value < 0 ? -value : value;
}
constexpr int sign_of(std::int64_t value) {
    return value > 0 ? 1 : value < 0 ? -1 : 0;
}

// numerator / denominator as an Fx (denominator > 0), both shifted down first when the numerator
// is too big to scale by 2^16 in 64 bits; saturated
Fx ratio_fx(std::int64_t numerator, std::int64_t denominator) {
    const int over =
        std::bit_width(static_cast<std::uint64_t>(magnitude(numerator))) + fx_shift - 62;
    if (over > 0) {
        numerator = shift_round(numerator, over);
        denominator = shift_round(denominator, over);
    }
    if (denominator == 0) {
        return fx_saturate(numerator >= 0 ? fx_raw_max : fx_raw_min);
    }
    return fx_saturate(div_round(numerator * fx_one_raw, denominator));
}

// a + (b - a) * numerator / denominator, part by part in one rounding (0 <= numerator <=
// denominator, denominator > 0): the denominator brought under 2^32 for a Q30 fraction
FxVec3 point_along(FxVec3 a, FxVec3 b, std::int64_t numerator, std::int64_t denominator) {
    const int over = std::bit_width(static_cast<std::uint64_t>(denominator)) - 32;
    if (over > 0) {
        numerator >>= over;
        denominator >>= over;
    }
    const std::int64_t fraction = div_round(numerator << 30, denominator);
    const FxVec3 ab = b - a;
    const auto part = [&](Fx start, Fx step) {
        return fx_saturate(start.raw + shift_round(step.raw * fraction, 30));
    };
    return {part(a.x, ab.x), part(a.y, ab.y), part(a.z, ab.z)};
}

// numerator (Q32) over the root of squared (Q32), an Fx: the root taken with as many extra bits
// as the numerator leaves room for
Fx over_root(std::int64_t numerator, std::int64_t squared) {
    const int root_room = (62 - std::bit_width(static_cast<std::uint64_t>(squared))) / 2;
    const int numerator_room =
        62 - std::bit_width(static_cast<std::uint64_t>(magnitude(numerator)));
    int extra = root_room < numerator_room ? root_room : numerator_room;
    extra = extra < 0 ? 0 : extra;
    const std::int64_t root =
        static_cast<std::int64_t>(isqrt_round(static_cast<std::uint64_t>(squared) << (2 * extra)));
    if (root == 0) {
        return Fx{};
    }
    return fx_saturate(div_round(numerator << extra, root));
}

// the two coordinates the triangle keeps when seen along its normal's largest part
struct Flat {
    std::int64_t a, b;
};
Flat drop_axis(FxVec3 v, int axis) {
    if (axis == 0) {
        return {v.y.raw, v.z.raw};
    }
    if (axis == 1) {
        return {v.z.raw, v.x.raw};
    }
    return {v.x.raw, v.y.raw};
}
std::int64_t flat_cross(Flat p, Flat q) {
    return p.a * q.b - p.b * q.a;
}

} // namespace

Fx tri_plane_height(const FxTri& tri, FxVec3 p) {
    return dot(tri.normal, p - tri.v0);
}

FxTriFrame tri_frame(const FxTri& tri, FxVec3 p) {
    FxTriFrame frame{};
    frame.height = tri_plane_height(tri, p);
    // drop the point onto the plane along the normal, as the original's inverse frame does, then
    // read the area ratios seen along the normal's largest part (exact: every product is under
    // 2^51 for a stage of 530 units)
    const FxVec3 on_plane = p - tri.normal * frame.height;
    const std::int64_t nx = magnitude(tri.normal.x.raw), ny = magnitude(tri.normal.y.raw),
                       nz = magnitude(tri.normal.z.raw);
    const int axis = nx >= ny && nx >= nz ? 0 : ny >= nz ? 1 : 2;
    const Flat e0 = drop_axis(tri.v2 - tri.v0, axis);
    const Flat e1 = drop_axis(tri.v1 - tri.v0, axis);
    const Flat d = drop_axis(on_plane - tri.v0, axis);
    // d = u e0 + w e1, by Cramer's rule
    frame.den = flat_cross(e0, e1);
    frame.u_num = flat_cross(d, e1);
    frame.w_num = flat_cross(e0, d);
    if (frame.den < 0) {
        frame.den = -frame.den;
        frame.u_num = -frame.u_num;
        frame.w_num = -frame.w_num;
    }
    return frame;
}

bool frame_inside(const FxTriFrame& frame) {
    return frame.den > 0 && frame.u_num >= 0 && frame.w_num >= 0 &&
           frame.u_num + frame.w_num <= frame.den;
}

Fx frame_u(const FxTriFrame& frame) {
    if (frame.den == 0) {
        return Fx{};
    }
    return ratio_fx(frame.u_num, frame.den);
}

Fx frame_w(const FxTriFrame& frame) {
    if (frame.den == 0) {
        return Fx{};
    }
    return ratio_fx(frame.w_num, frame.den);
}

FxVec3 tri_point_at_height(const FxTri& tri, FxVec3 p, Fx height) {
    return p + tri.normal * (height - tri_plane_height(tri, p));
}

FxVec3 replace_along(FxVec3 v, FxVec3 n, Fx value) {
    return v + n * (value - dot(n, v));
}

// decomp: collision_point_in_tri_xz 0x56C07
bool point_in_tri_xz(const FxTri& tri, Fx x, Fx z) {
    // the original's test as it stands: d = p - c0, e1 = c1 - c0, e2 = c2 - c0, and
    // cross(d, e1) cross(e1, e2) <= 0, cross(d, e1) cross(d, e2) <= 0,
    // |cross(d, e1)| + |cross(d, e2)| <= |cross(e1, e2)|, on exact crosses and their signs
    const FxVec2 d{x - tri.v0.x, z - tri.v0.z};
    const FxVec2 e1 = flat(tri.v1 - tri.v0);
    const FxVec2 e2 = flat(tri.v2 - tri.v0);
    const std::int64_t cross0 = cross_q32(d, e1);
    const std::int64_t cross2 = cross_q32(d, e2);
    const std::int64_t cross3 = cross_q32(e1, e2);
    return sign_of(cross0) * sign_of(cross3) <= 0 && sign_of(cross0) * sign_of(cross2) <= 0 &&
           magnitude(cross0) + magnitude(cross2) <= magnitude(cross3);
}

// decomp: collision_tri_height 0x55456
bool tri_height_at(const FxTri& tri, Fx x, Fx z, Fx* height) {
    if (!point_in_tri_xz(tri, x, z)) {
        return false;
    }
    // ((v0.x - x) n.x + (v0.z - z) n.z) / n.y + v0.y, the sum kept whole
    const std::int64_t across = static_cast<std::int64_t>((tri.v0.x - x).raw) * tri.normal.x.raw +
                                static_cast<std::int64_t>((tri.v0.z - z).raw) * tri.normal.z.raw;
    if (tri.normal.y.raw == 0) {
        *height = fx_saturate(across >= 0 ? fx_raw_max : fx_raw_min);
        return true;
    }
    *height = fx_saturate(div_round(across, tri.normal.y.raw) + tri.v0.y.raw);
    return true;
}

bool segment_tri_hit(const FxTri& tri, FxVec3 from, FxVec3 to, bool both_sides, Fx margin,
                     FxSegmentHit* hit) {
    const Fx height_from = tri_plane_height(tri, from);
    const Fx height_to = tri_plane_height(tri, to);
    if (!both_sides && height_from.raw <= 0) {
        return false;
    }
    if (sign_of(height_from.raw) * sign_of(height_to.raw) > 0) {
        return false;
    }
    // both ends on the plane: the original divides 0 by 0 and every test after fails
    if (height_from == height_to) {
        return false;
    }
    std::int64_t drop = static_cast<std::int64_t>(height_from.raw) - height_to.raw;
    std::int64_t fallen = height_from.raw;
    if (drop < 0) {
        drop = -drop;
        fallen = -fallen;
    }
    const FxVec3 point = point_along(from, to, fallen, drop);
    const FxTriFrame frame = tri_frame(tri, point);
    if (margin.raw == 0) {
        if (!frame_inside(frame)) {
            return false;
        }
    } else {
        // margin / |edge| in each edge's own units, as the original's 0.05 / length; summed in
        // 64 bits, as u and w far outside saturate
        const std::int64_t u = frame_u(frame).raw, w = frame_w(frame).raw;
        const std::int64_t margin_u = fx_div_near(margin, length(tri.v2 - tri.v0)).raw;
        const std::int64_t margin_w = fx_div_near(margin, length(tri.v1 - tri.v0)).raw;
        if (!(u - margin_u + w - margin_w <= fx_one_raw && u + margin_u >= 0 &&
              w + margin_w >= 0)) {
            return false;
        }
    }
    hit->point = tri_point_at_height(tri, point, Fx{});
    hit->along = ratio_fx(fallen, drop);
    return true;
}

Fx segment_param(FxVec3 a, FxVec3 b, FxVec3 p) {
    const std::int64_t size = length_q32(b - a);
    if (size == 0) {
        return Fx{};
    }
    return ratio_fx(dot_q32(p - a, b - a), size);
}

FxVec3 closest_on_segment(FxVec3 a, FxVec3 b, FxVec3 p) {
    const std::int64_t size = length_q32(b - a);
    const std::int64_t along = dot_q32(p - a, b - a);
    if (size == 0 || along <= 0) {
        return a;
    }
    if (along >= size) {
        return b;
    }
    return point_along(a, b, along, size);
}

// decomp: swept_segment_vs_sphere 0x9856A
bool segment_sphere_entry(FxVec3 from, FxVec3 to, FxVec3 centre, Fx radius, FxVec3* hit) {
    const std::int64_t reach = static_cast<std::int64_t>(radius.raw) * radius.raw;
    const FxVec3 path = to - from;
    const FxVec3 to_centre = centre - from;
    const std::int64_t size = length_q32(path);
    if (size == 0) {
        if (length_q32(to_centre) <= reach) {
            *hit = from;
            return true;
        }
        return false;
    }
    // how far along the path the centre is, and how far off the line: |to_centre x path| / |path|,
    // as the original takes it (the cross's parts are exact; each is divided by the length before
    // squaring, so a graze is as good as a hit square on)
    const std::int64_t along_q32 = dot_q32(to_centre, path);
    const Fx along = over_root(along_q32, size);
    const auto off_part = [&](Fx a, Fx b, Fx c, Fx e) {
        const Fx part = over_root(static_cast<std::int64_t>(a.raw) * b.raw -
                                      static_cast<std::int64_t>(c.raw) * e.raw,
                                  size);
        return static_cast<std::int64_t>(part.raw) * part.raw;
    };
    const std::int64_t off_line = off_part(to_centre.y, path.z, to_centre.z, path.y) +
                                  off_part(to_centre.z, path.x, to_centre.x, path.z) +
                                  off_part(to_centre.x, path.y, to_centre.y, path.x);
    // the segment's nearest approach: an end, or square to the path between them
    std::int64_t nearest = 0;
    if (along_q32 <= 0) {
        nearest = length_q32(to_centre);
    } else if (dot_q32(centre - to, from - to) <= 0) {
        nearest = distance_q32(centre, to);
    } else {
        nearest = off_line;
    }
    if (nearest > reach) {
        return false;
    }
    // the line's way in, or out when the way in is behind from
    const Fx half_chord = fx_sqrt_q32(reach - off_line);
    const Fx entry = along - half_chord >= Fx{} ? along - half_chord : along + half_chord;
    *hit = from + scaled_to(path, entry);
    return true;
}

int grid_cell(Fx p, Fx centre, Fx cell, int dim) {
    return fx_trunc_div(cell * (dim / 2) + p - centre, cell);
}
