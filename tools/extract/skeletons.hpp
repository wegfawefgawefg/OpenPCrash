#pragma once
// The skeletons family: the SV articulation graphs (SvNode, 0x38, include/tables.h) reachable from
// the leg and arm root tables, to data/tables/sv_graphs.sexp, one (node ...) per node keyed by its
// XBE address. translate and rotate are what the static path of sv_pose_walk 0xAA419 applies,
// put in the port frame: it applies (tx, ty, -tz) and (-rx, -ry, rz), and the port frame negates z,
// so both come out as the stored words. Dynamic weapon slots of type 7 nodes are written "weapon".
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_skeletons(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
