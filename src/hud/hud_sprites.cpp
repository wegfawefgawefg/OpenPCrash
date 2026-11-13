// The HUD's drawing pieces: a row of its tables at its place (HudElem::draw), a sprite, the
// colour blink, and number font 3's glyphs and numbers. The HUD around them is hud_core.cpp.
#include "hud/hud_internal.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

// font_record(3): number font 3's 16 rows from row 48 of number_fonts
constexpr int number_font_row = 48;

// decomp: number_fonts 0x27DB30
const UiTable* number_fonts(Ui& ui) {
    static const UiTable* fonts = nullptr;
    if (!fonts) {
        fonts = ui_table(ui.data, "font", "number_fonts");
    }
    return fonts;
}

} // namespace

// decomp: HudElem::draw 0x4D25B
void hud_row_draw(Ui& ui, const HudRow& row, float scale_x, float alpha, float dx, float dy,
                  std::uint32_t color, bool use_color) {
    if (row.sprite == -1 || scale_x == 0) {
        return;
    }
    std::uint32_t c = use_color ? color : row.color;
    if (alpha != 1) {
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        c = (static_cast<std::uint32_t>(static_cast<float>(c >> 24) * alpha) << 24) |
            (c & 0xFFFFFF);
    }
    hud_sprite(ui, row.sprite, row.x + dx, row.y + dy, row.scale_x * scale_x, row.scale_y, 0,
               row.blend, c, 0);
}

// decomp: HudElem::draw_scale 0x4D560
void hud_row_draw_scaled(Ui& ui, const HudRow& row, float scale) {
    if (row.sprite == -1 || scale == 0) {
        return;
    }
    const float w = static_cast<float>(sprite_width(ui, row.sprite));
    const float h = static_cast<float>(sprite_height(ui, row.sprite));
    hud_sprite(ui, row.sprite, row.x + (1 - scale) * w * 0.5f, row.y + (1 - scale) * h * 0.5f,
               row.scale_x * scale, row.scale_y * scale, 0, row.blend, row.color, 0);
}

void hud_sprite(Ui& ui, int id, float x, float y, float scale_x, float scale_y, float turn,
                int blend, std::uint32_t color, std::uint32_t flags) {
    Sprite sprite = sprite_of(ui, id);
    sprite.pos[0] = x;
    sprite.pos[1] = y;
    sprite.scale[0] = scale_x;
    sprite.scale[1] = scale_y;
    sprite.rot[2] = turn;
    sprite.blend = blend;
    sprite.color = color;
    sprite.z = 1;
    sprite.flags = 4 | flags;
    sprite_submit(ui, sprite);
}

// decomp: color_blink 0x4DBAF
std::uint32_t color_blink(const HudView& hud, int frames, std::uint32_t from, std::uint32_t to) {
    // retail's second half runs half - phase, negative, so it moves past `from` away from `to`;
    // the port comes back from `to` instead: 0, 20 .. 80, 100, 80 .. 20 percent (docs/bugs.md)
    const float half = static_cast<float>(frames) * 0.5f;
    const float phase = static_cast<float>(hud.task_timer % static_cast<std::uint32_t>(frames));
    const float t = (phase < half ? phase : static_cast<float>(frames) - phase) / half;
    const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFF);
        const float b = static_cast<float>((to >> shift) & 0xFF);
        return static_cast<std::uint32_t>(std::clamp((b - a) * t + a, 0.0f, 255.0f)) << shift;
    };
    return 0xFF000000u | channel(16) | channel(8) | channel(0);
}

int hud_font_advance(Ui& ui, int row) {
    const UiTable* fonts = number_fonts(ui);
    const std::size_t at = static_cast<std::size_t>(number_font_row + row);
    return fonts && at < fonts->pairs.size() ? fonts->pairs[at][1] : 0;
}

// decomp: glyph_draw_advance 0xBCCFD
void hud_glyph(Ui& ui, int row, int& x, int y, int blend, std::uint32_t color) {
    const UiTable* fonts = number_fonts(ui);
    const std::size_t at = static_cast<std::size_t>(number_font_row + row);
    if (!fonts || at >= fonts->pairs.size()) {
        return;
    }
    hud_sprite(ui, fonts->pairs[at][0], static_cast<float>(x), static_cast<float>(y), 1, 1, 0,
               blend, color, 0);
    x += fonts->pairs[at][1];
}

// decomp: number_draw 0xBC652
void hud_number(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                std::uint32_t color, int digits, int scale) {
    const UiTable* fonts = number_fonts(ui);
    if (!fonts) {
        return;
    }
    // number_digits_draw_grouped: right to left, each glyph stepped left then drawn; a measure
    // pass (flag 0x100) first unless flag 8, then drawn that far right of x
    const auto pass = [&](bool draw, int at) {
        long long v = std::llabs(value) * scale;
        int pen = at;
        for (int i = 0; i < digits; ++i) {
            const int ch = static_cast<int>(v % 10);
            v /= 10;
            if (v == 0 && ch == 0 && i != 0 && !(flags & number_zeros)) {
                break;
            }
            const auto glyph = [&](int row) {
                const std::size_t r = static_cast<std::size_t>(number_font_row + row);
                if (r >= fonts->pairs.size()) {
                    return;
                }
                pen -= fonts->pairs[r][1];
                if (draw) {
                    hud_sprite(ui, fonts->pairs[r][0], static_cast<float>(pen),
                               static_cast<float>(y), 1, 1, 0, blend, color, 0);
                }
            };
            if ((flags & number_commas) && i >= 3 && i % 3 == 0) {
                glyph(12);
            }
            glyph(ch);
        }
        return pen - at;
    };
    const int width = (flags & number_right) ? 0 : pass(false, x);
    pass(true, x - width);
}
