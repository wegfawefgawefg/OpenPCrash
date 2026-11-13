#pragma once
// The stage-props family: the stage props, supply craft and sky craft routes, crate and craft
// models, ambience points, the night sky's moon, and the gameplay random table, to
// data/tables/stage_props.sexp and data/tables/rand_table.sexp.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_stage_props(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
