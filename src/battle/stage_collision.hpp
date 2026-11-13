#pragma once
// A stage's collision grid (decomp include/stage.h CollisionGrid, docs/port/data.md section 11)
// and the queries the fighters and the camera make of it (src/od/u_052b10.c), in Q16.16 in the
// port's frame. Read from data/collision/<id>_<name>.sexp (the collision family): triangles in the
// port frame, the cell arrays in the original's indexing, so a point (x, y, z) looks up the cell
// of (x, y, -z) (docs/assets.md).
//
// Every test here is a geometric one (heights along a normal, places inside a triangle, distances
// from edges), which the z mirror leaves alone; the one sign test that flips (the slide's triple
// products) says so where it is.
#include "fx/hit.hpp"
#include "fx/vec.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct StageTriangle {
    FxTri tri;
    int type = 0; // surface type, read as type % 1000
};

struct StageCollision {
    Fx cell[3];             // cell size (the archive read it as an origin)
    FxVec3 centre_original; // the grid's centre in the original's frame (z not negated)
    std::vector<StageTriangle> tris, floor_tris;
    std::vector<int> cells;       // [x][y][z], 32 x 4 x 32: first ref
    std::vector<int> refs;        // runs of triangle indices, each ended by -1
    std::vector<int> floor_cells; // [x][z], 32 x 32
    std::vector<int> floor_refs;
    std::uint32_t surface_bits[11] = {}; // collision_surface_bits by type % 1000
    bool loaded = false;
};

// The sphere record the queries share (sphere_query_construct and its users): centre +0,
// previous centre +0xC, velocity +0x18, radius +0x24, bounce +0x28. Friction (+0x2C) is read only
// by queries the fighters don't make.
struct SphereQuery {
    FxVec3 centre;
    FxVec3 previous;
    FxVec3 velocity;
    Fx radius;
    Fx bounce;
};

// Contact bits (docs/findings.md): 1 face, 2 edge, 4 corner, 8 floor, 0x10 wall, 0x20 slide, and
// the triangle's surface bit
inline constexpr int contact_face = 1;
inline constexpr int contact_edge = 2;
inline constexpr int contact_corner = 4;
inline constexpr int contact_floor = 8;
inline constexpr int contact_wall = 0x10;
inline constexpr int contact_slide = 0x20;
// surface bits (collision_surface_bits by the triangle's type % 1000) the code reads
inline constexpr int surface_landing_b = 0x800;  // bank 3's landing_surface_b
inline constexpr int surface_landing_a = 0x1400; // landing_surface_a
inline constexpr int surface_edge_wall = 0x2000; // type 8, the stages' edge walls: sparks, a decal
inline constexpr int surface_prop = 0x20000;     // a prop's faces
// the surfaces a hit leaves no scorch mark on
inline constexpr int surface_unmarked = 0x10000 | 0x4000 | 0x400 | surface_prop | surface_edge_wall;

// collision_query_sphere: the sphere pushed out of the faces in its cell, then (no wall
// face touched) out of their edges, then (no edge either) their corners; the velocity bounced
int collision_query_sphere(const StageCollision& grid, SphereQuery& sphere);
// stage_props_collide_sphere's tests of one prop's twelve triangles, whose surface type is 0 (the
// prop pool's zeroed words: prop_init never writes it): each one the sphere's path crosses from
// above (collision_tri_segment_frame) puts the centre a radius over the crossing, which sits 0.05
// over the face, and bounces the velocity (0x20); with none, the faces; with none, the edges (2);
// with none, the corners (4). The contact bits, 0 for none
int collision_prop_sphere(const StageCollision& grid, const FxTri (&tris)[12],
                          SphereQuery& sphere);
// the first triangle of the sphere's cell carrying surface `bit` whose face the centre is within
// `reach` of (above its plane, inside it): the face collision_face_test would have met, for
// presentation (the contact decal's frame, which the world doesn't keep). Changes nothing
const StageTriangle* collision_surface_face(const StageCollision& grid, FxVec3 centre, Fx reach,
                                            std::uint32_t bit);
// collision_floor_height: the first floor triangle under (x, z) in the floor grid; its
// height and normal. 0 and -1000 when none
int collision_floor_height(const StageCollision& grid, FxVec3 position, FxVec3* normal,
                           Fx* height);
// collision_segment_contact: the nearest crossing of the wall-and-everything triangles
// along the segment, walked through the grid in 5-unit steps
int collision_segment_contact(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point);
// collision_segment_hit: the floor list first, then the rest; the crossing within the
// triangle widened by 0.05; from above the plane unless both_sides
bool collision_segment_hit(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point,
                           bool both_sides);
// collision_segment_frame: as segment_hit, giving the crossing 0.05 above the plane and
// the triangle's normal (the rows 3 and 1 of the original's matrix)
int collision_segment_frame(const StageCollision& grid, FxVec3 from, FxVec3 to, FxVec3* point,
                            FxVec3* normal, bool both_sides);
// collision_segment_normal_mid: the normal of the nearest triangle the segment crosses in
// the one cell at its midpoint
int collision_segment_normal_mid(const StageCollision& grid, FxVec3 from, FxVec3 to,
                                 FxVec3* normal);
// collision_segment_slide: the segment from -> to against the triangles it passes; on a
// crossing the sphere is put on the plane, lifted by its radius, its velocity laid along the plane
int collision_segment_slide(const StageCollision& grid, FxVec3 from, FxVec3 to,
                            SphereQuery& sphere);
