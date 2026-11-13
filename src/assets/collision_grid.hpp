#pragma once
// A stage collision grid (CollisionGrid, decomp include/stage.h; data/collision/*.sexp from the
// extractor's collision family, cooked into the stage packs). Triangles are in the port frame; the
// cell arrays keep the original's indexing, so a query at (x, y, z) looks up cell (x, y, -z)
// (tools/extract/collision.hpp).
#include <cstdint>
#include <string>
#include <vector>

struct Pack;

struct CollisionTriangle {
    float v[3][3] = {};
    float normal[3] = {};  // the stored one: trust it over the winding
    std::int32_t type = 0; // surface type
};

struct CollisionGrid {
    int asset = -1;
    int dims[3] = {};
    float cell[3] = {};
    float centre_original[3] = {}; // the grid's own frame, z not negated
    std::vector<CollisionTriangle> triangles;
    std::vector<std::int32_t> cells; // first ref of each [x][y][z] cell, -1 none
    std::vector<std::int32_t> refs;  // triangle indices, each cell's run ended by -1
    std::vector<CollisionTriangle> floor_triangles;
    std::vector<std::int32_t> floor_cells; // [x][z]
    std::vector<std::int32_t> floor_refs;
    // collision_surface_bits by type % 1000 (types 0..10), all 0 from an extract before the
    // collision family wrote them
    std::uint32_t surface_bits[11] = {};
};

// the grid `asset` from a stage pack
bool load_collision_grid(const Pack& pack, int asset, CollisionGrid& out, std::string& error);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, CollisionGrid& g) {
    a(g.asset);
    a(g.dims);
    a(g.cell);
    a(g.centre_original);
    a(g.triangles);
    a(g.cells);
    a(g.refs);
    a(g.floor_triangles);
    a(g.floor_cells);
    a(g.floor_refs);
    a(g.surface_bits);
}
