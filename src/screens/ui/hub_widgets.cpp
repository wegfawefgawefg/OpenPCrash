// The pieces the hub screens share (decomp src/hand/u_0b1a50.c day_draw, money_draw;
// src/hand/u_0bbb00.c number_draw, time_draw_seconds;
// src/od/u_06c3e0.c garage_name_banner_draw).
#include "screens/campaign/campaign.hpp"

#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <cstdlib>
#include <map>

std::string string_at_data(Ui& ui, unsigned address) {
    // text/strings.sexp: (string id text (used-by (code ..) (data ..)...)), read once
    static std::map<unsigned, std::string> by_data;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        gsexp::ParseResult parsed;
        std::string error;
        if (ui_sexp(ui.data, "text/strings.sexp", parsed, error)) {
            for (std::size_t i = 0; i < parsed.root_count(); ++i) {
                const gsexp::Node form = parsed.root(i);
                if (!form.is_list() || form.child_count() < 4) {
                    continue;
                }
                const std::string text(form.child_at(2).text());
                // the string's own address (its id, str.XXXXXX)
                const std::string id(form.child_at(1).text());
                if (id.rfind("str.", 0) == 0) {
                    by_data.emplace(static_cast<unsigned>(std::strtoul(id.c_str() + 4, nullptr, 16)),
                                    text);
                }
                for (gsexp::Node use : form.child_at(3).children()) {
                    if (use.is_list() && use.head().is_atom("data")) {
                        by_data[static_cast<unsigned>(std::strtoul(
                            std::string(use.child_at(1).text()).c_str(), nullptr, 16))] = text;
                    }
                }
            }
        }
    }
    const auto found = by_data.find(address);
    return found != by_data.end() ? found->second : std::string();
}

namespace {

// a number font's glyph (font_record(font) + 4 x index): 0..9 the digits, 10 minus, 11 plus,
// 12 the separator, 13 the point, 15 the colon. The extracted table stops two rows into font 4
// (the result's destruction count); its digits run on from 2282 with advance 28 in the words
// after it (D_0027DC38, read as short pairs)
bool font_glyph(Ui& ui, int font, int index, int& sprite, int& advance) {
    const UiTable* fonts = ui_table(ui.data, "font", "number_fonts");
    const std::size_t row = static_cast<std::size_t>(font * 16 + index);
    if (fonts && row < fonts->pairs.size()) {
        sprite = fonts->pairs[row][0];
        advance = fonts->pairs[row][1];
        return true;
    }
    if (font == 4 && index < 10) {
        sprite = 2282 + index;
        advance = 28;
        return true;
    }
    return false;
}

// glyph_draw_advance with flags 1: step left by the advance, then draw (measure: no draw)
// decomp: glyph_draw_advance 0xBCCFD
int glyph_left(Ui& ui, int font, int index, int& x, int y, int blend, std::uint32_t color,
               bool measure) {
    int sprite = 0;
    int advance = 0;
    if (!font_glyph(ui, font, index, sprite, advance)) {
        return 0;
    }
    x -= advance;
    if (!measure) {
        Sprite s = sprite_of(ui, sprite);
        s.pos[0] = static_cast<float>(x);
        s.pos[1] = static_cast<float>(y);
        s.blend = blend;
        s.color = color;
        sprite_submit(ui, s);
    }
    return advance;
}

// three numbers of `digits` leading-zero places from x, a glyph of the font between them (drawn at
// x and stepped over: glyph_draw_advance with flags 0), the shape of date_draw and time_hms_draw
void three_numbers_draw(Ui& ui, const int parts[3], const int digits[3], int separator, int x,
                        int y, int blend, std::uint32_t color, int font) {
    int sprite = 0;
    int advance = 0;
    font_glyph(ui, font, 0, sprite, advance);
    for (int i = 0; i < 3; ++i) {
        number_draw_ex(ui, 0x10, parts[i], x, y, blend, color, digits[i], 1, font);
        x += advance * digits[i];
        int mark = 0;
        int mark_advance = 0;
        if (i < 2 && font_glyph(ui, font, separator, mark, mark_advance)) {
            Sprite s = sprite_of(ui, mark);
            s.pos[0] = static_cast<float>(x);
            s.pos[1] = static_cast<float>(y);
            s.blend = blend;
            s.color = color;
            sprite_submit(ui, s);
            x += mark_advance;
        }
    }
}

} // namespace

// decomp: date_draw 0xBCF62
void date_draw(Ui& ui, int year, int month, int day, int x, int y, int blend, std::uint32_t color,
               int font) {
    // date_draw: four places, two and two, the font's glyph 14 (font_record + 0x38)
    // between them
    const int parts[3] = {year, month, day};
    const int digits[3] = {4, 2, 2};
    three_numbers_draw(ui, parts, digits, 14, x, y, blend, color, font);
}

// decomp: time_hms_draw 0xBD1D5
void time_hms_draw(Ui& ui, int hours, int minutes, int seconds, int x, int y, int blend,
                   std::uint32_t color, int font) {
    // time_hms_draw: two places each, the colon (glyph 15, font_record + 0x3C) between
    const int parts[3] = {hours, minutes, seconds};
    const int digits[3] = {2, 2, 2};
    three_numbers_draw(ui, parts, digits, 15, x, y, blend, color, font);
}

// decomp: time_draw_seconds 0xBD14C
void time_draw_seconds(Ui& ui, int ticks, int x, int y, int blend, std::uint32_t color,
                       int font) {
    // time_draw_seconds: hours, minutes, seconds of a tick count (ticks / 60 are the
    // seconds the function's name means)
    int seconds = ticks / 60;
    int minutes = seconds / 60;
    seconds -= minutes * 60;
    const int hours = minutes / 60;
    minutes -= hours * 60;
    time_hms_draw(ui, hours, minutes, seconds, x, y, blend, color, font);
}

GlyphText glyphs_of(Ui& ui, const std::string& text) {
    GlyphText glyphs = utf8_to_glyphs(ui.data, text);
    glyphs.push_back(-1);
    return glyphs;
}
