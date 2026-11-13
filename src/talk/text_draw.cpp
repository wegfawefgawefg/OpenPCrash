// wide_text_draw (decomp src/hand/u_0bd620.c): a glyph token stream drawn as font quads,
// with the control codes, the glyph budget that makes typing work, and the blip.
#include "talk/talk_interp.hpp"

#include <cmath>

namespace {

constexpr int font_slot = 518;           // ascii_font_sys_tex (asset 1684), the one font
constexpr int glyph_width_count = 0x184; // glyph_widths covers glyphs below this
constexpr int press_glyph = 0x64;        // the mark -4 draws (func_000BEA5F)
constexpr int talking_glyph = 0x93;      // letters and digits start here: the mouth moves
constexpr int text_layer = 0x101;        // the texture argument is added to this
constexpr int line_height = 18;          // gap_y 0 plus the 18 pixel glyph
constexpr std::uint32_t pilot_name_color = 0xFF66EEEE; // pilot_name_draw's fixed cyan
// text_draw_18px's budgets: 9 glyphs for a name, 0x32 for text_draw_18px_wide
constexpr int name_budget = 9;
constexpr int wide_name_budget = 0x32;

// the blink flags pulse z over 80 ticks from the last menu input, and sprite_hidden and
// sprite_shown force it (render_command_submit_pass)
float sprite_alpha(std::uint32_t flags, const TalkContext& context) {
    if (flags & sprite_blinks) {
        const float period = 80.0f / static_cast<float>(60 / context.frame_cadence);
        const int wrap = static_cast<int>(period) + 1;
        const float phase = static_cast<float>(static_cast<unsigned>(context.input_age) %
                                               static_cast<unsigned>(wrap)) /
                            period;
        float z = std::fabs(phase * 2.0f - 1.0f);
        if (flags & sprite_blink_soft) {
            z = z * 0.7f + 0.3f;
        } else if (flags & sprite_blink_hard) {
            z = z < 0.5f ? 0.0f : 1.0f;
        }
        return z;
    }
    if (flags & sprite_hidden) {
        return 0.0f;
    }
    if (flags & sprite_shown) {
        return 1.0f;
    }
    return 1.0f;
}

// one glyph: its cell trimmed to left..left+width (wide_glyph_rect, func_000BEA9F)
void glyph_quad(const TalkContext& context, Draw2D& draw, int glyph, int left, int width, int x,
                int y, int texture, std::uint32_t color, std::uint32_t flags) {
    const TalkFont& font = *context.font;
    const int cell = font.cell;
    const int per_row = font.sheet_width / cell;
    const int cx = (glyph % per_row) * cell;
    const int cy = (glyph / per_row) * cell;
    Quad2D quad;
    quad.slot = font_slot;
    quad.u0 = static_cast<float>(cx + left);
    quad.v0 = static_cast<float>(cy);
    quad.u1 = static_cast<float>(cx + left + width);
    quad.v1 = static_cast<float>(cy + cell);
    quad.sheet_w = static_cast<float>(font.sheet_width);
    quad.sheet_h = static_cast<float>(font.sheet_height);
    quad.x = static_cast<float>(x);
    quad.y = static_cast<float>(y);
    quad.w = static_cast<float>(width);
    quad.h = static_cast<float>(cell);
    quad.color = color;
    quad.alpha = sprite_alpha(flags & sprite_blinks, context);
    quad.layer = text_layer + texture;
    submit_quad(draw, quad);
}

// wide_text_width(text, 2): the pen's travel over the whole string, proportional
int text_width(const TalkFont& font, const GlyphText& text) {
    int width = 0;
    for (const std::int16_t token : text) {
        if (token == -1) {
            break;
        }
        if (token >= 0) {
            width +=
                token < glyph_width_count && static_cast<std::size_t>(token) < font.advance.size()
                    ? font.advance[static_cast<std::size_t>(token)]
                    : 18;
        }
    }
    return width;
}

// text_draw_18px and friends: a name drawn inside a line, returning how far the pen moves
int draw_name(TalkInterp& interp, const TalkContext& context, const GlyphText& name, int x, int y,
              int texture, std::uint32_t color, int budget, const TalkPads& pads, Draw2D& draw,
              std::vector<int>& sounds) {
    if (name.empty()) {
        return 0;
    }
    wide_text_draw(interp, context, name, 0, x, y, texture, color, budget, 2, pads, draw, sounds);
    return text_width(*context.font, name);
}

} // namespace

