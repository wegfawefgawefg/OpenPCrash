#include "screens/ui/screen_text.hpp"

#include "audio/sound_cues.hpp"
#include "render/vk_sprites.hpp"
#include "screens/ui/ui_anchors.hpp"

#include <cmath>

namespace {

// the font sheet (ascii_font_sys_tex: 512 x 2048, 18 x 18 cells, 28 a row)
constexpr float font_sheet_w = 512.0f;
constexpr float font_sheet_h = 2048.0f;
constexpr int cell = 18;
constexpr int per_row = 28;

// D_002805D8.f_24 (0x2805FC) is 1: boxes open height first, 0.15 and 0.3 of their size an update
constexpr float box_grow_x = 0.15f;
constexpr float box_grow_y = 0.3f;
// D_0028039C.f_04 (0x2803A0): the two corner sprites by style, -1 none
constexpr int box_corners[4][2] = {{-1, -1}, {3155, 1224}, {3159, 1225}, {3168, 1226}};

int glyph_left(const Ui& ui, int glyph) {
    const auto& left = ui.data.font.left;
    return glyph >= 0 && glyph < static_cast<int>(left.size()) ? left[static_cast<std::size_t>(glyph)]
                                                                : 0;
}
// decomp: glyph_advance 0xBD665
int glyph_advance(const Ui& ui, int glyph) {
    const auto& advance = ui.data.font.advance;
    return glyph >= 0 && glyph < static_cast<int>(advance.size())
               ? advance[static_cast<std::size_t>(glyph)]
               : cell;
}

// one glyph's texture record: its cell trimmed to left..left+advance (wide_glyph_rect)
SpriteTex glyph_tex(const Ui& ui, int glyph) {
    SpriteTex tex;
    tex.slot = font_slot;
    const int cx = (glyph % per_row) * cell;
    const int cy = (glyph / per_row) * cell;
    const int left = glyph_left(ui, glyph);
    tex.rect[0] = static_cast<float>(cx + left);
    tex.rect[1] = static_cast<float>(cy);
    tex.rect[2] = static_cast<float>(cx + left + glyph_advance(ui, glyph));
    tex.rect[3] = static_cast<float>(cy + cell);
    tex.sheet[0] = font_sheet_w;
    tex.sheet[1] = font_sheet_h;
    return tex;
}

// wide_text_draw's colour codes -10..-18 (-9 is the caller's colour)
std::uint32_t code_colour(int code, std::uint32_t base) {
    switch (code) {
    case -0xa:
        return static_cast<std::uint32_t>(-0x991112);
    case -0xb:
        return static_cast<std::uint32_t>(-0x116b00);
    case -0xc:
        return static_cast<std::uint32_t>(-0x117712);
    case -0xd:
        return static_cast<std::uint32_t>(-0x7f7f80);
    case -0xe:
        return static_cast<std::uint32_t>(-0xcf7f80);
    case -0xf:
        return static_cast<std::uint32_t>(-0x71bb00);
    case -0x10:
        return static_cast<std::uint32_t>(-0x9911cd);
    case -0x11:
        return static_cast<std::uint32_t>(-0x11119a);
    case -0x12:
        return static_cast<std::uint32_t>(-0xda9a7c);
    default:
        return base;
    }
}

void box_sprite(Ui& ui, int id, float x, float y, int blend, std::uint32_t flags = 0) {
    Sprite sprite = sprite_of(ui, id);
    sprite.pos[0] = x;
    sprite.pos[1] = y;
    sprite.blend = blend;
    sprite.flags |= flags;
    sprite_submit(ui, sprite);
}

} // namespace

// decomp: wide_text_draw_small 0xBDC1B
void text_draw(Ui& ui, const GlyphText& text, int x, int y, int texture, int glyph_w,
               int glyph_h, std::uint32_t color, int wrap, std::uint32_t flags) {
    const int x0 = x;
    std::uint32_t colour = color;
    const float scale_x = static_cast<float>(glyph_w) / 18.0f;
    const float scale_y = static_cast<float>(glyph_h) / 18.0f;
    for (const std::int16_t token : text) {
        if (token == -1 || --wrap < 0) {
            break;
        }
        if (token == -2) {
            x = x0;
            y += glyph_h;
            continue;
        }
        if (token <= -9 && token >= -0x12) {
            ++wrap; // colour codes don't count
            colour = code_colour(token, color);
            continue;
        }
        if (token < 0) {
            continue; // names and the press mark belong to conversations
        }
        // func_000BEA9F: flags 0, or 0x10 for a scaled glyph, and the blinks passed on
        Sprite sprite;
        sprite.tex = glyph_tex(ui, token);
        sprite.pos[0] = static_cast<float>(x);
        sprite.pos[1] = static_cast<float>(y);
        sprite.scale[0] = scale_x;
        sprite.scale[1] = scale_y;
        sprite.blend = texture + 0x101;
        sprite.color = colour;
        sprite.flags = (scale_x != 1.0f || scale_y != 1.0f ? 0x10u : 0u) | (flags & 0x6020);
        sprite_submit(ui, sprite);
        x += static_cast<int>(static_cast<float>(glyph_advance(ui, token)) * scale_x);
    }
}

