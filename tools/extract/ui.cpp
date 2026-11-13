#include "ui.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sjis.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <map>

namespace {

// the font's glyph order (src/data/misc_e.c): glyph i is Shift-JIS pair i of font_sjis_order and,
// for the first 2222, UTF-16 unit i of font_unicode_order
constexpr std::uint32_t font_sjis_order_va = 0x27DE38;
constexpr std::uint32_t font_unicode_order_va = 0x27F030;
constexpr int font_unicode_glyphs = 2222;

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | code >> 6));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | code >> 12));
        out.push_back(static_cast<char>(0x80 | (code >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

const std::map<std::uint16_t, std::uint16_t>& font_unicode(const Xbe& xbe) {
    static std::map<std::uint16_t, std::uint16_t> unicode;
    if (unicode.empty()) {
        for (int glyph = 0; glyph < font_unicode_glyphs; ++glyph) {
            const std::uint32_t at = font_sjis_order_va + static_cast<std::uint32_t>(glyph) * 2;
            const auto pair =
                static_cast<std::uint16_t>(xbe_u8(xbe, at) << 8 | xbe_u8(xbe, at + 1));
            const auto code = static_cast<std::uint16_t>(
                xbe_i16(xbe, font_unicode_order_va + static_cast<std::uint32_t>(glyph) * 2));
            if (code != 0) {
                unicode.emplace(pair, code);
            }
        }
        // cp932's IBM full-width apostrophe, which table text uses and the font lacks (as the
        // tables family does)
        unicode.emplace(static_cast<std::uint16_t>(0xFA56), 0xFF07);
    }
    return unicode;
}

} // namespace

std::string sjis_to_utf8(const Xbe& xbe, const std::string& text) {
    const std::map<std::uint16_t, std::uint16_t>& unicode = font_unicode(xbe);
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<std::uint8_t>(text[i]);
        const bool lead = (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c >= 0xA1 && c <= 0xDF) {
            append_utf8(out, 0xFF61 + (c - 0xA1)); // half-width katakana
        } else if (lead && i + 1 < text.size()) {
            const auto pair =
                static_cast<std::uint16_t>(c << 8 | static_cast<std::uint8_t>(text[i + 1]));
            const auto found = unicode.find(pair);
            if (found != unicode.end()) {
                append_utf8(out, found->second);
            } else {
                // the kanji past the font's Unicode table: cp932
                const char bytes[3] = {text[i], text[i + 1], 0};
                char* converted = sjis_iconv_string("CP932", bytes, 3);
                if (converted && converted[0]) {
                    out += converted;
                } else {
                    std::fprintf(stderr, "ui: no Unicode for Shift-JIS %04X\n", pair);
                    append_utf8(out, 0xFFFD);
                }
                SDL_free(converted);
            }
            ++i;
        } else {
            append_utf8(out, 0xFFFD);
        }
    }
    return out;
}

bool extract_ui(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/ui/layouts", made);
    std::filesystem::create_directories(options.out_dir + "/ui/timelines", made);
    std::filesystem::create_directories(options.out_dir + "/ui/images", made);

    // layouts first: they say which screens use each sprite, for the image index
    UiSpriteUses uses = {};
    return write_ui_layouts(disc, options.out_dir, uses, manifest) &&
           write_ui_timelines(disc, options.out_dir, manifest) &&
           write_ui_images(disc, options.out_dir, uses, manifest);
}
