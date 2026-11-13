#pragma once
// The fighters family: which assets make up a fighter and how fighter_assets_load 0x817E3 and
// actor_part_batches_build 0x78185 pick them, to data/tables/fighter_assets.sexp: model, paint,
// weapon block and motion asset ids by class, articulation graph roots (nodes in
// data/tables/sv_graphs.sexp, the skeletons family), motion timing, the parts' class fields and
// the Scoobee builds (rumble_scoobee_loadouts).
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_fighters(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
