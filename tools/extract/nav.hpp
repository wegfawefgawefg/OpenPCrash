#pragma once
// The nav family: the stages' AI route graphs (registry kind 10, "ENPT" files 1687..1690, loaded by
// ai_nav_resource_select 0x57F2C and relocated by enpt_load 0x58136; decomp docs/port/data.md
// section 12, docs/port/ai.md "Routes") to data/nav/<id>_<name>.sexp. Each node keeps its table
// index, its own id byte, its kind and its links as node indices (the file's offsets resolved),
// and its spline: the start point, then three points a segment (two controls and the end), in the
// port frame (z negated). The manifest entry lists the stage selectors that load the graph
// (ai_nav_resource_selectors 0x20614C), so the cook packs it with each of those stages.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_nav(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
