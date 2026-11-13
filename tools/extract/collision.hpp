#pragma once
// The collision family: the stage collision grids (registry kind 1, assets 379..382; CollisionGrid
// in the decomp's include/stage.h, docs/port/data.md section 11) to
// data/collision/<id>_<name>.sexp: the wall-and-everything triangle list and the floor list with
// their per-triangle surface type, in the port frame (z negated, normals too, so each triangle's
// winding is mirrored and its stored normal is the one to trust), and the original's cell grids and
// reference runs as they are. The grids index cells in the original's frame: a port query at (x, y,
// z) looks up (x, y, -z).
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_collision(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
