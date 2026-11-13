#pragma once
// The stages family: what each stage selector (stage * 2 + night) loads and draws, to
// data/tables/stage_scene.sexp (src/data/stage_scene.c, stage_render_lods 0x6DFC0 family).
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_stages(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
