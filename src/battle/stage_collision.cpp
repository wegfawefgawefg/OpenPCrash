#include "battle/stage_collision.hpp"

namespace {

constexpr int dim_x = 32, dim_y = 4, dim_z = 32;
// the segment walks' grid step, 5 units, and the 2.5 the original adds to every part of the walked
// point (u_052b10.c: count * step + from + 2.5)
constexpr Fx walk_step = fx(5);
constexpr Fx walk_bias = fx_f(2.5);
constexpr Fx walk_limit = fx(10000); // longer segments are given up on
// a face steeper than this is a floor (collision_face_test and the slide: > 0.75; the
// segment contact: >= 0.8)
constexpr Fx floor_normal_y = fx_f(0.75);
constexpr Fx contact_floor_normal_y = fx_f(0.8);
constexpr Fx wall_lift = fx_f(0.1);      // a sphere off a wall sits 0.1 further out
constexpr Fx bounce_floor = fx_f(0.001); // a bounce under this is none
constexpr Fx crossing_margin = fx_f(0.05);
constexpr Fx frame_lift = fx_f(0.05); // collision_tri_segment_frame's point above the plane

struct Cell {
    int x = 0, y = 0, z = 0;
    bool inside = false;
};

// the cell of a port-frame point, bias added to every part of its original-frame point
Cell cell_of(const StageCollision& grid, FxVec3 p, Fx bias) {
    Cell c;
    c.x = grid_cell(p.x + bias, grid.centre_original.x, grid.cell[0], dim_x);
    c.y = grid_cell(p.y + bias, grid.centre_original.y, grid.cell[1], dim_y);
    c.z = grid_cell(-p.z + bias, grid.centre_original.z, grid.cell[2], dim_z);
    c.inside = c.x >= 0 && c.x < dim_x && c.y >= 0 && c.y < dim_y && c.z >= 0 && c.z < dim_z;
    return c;
}
int first_ref(const StageCollision& grid, Cell c) {
    return grid.cells[(c.x * dim_y + c.y) * dim_z + c.z];
}
int first_floor_ref(const StageCollision& grid, Cell c) {
    return grid.floor_cells[c.x * dim_z + c.z];
}
// each triangle of a cell's run; f returns false to stop
template <typename F>
void each_ref(const std::vector<StageTriangle>& tris, const std::vector<int>& refs, int first,
              F f) {
    for (int r = first; r >= 0 && r < static_cast<int>(refs.size()) && refs[r] != -1; ++r) {
        if (refs[r] >= 0 && refs[r] < static_cast<int>(tris.size()) && !f(tris[refs[r]])) {
            return;
        }
    }
}
int surface(const StageCollision& grid, int type) {
    const int index = type % 1000;
    return index >= 0 && index < 11 ? static_cast<int>(grid.surface_bits[index]) : 0;
}
Fx bounced(Fx into, Fx bounce) {
    const Fx back = fx_mul_near(fx_abs(into), bounce);
    return back < bounce_floor ? Fx{} : back;
}

// decomp: collision_face_test 0x54BC5
int face_test(const StageCollision& grid, const StageTriangle& t, SphereQuery& s) {
    const int contact = t.tri.normal.y > floor_normal_y ? contact_floor : contact_wall;
    const FxTriFrame frame = tri_frame(t.tri, s.centre);
    if (frame.height >= s.radius || frame.height < Fx{} || !frame_inside(frame)) {
        return 0;
    }
    // the part of the velocity into the face comes back times the bounce
    const Fx into = dot(t.tri.normal, s.velocity);
    if (into < Fx{}) {
        s.velocity = replace_along(s.velocity, t.tri.normal, bounced(into, s.bounce));
    }
    const Fx height = contact & contact_wall ? s.radius + wall_lift : s.radius;
    s.centre = tri_point_at_height(t.tri, s.centre, height);
    return contact | surface(grid, t.type) | contact_face;
}

// the first edge the centre is within a radius of, between its
// ends: pushed out square to it, the velocity's part toward it dropped
// decomp: collision_tri_edge_push 0x5581D
bool edge_push(const StageTriangle& t, SphereQuery& s) {
    const FxVec3 corners[3] = {t.tri.v0, t.tri.v1, t.tri.v2};
    for (int i = 0; i < 3; ++i) {
        const FxVec3 a = corners[i], b = corners[(i + 1) % 3];
        const std::int64_t size = length_q32(b - a);
        const std::int64_t along = dot_q32(s.centre - a, b - a);
        if (size == 0 || along < 0 || along > size) {
            continue;
        }
        const FxVec3 off = s.centre - closest_on_segment(a, b, s.centre);
        // on the edge's line the original's frame has no out direction and its inverse fails
        if (off == FxVec3{} || !shorter_than(off, s.radius)) {
            continue;
        }
        const FxVec3 out = normalized(off);
        s.centre = s.centre - off + out * s.radius;
        s.velocity = replace_along(s.velocity, out, Fx{});
        return true;
    }
    return false;
}

// the first corner within a radius moves the centre by
// (radius - distance) / radius of its offset (the original's arithmetic: not out to the radius);
// the velocity's part along the offset bounced
// decomp: collision_tri_corner_push 0x55AE8
bool corner_push(const StageTriangle& t, SphereQuery& s) {
    const FxVec3 corners[3] = {t.tri.v0, t.tri.v1, t.tri.v2};
    for (const FxVec3& corner : corners) {
        const FxVec3 off = s.centre - corner;
        if (off == FxVec3{} || !shorter_than(off, s.radius)) {
            continue;
        }
        const Fx share = fx_div_near(s.radius - length(off), s.radius);
        s.centre += FxVec3{fx_mul_near(off.x, share), fx_mul_near(off.y, share),
                           fx_mul_near(off.z, share)};
        const FxVec3 out = normalized(off);
        s.velocity = replace_along(s.velocity, out, bounced(dot(out, s.velocity), s.bounce));
        return true;
    }
    return false;
}

// The grid walks of the segment queries: cells along from -> to in 5-unit steps, a cell visited
// twice in a row tested once (the slide tests it again). visit returns true when it found
// something, which ends the walk; a cell off the grid ends it with nothing
enum class WalkLimit {
    fifths, // (count - 1) < length / 5: contact and hit
    frame,  // count < length / 2.5 * 2
    slide,  // count < length, as written (it walks on well past the end)
};
template <typename Visit>
void walk_cells(const StageCollision& grid, FxVec3 from, FxVec3 to, WalkLimit limit,
                Visit visit) {
    const FxVec3 path = to - from;
    const Fx size = length(path);
    if (size > walk_limit) {
        return;
    }
    const FxVec3 step = scaled_to(path, walk_step);
    const std::int64_t size_raw = size.raw;
    Cell last{-1, -1, -1, false};
    for (std::int64_t count = 0;; ++count) {
        const bool more = limit == WalkLimit::fifths ? (count - 1) * 5 * fx_one_raw < size_raw
                          : limit == WalkLimit::frame ? count * 5 * fx_one_raw < size_raw * 4
                                                      : count * fx_one_raw < size_raw;
        if (!more) {
            return;
        }
        const FxVec3 at{fx_saturate(step.x.raw * count + from.x.raw),
                        fx_saturate(step.y.raw * count + from.y.raw),
                        fx_saturate(step.z.raw * count + from.z.raw)};
        const Cell c = cell_of(grid, at, walk_bias);
        if (!c.inside) {
            return;
        }
        if (limit != WalkLimit::slide && c.x == last.x && c.y == last.y && c.z == last.z) {
            continue;
        }
        last = c;
        if (visit(c)) {
            return;
        }
    }
}

// the crossing (from above unless both_sides, the triangle
// widened by 0.05), 0.05 above the plane; the surface bits, 0 for none
// decomp: collision_tri_segment_frame 0x563BC
int segment_frame_tri(const StageCollision& grid, const StageTriangle& t, FxVec3 from,
                      FxVec3 to, bool both_sides, FxVec3* point, Fx* distance) {
    FxSegmentHit hit;
    if (!segment_tri_hit(t.tri, from, to, both_sides, crossing_margin, &hit)) {
        return 0;
    }
    *point = hit.point + t.tri.normal * frame_lift;
    *distance = ::distance(*point, from);
    return surface(grid, t.type);
}

// the sign of a . (b x c), exact (the parts of b x c are whole in 64 bits, the sum in 128)
int triple_sign(FxVec3 a, FxVec3 b, FxVec3 c) {
    __extension__ using Wide = __int128;
    const std::int64_t x = static_cast<std::int64_t>(b.y.raw) * c.z.raw -
                           static_cast<std::int64_t>(b.z.raw) * c.y.raw;
    const std::int64_t y = static_cast<std::int64_t>(b.z.raw) * c.x.raw -
                           static_cast<std::int64_t>(b.x.raw) * c.z.raw;
    const std::int64_t z = static_cast<std::int64_t>(b.x.raw) * c.y.raw -
                           static_cast<std::int64_t>(b.y.raw) * c.x.raw;
    const Wide sum = static_cast<Wide>(a.x.raw) * x + static_cast<Wide>(a.y.raw) * y +
                     static_cast<Wide>(a.z.raw) * z;
    return sum > 0 ? 1 : sum < 0 ? -1 : 0;
}

// the segment crosses the plane, and its three edge-side
// tests pass. The original rejects a . ((e) x b) > 0 in its left-handed frame; mirrored, every
// triple product changes sign, so the port rejects < 0
// decomp: collision_tri_segment_slide 0x566D3
int segment_slide_tri(const StageCollision& grid, const StageTriangle& t, FxVec3 from,
                      FxVec3 to, SphereQuery& s, Fx* distance) {
    const FxTri& tri = t.tri;
    const Fx h_from = tri_plane_height(tri, from), h_to = tri_plane_height(tri, to);
    if (fx_sign(h_from) * fx_sign(h_to) > 0 || h_from == h_to) {
        return 0;
    }
    if (triple_sign(from - tri.v0, tri.v2 - tri.v0, to - tri.v0) < 0 ||
        triple_sign(from - tri.v2, tri.v1 - tri.v2, to - tri.v2) < 0 ||
        triple_sign(from - tri.v1, tri.v0 - tri.v1, to - tri.v1) < 0) {
        return 0;
    }
    // the crossing of the plane; the triple products above were the inside test, so the margin is
    // made wide enough to pass anything
    FxSegmentHit hit;
    if (!segment_tri_hit(tri, from, to, true, fx(30000), &hit)) {
        return 0;
    }
    *distance = ::distance(hit.point, from);
    s.centre = hit.point + tri.normal * s.radius;
    s.velocity = replace_along(s.velocity, tri.normal, Fx{});
    const int contact = tri.normal.y > floor_normal_y ? contact_floor : contact_wall;
    return contact | surface(grid, t.type) | contact_slide;
}

} // namespace

