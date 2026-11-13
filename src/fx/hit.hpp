#pragma once
// Geometry tests for the stage collision and the shots, in the forms the original's queries use
// (decomp src/od/u_052b10.c, swept_segment_vs_sphere, the contact functions in
// src/od/u_0b7ea0.c). The original builds a matrix per triangle (rows v2 - v0, normal, v1 - v0,
// origin v0), inverts it and reads a point's height and its place along the two edges; here the
// same numbers come from the plane and exact 64-bit area ratios, and "move to height r and
// transform back" is a move along the normal (tri_point_at_height, replace_along).
//
// The original has no continuous swept-sphere-against-triangle test: a fighter's sphere is tested
// where it ends up, its centre's path is tested as a segment (collision_segment_slide), and the
// fighters' sweep against each other is a closest point on their relative motion. Those are what
// is here.
#include "fx/vec.hpp"

#include <cstdint>

// a collision triangle (include/stage.h CollisionTri) without its surface type
struct FxTri {
    FxVec3 v0, v1, v2, normal;
};

// A point in a triangle's frame: its height above the plane along the normal, and the point
// dropped onto the plane as u along v2 - v0 and w along v1 - v0, kept as exact fractions
// u_num / den and w_num / den (den > 0; 0 for a triangle with no area)
struct FxTriFrame {
    Fx height;
    std::int64_t u_num, w_num, den;
};

// height of p above the triangle's plane (dot(normal, p - v0))
Fx tri_plane_height(const FxTri& tri, FxVec3 p);
FxTriFrame tri_frame(const FxTri& tri, FxVec3 p);
// u >= 0, w >= 0 and u + w <= 1, exactly (the original's out.x + out.z <= 1 test)
bool frame_inside(const FxTriFrame& frame);
// u and w as Fx, saturated, for tests with margins
Fx frame_u(const FxTriFrame& frame);
Fx frame_w(const FxTriFrame& frame);
// p moved along the normal to the given height above the plane
FxVec3 tri_point_at_height(const FxTri& tri, FxVec3 p, Fx height);
// v with its part along the unit vector n set to value (the original's transform_normal into a
// frame, change .y, transform back: a bounce, or a velocity laid along a wall)
FxVec3 replace_along(FxVec3 v, FxVec3 n, Fx value);

// collision_point_in_tri_xz: (x, z) inside the triangle seen from above, edges included
bool point_in_tri_xz(const FxTri& tri, Fx x, Fx z);
// collision_tri_height: the plane's height at (x, z) when (x, z) is inside the triangle
// seen from above. A wall's (normal.y 0) saturates
bool tri_height_at(const FxTri& tri, Fx x, Fx z, Fx* height);

// A segment through a triangle (collision_tri_segment_hit and its kin): from must be
// above the plane unless both_sides, the ends on opposite sides or on it, and the crossing inside
// the triangle widened by margin along each edge (the original's 0.05). point is on the plane;
// along is how far from `from` toward `to`, 0..1
struct FxSegmentHit {
    FxVec3 point;
    Fx along;
};
bool segment_tri_hit(const FxTri& tri, FxVec3 from, FxVec3 to, bool both_sides, Fx margin,
                     FxSegmentHit* hit);

// where p falls along a -> b, unclamped (0 at a, 1 at b; saturated far off), and the nearest
// point of the segment to p
Fx segment_param(FxVec3 a, FxVec3 b, FxVec3 p);
FxVec3 closest_on_segment(FxVec3 a, FxVec3 b, FxVec3 p);

// swept_segment_vs_sphere: a shot moving from -> to against a sphere. It hits when the
// segment comes within radius of the centre; the hit point is where the line enters the sphere,
// or where it leaves when it enters behind from (as the original's root choice gives). A zero
// segment hits when from is within radius
bool segment_sphere_entry(FxVec3 from, FxVec3 to, FxVec3 centre, Fx radius, FxVec3* hit);

// the original's grid cell along one axis: (int)((dim / 2 * cell + p - centre) / cell),
// truncated toward zero as its float cast is (so -0.5 cells is cell 0); the caller checks 0..dim-1
int grid_cell(Fx p, Fx centre, Fx cell, int dim);
