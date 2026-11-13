#include "glyphs.hpp"

#include "addresses.hpp"
#include "sjis.hpp"

#include <SDL3/SDL.h>
#include <cstdio>

namespace {

// one code point to UTF-8
void append_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | c >> 6));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | c >> 12));
        out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | c >> 18));
        out.push_back(static_cast<char>(0x80 | (c >> 12 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

// the colours -10..-18 switch to (wide_text_draw 0xBD6A0); -9 goes back to the command's colour
const std::uint32_t control_colours[9] = {0xFF66EEEE, 0xFFEE9500, 0xFFEE88EE,
                                          0xFF808080, 0xFF308080, 0xFF8E4500,
                                          0xFF66EE33, 0xFFEEEE66, 0xFF256584};

std::string control_markup(int token) {
    char text[32];
    switch (token) {
    case -2:
        return "\n";
    case -3:
        return "{mark-reset}"; // lets the next -4 draw its mark again
    case -4:
        return "{press}"; // the press mark (glyph 0x64); the rest waits for a press
    case -5:
        return "{player}"; // the player's alias (pilot_name_draw)
    case -6:
        return "{chip}"; // the battle chip's name (battle_chip_name_draw)
    case -7:
        return "{speaker-a}"; // a bark's pilot name (event_speaker_a)
    case -8:
        return "{speaker-b}"; // a bark's Scoobee name (event_speaker_b)
    case -9:
        return "{c}";
    default:
        break;
    }
    if (token <= -10 && token >= -18) {
        std::snprintf(text, sizeof text, "{c:%08x}", control_colours[-10 - token]);
    } else {
        std::snprintf(text, sizeof text, "{ctl:%d}", token);
    }
    return text;
}

} // namespace

std::string cp932_pair_to_utf8(std::uint16_t sjis) {
    const char pair[3] = {static_cast<char>(sjis >> 8), static_cast<char>(sjis & 0xFF), 0};
    char* utf8 = sjis_iconv_string("CP932", pair, 3);
    if (!utf8) {
        utf8 = sjis_iconv_string("SHIFT_JIS", pair, 3);
    }
    std::string out = utf8 ? utf8 : "";
    SDL_free(utf8);
    // a pair is one character; iconv may skip a bad lead byte and read the trail byte alone as
    // half-width katakana (U+FF61..U+FF9F, EF BD A1..EF BE 9F), or as ASCII
    const auto lead = out.empty() ? 0 : static_cast<std::uint8_t>(out[0]);
    const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    const bool half_width =
        out.size() == 3 && lead == 0xEF &&
        (static_cast<std::uint8_t>(out[1]) == 0xBE ||
         (static_cast<std::uint8_t>(out[1]) == 0xBD && static_cast<std::uint8_t>(out[2]) >= 0xA1));
    if (out.size() != length || lead < 0x80 || half_width) {
        return "";
    }
    return out;
}

bool load_glyph_table(const Xbe& xbe, GlyphTable& table, std::string& error) {
    table = GlyphTable{};
    // the Shift-JIS order: glyph i is pair i
    std::vector<std::uint8_t> pairs(xbe_at::font_glyph_count * 2);
    if (!xbe_read(xbe, xbe_at::font_sjis_order, pairs.data(), pairs.size())) {
        error = "font_sjis_order is in no section";
        return false;
    }
    table.glyphs.resize(xbe_at::font_glyph_count);
    for (int i = 0; i < xbe_at::font_glyph_count; ++i) {
        Glyph& glyph = table.glyphs[i];
        glyph.sjis = static_cast<std::uint16_t>(pairs[i * 2] << 8 | pairs[i * 2 + 1]);
        table.by_sjis.emplace(glyph.sjis, i);
    }

    // Unicode: font_unicode_order for the first glyphs (it ends with a 0), CP932 for the rest
    for (int i = 0; i < xbe_at::font_glyph_count; ++i) {
        const std::uint16_t unit = static_cast<std::uint16_t>(
            xbe_i16(xbe, xbe_at::font_unicode_order + static_cast<std::uint32_t>(i) * 2));
        if (unit == 0) {
            break;
        }
        append_utf8(table.glyphs[i].utf8, unit);
        table.unicode_count = i + 1;
    }
    for (int i = 0; i < xbe_at::font_glyph_count; ++i) {
        Glyph& glyph = table.glyphs[i];
        const std::string decoded = cp932_pair_to_utf8(glyph.sjis);
        if (i >= table.unicode_count) {
            glyph.utf8 = decoded;
            glyph.from_cp932 = true;
            if (decoded.empty()) {
                error = "glyph " + std::to_string(i) + " has no Unicode and no CP932 decoding";
                return false;
            }
        } else if (decoded != glyph.utf8) {
            table.disagreeing.push_back(i);
        }
    }

    // widths: low byte the left offset, high byte the drawn width (glyph_advance 0xBD665)
    for (int i = 0; i < xbe_at::glyph_width_count; ++i) {
        const std::uint16_t entry = static_cast<std::uint16_t>(
            xbe_i16(xbe, xbe_at::glyph_widths + static_cast<std::uint32_t>(i) * 2));
        table.glyphs[i].left = static_cast<std::int8_t>(entry & 0xFF);
        table.glyphs[i].advance = static_cast<std::int8_t>(entry >> 8);
    }
    return true;
}

std::vector<std::int16_t> read_tokens(const Xbe& xbe, std::uint32_t va) {
    std::vector<std::int16_t> tokens;
    for (std::uint32_t at = va; tokens.size() < 8192; at += 2) {
        std::int16_t token = 0;
        if (!xbe_read(xbe, at, &token, 2)) {
            return {};
        }
        if (token == -1) {
            return tokens;
        }
        tokens.push_back(token);
    }
    return {};
}

std::string tokens_to_utf8(const GlyphTable& table, const std::vector<std::int16_t>& tokens) {
    std::string text;
    for (const std::int16_t token : tokens) {
        if (token < 0) {
            text += control_markup(token);
        } else if (token < static_cast<int>(table.glyphs.size())) {
            text += table.glyphs[token].utf8;
        } else {
            text += "{glyph:" + std::to_string(token) + "}";
        }
    }
    return text;
}

std::string sjis_to_utf8(const GlyphTable& table, const std::string& sjis, int& missing) {
    // what the game draws: two bytes at a time, unknown pairs as glyph 0
    missing = 0;
    for (std::size_t i = 0; i < sjis.size(); i += 2) {
        const std::uint16_t pair = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(sjis[i]) << 8 |
            (i + 1 < sjis.size() ? static_cast<std::uint8_t>(sjis[i + 1]) : 0));
        missing += table.by_sjis.count(pair) ? 0 : 1;
    }

    // what it says: pairs through the font (CP932 for pairs the font lacks), single bytes as
    // Latin-1, so a stray ASCII space doesn't hide the rest
    std::string text;
    for (std::size_t i = 0; i < sjis.size();) {
        const auto lead = static_cast<std::uint8_t>(sjis[i]);
        const bool double_byte = (lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC);
        if (!double_byte || i + 1 >= sjis.size()) {
            append_utf8(text, lead);
            ++i;
            continue;
        }
        const std::uint16_t pair =
            static_cast<std::uint16_t>(lead << 8 | static_cast<std::uint8_t>(sjis[i + 1]));
        i += 2;
        const auto found = table.by_sjis.find(pair);
        if (found != table.by_sjis.end()) {
            text += table.glyphs[found->second].utf8;
            continue;
        }
        const std::string decoded = cp932_pair_to_utf8(pair);
        char unknown[24];
        std::snprintf(unknown, sizeof unknown, "{sjis:%04x}", pair);
        text += decoded.empty() ? unknown : decoded;
    }
    return text;
}

std::string narrow_for_reading(const std::string& utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<std::uint8_t>(utf8[i]);
        if (b == 0xEF && i + 2 < utf8.size()) {
            // U+FF01..U+FF5E are EF BC 81..EF BD 9E
            const std::uint32_t c =
                (b & 0x0F) << 12 | (utf8[i + 1] & 0x3F) << 6 | (utf8[i + 2] & 0x3F);
            if (c >= 0xFF01 && c <= 0xFF5E) {
                out.push_back(static_cast<char>(c - 0xFEE0));
                i += 3;
                continue;
            }
        }
        if (utf8.compare(i, 3, "\xE3\x80\x80") == 0) { // U+3000
            out.push_back(' ');
            i += 3;
            continue;
        }
        if (utf8.compare(i, 3, "\xE2\x80\x99") == 0) { // U+2019, the font's apostrophe
            out.push_back('\'');
            i += 3;
            continue;
        }
        if (utf8[i] == '\n') {
            out += " / ";
            ++i;
            continue;
        }
        out.push_back(utf8[i]);
        ++i;
    }
    return out;
}