// decomp: collision_query_sphere 0x52B9F
int collision_query_sphere(const StageCollision& grid, SphereQuery& sphere) {
    const Cell c = cell_of(grid, sphere.centre, Fx{});
    if (!grid.loaded || !c.inside) {
        return 0;
    }
    const int first = first_ref(grid, c);
    int contact = 0;
    each_ref(grid.tris, grid.refs, first, [&](const StageTriangle& t) {
        contact |= face_test(grid, t, sphere);
        return true;
    });
    if (!(contact & contact_wall)) {
        each_ref(grid.tris, grid.refs, first, [&](const StageTriangle& t) {
            if (edge_push(t, sphere)) {
                contact |= surface(grid, t.type) | contact_edge;
            }
            return true;
        });
    }
    if (!(contact & contact_wall) && !(contact & contact_edge)) {
        each_ref(grid.tris, grid.refs, first, [&](const StageTriangle& t) {
            if (corner_push(t, sphere)) {
                contact |= surface(grid, t.type) | contact_corner;
            }
            return true;
        });
    }
    return contact;
}

int collision_prop_sphere(const StageCollision& grid, const FxTri (&tris)[12],
                          SphereQuery& sphere) {
    int contact = 0;
    for (const FxTri& tri : tris) {
        // the crossing's frame, moved to height radius and back: the centre radius + 0.05 over
        // the face, the velocity into it bounced
        FxSegmentHit hit;
        if (!segment_tri_hit(tri, sphere.previous, sphere.centre, false, crossing_margin, &hit)) {
            continue;
        }
        const Fx into = dot(tri.normal, sphere.velocity);
        if (into < Fx{}) {
            sphere.velocity =
                replace_along(sphere.velocity, tri.normal, bounced(into, sphere.bounce));
        }
        sphere.centre = tri_point_at_height(tri, sphere.centre, sphere.radius + frame_lift);
        contact |= contact_slide;
    }
    if (!contact) {
        for (const FxTri& tri : tris) {
            contact |= face_test(grid, StageTriangle{tri, 0}, sphere);
        }
    }
    if (!contact) {
        for (const FxTri& tri : tris) {
            if (edge_push(StageTriangle{tri, 0}, sphere)) {
                contact |= contact_edge;
            }
        }
    }
    if (!contact) {
        for (const FxTri& tri : tris) {
            if (corner_push(StageTriangle{tri, 0}, sphere)) {
                contact |= contact_corner;
            }
        }
    }
    return contact;
}

