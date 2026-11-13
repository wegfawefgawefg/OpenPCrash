#pragma once
// The effects family: the particle records with their curves, the effect lists (effect_lists),
// the effect sets (effect_sets), the sprite effect kinds and the lens flare's pieces, to
// data/tables/effects.sexp (decomp docs/port/effects.md, src/data/particles.c, ui_d.c,
// stage_routes.c, stage_props.c). Kept in the original's frame: the effects code runs in it.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_effects(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