// decomp: wide_text_draw 0xBD6A0
bool wide_text_draw(TalkInterp& interp, const TalkContext& context, const GlyphText& text,
                    std::uint32_t flags, int x, int y, int texture, std::uint32_t color, int budget,
                    int opts, const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds) {
    const TalkFont& font = *context.font;
    if (!(opts & 4)) {
        opts |= 2; // proportional: every caller gets it
    }
    const int line_start = x;
    std::uint32_t current = color;
    int wrap = budget;
    int token = -1;
    std::size_t at = 0;
    while (at < text.size() && (token = text[at++]) != -1 && --wrap >= 0) {
        switch (token) {
        case -2: // new line
            x = line_start;
            y += line_height;
            interp.press_mark_up = false;
            break;
        case -3: // the next -4 draws its mark again
            interp.press_mark_up = false;
            break;
        case -4: // the press mark, once
            if (!interp.press_mark_up) {
                glyph_quad(context, draw, press_glyph, 0, font.cell, x, y, texture, current, flags);
                x += font.cell;
                interp.press_mark_up = true;
            }
            break;
        case -5:
            x += draw_name(interp, context, context.pilot_name, x, y, texture, pilot_name_color,
                           name_budget, pads, draw, sounds);
            interp.press_mark_up = false;
            break;
        case -6:
            x += draw_name(interp, context, context.chip_name, x, y, texture, color, name_budget,
                           pads, draw, sounds);
            interp.press_mark_up = false;
            break;
        case -7:
            x += draw_name(interp, context, context.speaker_a, x, y, texture, color, name_budget,
                           pads, draw, sounds);
            break;
        case -8:
            x += draw_name(interp, context, context.speaker_b, x, y, texture, color,
                           wide_name_budget, pads, draw, sounds);
            break;
        case -9: // back to the command's colour; colour codes don't count against the budget
            ++wrap;
            current = color;
            break;
        case -10:
        case -11:
        case -12:
        case -13:
        case -14:
        case -15:
        case -16:
        case -17:
        case -18: {
            static constexpr std::uint32_t colors[9] = {0xFF66EEEE, 0xFFEE9500, 0xFFEE88EE,
                                                        0xFF808080, 0xFF308080, 0xFF8E4500,
                                                        0xFF66EE33, 0xFFEEEE66, 0xFF256584};
            ++wrap;
            current = colors[-10 - token];
            break;
        }
        default: {
            // a glyph: proportional below 0x184, a whole cell above
            int left = 0;
            int width = font.cell;
            if ((opts & 2) && token >= 0 && token < glyph_width_count &&
                static_cast<std::size_t>(token) < font.left.size()) {
                left = font.left[static_cast<std::size_t>(token)];
                width = font.advance[static_cast<std::size_t>(token)];
            }
            if (token >= 0) {
                glyph_quad(context, draw, token, left, width, x, y, texture, current, flags);
            }
            // the newest glyph of a typed line blips, and a letter or digit moves the mouth
            if (token != 0 && wrap == 0 && (opts & 1)) {
                sounds.push_back(sound_text_blip);
                if (token >= talking_glyph) {
                    interp.talking = true;
                }
            }
            x += width;
            interp.press_mark_up = false;
            break;
        }
        }
        // a -4 holds the rest of the line until accept
        if (token == -4 && !pads.accept) {
            break;
        }
        // every token that doesn't stop the line lets the mark draw again (so a -4 that isn't the
        // line's first token shows its mark every frame)
        interp.press_mark_up = false;
    }
    return token == -1;
}