int text_width(Ui& ui, const GlyphText& text) {
    int total = 0;
    for (const std::int16_t token : text) {
        if (token == -1) {
            break;
        }
        total += token >= 0 ? glyph_advance(ui, token) : cell;
    }
    return total;
}

// decomp: text_strip_init 0xBE207
void text_strip_init(Ui& ui, TextStrip& strip, int x, int y, const std::string& text, int left,
                     int top, int right, int bottom) {
    strip.text = utf8_to_glyphs(ui.data, text);
    strip.x = x;
    strip.y = y;
    strip.next = 0;
    strip.left = left;
    strip.top = top;
    strip.right = right;
    strip.bottom = bottom;
}

void help_strip_init(Ui& ui, TextStrip& strip, const std::string& text) {
    text_strip_init(ui, strip, 0, 0x48, text, 0, 0, 0x280, 0x1e0);
}

// decomp: text_strip_draw_scroll 0xBE430
void text_strip_draw_scroll(Ui& ui, TextStrip& strip, std::uint32_t color, int blend) {
    const int width = text_width(ui, strip.text);
    if (width <= 0) {
        return;
    }
    // a ticker that reaches the 4:3 box's title-safe edge runs on to the window's in a wide one
    // (docs/screens.md "Widescreen"; its glyphs take a free or extend anchor there)
    const int reach = ui.renderer
                          ? static_cast<int>(std::ceil(
                                sprite_frame_reach(renderer_sprite_frame(*ui.renderer, -1))))
                          : 0;
    // and an end the screen's anchors tie to a moved piece (a footer opened at a column) moves
    // with it
    int left = strip.left <= 32 ? strip.left - reach : strip.left;
    int right = strip.right >= 608 ? strip.right + reach : strip.right;
    if (ui.renderer && ui.anchor_screen) {
        const SpriteFrame f = renderer_sprite_frame(*ui.renderer, -1);
        const float mid = static_cast<float>(strip.top + strip.bottom) * 0.5f;
        if (strip.left > 32) {
            left += static_cast<int>(std::floor(anchor_ticker_end_shift(
                ui.anchor_screen, f, static_cast<float>(strip.left), mid)));
        }
        if (strip.right < 608) {
            right += static_cast<int>(std::ceil(anchor_ticker_end_shift(
                ui.anchor_screen, f, static_cast<float>(strip.right), mid)));
        }
    }
    // copies of the line side by side across the clip rectangle, the first one scrolled
    int start = strip.x - strip.next;
    while (start > left - width) {
        start -= width;
    }
    while ((start += width) < right) {
        int cx = start;
        for (const std::int16_t glyph : strip.text) {
            if (cx > left - cell && cx < right) {
                Sprite sprite;
                sprite.tex = glyph_tex(ui, glyph);
                sprite.pos[0] = static_cast<float>(cx);
                sprite.pos[1] = static_cast<float>(strip.y);
                sprite.blend = blend;
                sprite.color = color;
                sprite.flags = 0;
                sprite_submit_clipped(ui, sprite, static_cast<float>(left),
                                      static_cast<float>(strip.top), static_cast<float>(right),
                                      static_cast<float>(strip.bottom));
            }
            cx += glyph_advance(ui, glyph);
        }
    }
    // one pixel a vblank tick
    strip.next += 60 / ui.frame_cadence;
    if (strip.next > width) {
        strip.next -= width;
    }
}

MsgBox msg_box_from(Ui& ui, const std::string& screen, const std::string& table, int row) {
    MsgBox box;
    if (const MsgBoxRow* found = ui_box(ui.data, screen, table, row)) {
        box.row = *found;
        box.valid = true;
    }
    return box;
}

// decomp: msg_box_open 0xBE1DC
void msg_box_open(MsgBox& box) {
    box.row.sx = 0.1f;
    box.row.sy = 0.1f;
    box.row.done = false;
}

