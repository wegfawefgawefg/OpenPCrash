#pragma once
// The scripts family: every event script (EVENTSEG conversations, barks, artist bios, maker talks)
// as a readable .sexp under data/scripts/ (docs/text-and-scripts.md): commands by the names of
// the decomp's docs/port/progression.md, text as ids into data/text/, page labels where each
// frame's walk restarts.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_scripts(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