const StageTriangle* collision_surface_face(const StageCollision& grid, FxVec3 centre, Fx reach,
                                            std::uint32_t bit) {
    const Cell c = cell_of(grid, centre, Fx{});
    if (!grid.loaded || !c.inside) {
        return nullptr;
    }
    const StageTriangle* found = nullptr;
    each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
        if (!(static_cast<std::uint32_t>(surface(grid, t.type)) & bit)) {
            return true;
        }
        const FxTriFrame frame = tri_frame(t.tri, centre);
        if (frame.height < Fx{} || frame.height > reach || !frame_inside(frame)) {
            return true;
        }
        found = &t;
        return false;
    });
    return found;
}

// decomp: collision_floor_height 0x532B7
int collision_floor_height(const StageCollision& grid, FxVec3 position, FxVec3* normal,
                           Fx* height) {
    constexpr Fx none = fx(-1000);
    *height = none;
    Cell c = cell_of(grid, position, Fx{});
    c.inside = c.x >= 0 && c.x < dim_x && c.z >= 0 && c.z < dim_z; // y isn't tested
    if (!grid.loaded || grid.floor_tris.empty() || !c.inside) {
        return 0;
    }
    int hit = 0;
    each_ref(grid.floor_tris, grid.floor_refs, first_floor_ref(grid, c),
             [&](const StageTriangle& t) {
                 Fx at;
                 if (tri_height_at(t.tri, position.x, position.z, &at)) {
                     *height = at;
                     *normal = t.tri.normal;
                     hit |= surface(grid, t.type);
                 }
                 return hit == 0;
             });
    if (!hit) {
        *height = none;
    }
    return hit;
}

