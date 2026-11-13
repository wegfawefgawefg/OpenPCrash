#pragma once
// PC CONFIG's letters, cut in the cook from the original's label, number and button PNGs
// (docs/settings.md, "The look"); the game reads the cuts from the core pack.
#include <cstdint>
#include <string>
#include <vector>

// the cuts' name in the core pack
inline constexpr const char* pc_art_path = "pc_art.sexp";

// the cuts as sexp text: (font NAME (baseline B) (space S)), (glyph NAME CHAR SPRITE X0 X1 BASELINE
// HEIGHT) for the fonts "thin" and "button", (lit SPRITE LIT) a button and its lit twin; sources
// gets the PNGs read, relative to data_dir
bool cook_pc_art(const std::string& data_dir, std::vector<std::uint8_t>& out,
                 std::vector<std::string>& sources, std::string& error);
