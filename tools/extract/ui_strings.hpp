#pragma once
// The game's Shift-JIS strings (names, labels, shop and menu text, message boxes, subtitles):
// the named tables row by row, then every other displayable string in .rdata and .data by its
// address. Displayable means whole Shift-JIS pairs, since sjis_to_glyphs 0xBDCA5 reads two bytes
// a character; the US release writes its English full-width.
#include "glyphs.hpp"
#include "xbe.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct UiString {
    std::string id;    // tournament.12.name (a table's row and field) or str.19b41c (its address)
    std::string table; // the table's name, empty for loose strings
    std::uint32_t va = 0;
    std::string text; // UTF-8
    int missing = 0;  // pairs the game can't find in the font and draws as glyph 0, a space
    std::vector<std::string> used_by; // loose strings: (data 0x...) and (code 0x...) referrers
};

// tables first in table order, then loose strings by address
bool collect_ui_strings(const Xbe& xbe, const GlyphTable& glyphs, std::vector<UiString>& out,
                        std::string& error);