// decomp: collision_segment_contact 0x5363F
int collision_segment_contact(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point) {
    if (!grid.loaded || from == to) {
        return 0;
    }
    int bits = 0;
    walk_cells(grid, from, to, WalkLimit::fifths, [&](Cell c) {
        Fx nearest = fx(10000);
        each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
            // collision_tri_segment_contact: either side, no margin; the original measures
            // the distance in the triangle's skewed frame, the port the true one (only the
            // nearest of a cell's crossings is kept by it)
            FxSegmentHit hit;
            if (segment_tri_hit(t.tri, from, to, true, Fx{}, &hit)) {
                const int contact =
                    t.tri.normal.y >= contact_floor_normal_y ? contact_floor : contact_wall;
                bits |= contact | surface(grid, t.type) | contact_face;
                const Fx away = distance(hit.point, from);
                if (away < nearest) {
                    *point = hit.point;
                    nearest = away;
                }
            }
            return true;
        });
        return bits != 0;
    });
    return bits;
}

// decomp: collision_segment_hit 0x5394E
bool collision_segment_hit(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point,
                           bool both_sides) {
    if (!grid.loaded || from == to) {
        return false;
    }
    bool found = false;
    walk_cells(grid, from, to, WalkLimit::fifths, [&](Cell c) {
        // collision_tri_segment_hit: the floor list's first crossing, then the nearest of
        // the rest (which may replace it)
        FxSegmentHit hit;
        each_ref(grid.floor_tris, grid.floor_refs, first_floor_ref(grid, c),
                 [&](const StageTriangle& t) {
                     if (segment_tri_hit(t.tri, from, to, both_sides, crossing_margin, &hit)) {
                         found = true;
                         *point = hit.point;
                         return false;
                     }
                     return true;
                 });
        Fx nearest = fx(10000);
        each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
            if (segment_tri_hit(t.tri, from, to, both_sides, crossing_margin, &hit)) {
                found = true;
                const Fx away = distance(hit.point, from);
                if (away < nearest) {
                    *point = hit.point;
                    nearest = away;
                }
            }
            return true;
        });
        return found;
    });
    return found;
}