// decomp: msg_box_update 0xBDF2C
void msg_box_update(Ui& ui, MsgBox& box) {
    if (!box.valid) {
        return;
    }
    MsgBoxRow& m = box.row;
    if (m.done) {
        // the columns field is the text's layer above 0x101 less one
        for (int i = 0; i < 6; ++i) {
            if (!m.lines[i].empty()) {
                text_draw(ui, m.lines[i], m.x, m.y + m.step * i, m.columns + 1, m.size, m.step,
                          m.color, 0x2e);
            }
        }
    }
    Sprite frame = sprite_of(ui, m.style == 0 ? 0xe8d : 0xe8e);
    frame.pos[0] = m.cx;
    frame.pos[1] = m.cy;
    frame.blend = m.blend + 0x102;
    frame.scale[0] = m.sx;
    frame.scale[1] = m.sy;
    frame.flags |= 8;
    sprite_submit(ui, frame);
    if (m.style != 0 && m.done && m.style < 4) {
        if (box_corners[m.style][0] != -1) {
            box_sprite(ui, box_corners[m.style][0], m.cx - 200.0f, m.cy - 66.0f, m.blend + 0x103);
        }
        if (box_corners[m.style][1] != -1) {
            box_sprite(ui, box_corners[m.style][1], m.cx + 124.0f, m.cy - 27.0f, m.blend + 0x103);
        }
    }
    // the opening sound, on the box's first update (its scale still exactly 0.1)
    if (m.sx == 0.1f && m.sy == 0.1f) {
        ui_sound(ui, bank_sys, se_message_box);
    }
    m.sx = std::fmin(m.sx + box_grow_x, 1.0f);
    m.sy = std::fmin(m.sy + box_grow_y, 1.0f);
    if (m.sx >= 1.0f && m.sy >= 1.0f) {
        m.done = true;
    }
}

// decomp: msg_box_update_ok 0xBDF3D
void msg_box_update_ok(Ui& ui, MsgBox& box) {
    msg_box_update(ui, box);
    if (!box.row.done) {
        return;
    }
    const PointerRect at = msg_box_ok_rect(ui, box);
    box_sprite(ui, 0xc1b, at.left, at.top, box.row.blend + 0x103);
    box_sprite(ui, 0xc1c, at.left, at.top, box.row.blend + 0x104, 0x2000);
}

// decomp: msg_box_update_choice 0xBE01E
void msg_box_update_choice(Ui& ui, MsgBox& box, int choice) {
    msg_box_update(ui, box);
    if (!box.row.done) {
        return;
    }
    PointerRect at[2];
    msg_box_choice_rects(ui, box, at);
    box_sprite(ui, 0xe85, at[0].left, at[0].top, box.row.blend + 0x103);
    box_sprite(ui, 0xe83, at[1].left, at[1].top, box.row.blend + 0x103);
    if (choice == 0) {
        box_sprite(ui, 0xe86, at[0].left, at[0].top, box.row.blend + 0x104, 0x2000);
    } else {
        box_sprite(ui, 0xe84, at[1].left, at[1].top, box.row.blend + 0x104, 0x2000);
    }
}

void msg_box_choice_rects(Ui& ui, const MsgBox& box, PointerRect out[2]) {
    const bool plain = box.row.style == 0;
    const float y = plain ? 297.0f : 282.0f;
    out[0] = sprite_rect(ui, 0xe85, plain ? 223.0f : 186.0f, y);
    out[1] = sprite_rect(ui, 0xe83, plain ? 322.0f : 281.0f, y);
}

PointerRect msg_box_ok_rect(Ui& ui, const MsgBox& box) {
    const bool plain = box.row.style == 0;
    return sprite_rect(ui, 0xc1b, plain ? 273.0f : 235.0f, plain ? 297.0f : 282.0f);
}

// decomp: number_draw 0xBC652
void number_draw(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                 std::uint32_t color, int digits, int font) {
    const UiTable* fonts = ui_table(ui.data, "font", "number_fonts");
    if (!fonts) {
        return;
    }
    long long v = value < 0 ? -value : value;
    for (int i = 0; i < digits; ++i) {
        const int ch = static_cast<int>(v % 10);
        v /= 10;
        if (v == 0 && ch == 0 && i != 0 && !(flags & 0x10)) {
            break;
        }
        const std::size_t row = static_cast<std::size_t>(font * 16 + ch);
        if (row >= fonts->pairs.size()) {
            break;
        }
        // glyph_draw_advance with flags 1: step left by the advance, then draw
        x -= fonts->pairs[row][1];
        Sprite sprite = sprite_of(ui, fonts->pairs[row][0]);
        sprite.pos[0] = static_cast<float>(x);
        sprite.pos[1] = static_cast<float>(y);
        sprite.blend = blend;
        sprite.color = color;
        sprite_submit(ui, sprite);
    }
}
