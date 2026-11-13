#pragma once
// The text family: every string the game shows as UTF-8 under data/text/ (docs/text-and-scripts.md)
// and the font: the atlas as font.png and font.sexp mapping glyph number -> Unicode, cell, advance.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_text(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