// decomp: collision_segment_frame 0x53D34
int collision_segment_frame(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point,
                            FxVec3* normal, bool both_sides) {
    if (!grid.loaded || from == to) {
        return 0;
    }
    int contact = 0;
    walk_cells(grid, from, to, WalkLimit::frame, [&](Cell c) {
        Fx nearest = fx(10000);
        each_ref(grid.floor_tris, grid.floor_refs, first_floor_ref(grid, c),
                 [&](const StageTriangle& t) {
                     FxVec3 at;
                     Fx away;
                     const int hit = segment_frame_tri(grid, t, from, to, both_sides, &at, &away);
                     if (!hit) {
                         return true;
                     }
                     contact = hit | contact_floor;
                     if (away < nearest) {
                         *point = at;
                         *normal = t.tri.normal;
                         nearest = away;
                     }
                     return false;
                 });
        each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
            FxVec3 at;
            Fx away;
            const int hit = segment_frame_tri(grid, t, from, to, both_sides, &at, &away);
            if (hit) {
                contact = hit | contact_wall;
                if (away < nearest) {
                    *point = at;
                    *normal = t.tri.normal;
                    nearest = away;
                }
            }
            return true;
        });
        return contact != 0;
    });
    return contact;
}

// decomp: collision_segment_normal_mid 0x54162
int collision_segment_normal_mid(const StageCollision& grid, FxVec3 from, FxVec3 to,
                                 FxVec3* normal) {
    const FxVec3 middle{fx_raw(static_cast<std::int32_t>(div_round(
                            static_cast<std::int64_t>(from.x.raw) + to.x.raw, 2))),
                        fx_raw(static_cast<std::int32_t>(div_round(
                            static_cast<std::int64_t>(from.y.raw) + to.y.raw, 2))),
                        fx_raw(static_cast<std::int32_t>(div_round(
                            static_cast<std::int64_t>(from.z.raw) + to.z.raw, 2)))};
    const Cell c = cell_of(grid, middle, Fx{});
    if (!grid.loaded || !c.inside) {
        return 0;
    }
    int contact = 0;
    Fx nearest = fx(10000);
    each_ref(grid.floor_tris, grid.floor_refs, first_floor_ref(grid, c),
             [&](const StageTriangle& t) {
                 FxVec3 at;
                 Fx away;
                 const int hit = segment_frame_tri(grid, t, from, to, false, &at, &away);
                 if (!hit) {
                     return true;
                 }
                 contact = hit | contact_floor;
                 if (away < nearest) {
                     *normal = t.tri.normal;
                     nearest = away;
                 }
                 return false;
             });
    nearest = fx(10000);
    each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
        FxVec3 at;
        Fx away;
        const int hit = segment_frame_tri(grid, t, from, to, false, &at, &away);
        if (hit) {
            contact = hit | contact_wall;
            if (away < nearest) {
                *normal = t.tri.normal;
                nearest = away;
            }
        }
        return true;
    });
    return contact;
}

// decomp: collision_segment_slide 0x54497
int collision_segment_slide(const StageCollision& grid, FxVec3 from, FxVec3 to,
                            SphereQuery& sphere) {
    if (!grid.loaded) {
        return 0;
    }
    int bits = 0;
    walk_cells(grid, from, to, WalkLimit::slide, [&](Cell c) {
        Fx nearest = fx(10000);
        each_ref(grid.tris, grid.refs, first_ref(grid, c), [&](const StageTriangle& t) {
            // each triangle tried on a copy of the sphere; the nearest crossing is kept
            SphereQuery trial = sphere;
            Fx away;
            const int hit = segment_slide_tri(grid, t, from, to, trial, &away);
            bits |= hit;
            if (bits && hit && away < nearest) {
                sphere.centre = trial.centre;
                sphere.velocity = trial.velocity;
                nearest = away;
            }
            return true;
        });
        return bits != 0;
    });
    return bits;
}
