#pragma once
// The game's one text font as a table: glyph number -> Shift-JIS pair, Unicode, cell and advance
// (docs/port/data.md "Text and the font" and section 14). Event text is glyph numbers; table text
// is Shift-JIS turned into glyph numbers by searching font_sjis_order (sjis_to_glyphs 0xBDCA5).
// Both come out as UTF-8 here, with the control codes of wide_text_draw 0xBD6A0 as {markup}.
#include "xbe.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Glyph {
    std::uint16_t sjis = 0; // lead byte high
    std::string utf8;
    bool from_cp932 = false; // past font_unicode_order (glyph 2222 on): decoded from the pair
    int left = 0;            // glyph_widths low byte: where the drawn part starts in the cell
    int advance = 18;        // glyph_widths high byte; 0x184 and up are full 18
};

struct GlyphTable {
    std::vector<Glyph> glyphs;
    std::unordered_map<std::uint16_t, int> by_sjis;
    int unicode_count = 0;        // how many glyphs font_unicode_order names (2222)
    std::vector<int> disagreeing; // glyphs where font_unicode_order and CP932 differ
};

bool load_glyph_table(const Xbe& xbe, GlyphTable& table, std::string& error);

// one Shift-JIS pair to UTF-8 by CP932 (SDL_iconv); empty when it doesn't decode
std::string cp932_pair_to_utf8(std::uint16_t sjis);

// a -1-ended token stream at va (event text); empty on a bad read
std::vector<std::int16_t> read_tokens(const Xbe& xbe, std::uint32_t va);

// tokens to UTF-8: glyphs as their characters, controls as markup ("\n" for -2, {press},
// {player}, {chip}, {speaker-a}, {speaker-b}, {mark-reset}, {c} and {c:AARRGGBB}), anything else as
// {glyph:N} or {ctl:N}
std::string tokens_to_utf8(const GlyphTable& table, const std::vector<std::int16_t>& tokens);

// a Shift-JIS table string to UTF-8, pairs through font_sjis_order. missing counts what the game
// gets wrong: it reads two bytes a character and draws a pair the font lacks as glyph 0 (a space),
// so a lone ASCII byte blanks the rest of the string. The text keeps what the string says: CP932
// for pairs the font lacks, single bytes as Latin-1.
std::string sjis_to_utf8(const GlyphTable& table, const std::string& sjis, int& missing);

// full-width Latin, digits and the ideographic space to ASCII, for comments a person reads
std::string narrow_for_reading(const std::string& utf8);
