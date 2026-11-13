// PC only: the conversation skip (talk_skip.hpp).
#include "talk/talk_skip.hpp"

#include "talk/draw2d.hpp"

#include <algorithm>

namespace {

// the hint: right-aligned under the top band's line, 3/4 of the 18 pixel font, dim grey over
// every layer the scripts use
constexpr float hint_right = 600.0f;
constexpr float hint_top = 40.0f;
constexpr float hint_scale = 0.75f;
constexpr std::uint32_t hint_color = 0xFFC0C0C0;
constexpr float hint_alpha = 0.7f;
constexpr int hint_texture = 0x0F; // layer 0x110
constexpr const char* hint_one = "Esc/Back: skip";
constexpr const char* hint_all = "Esc/Back: skip   Hold: skip all";

// the font has only the full-width forms: ASCII moved up to them as UTF-8 (the scripts' text is
// written that way, and ascii_char_to_fullwidth does the same for the screens)
std::string full_width(const char* ascii) {
    std::string out;
    for (const char* c = ascii; *c; ++c) {
        const std::uint32_t code = *c == ' ' ? 0x3000u : static_cast<std::uint32_t>(*c) + 0xFEE0u;
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

} // namespace

int talk_interp_finish(TalkInterp& interp, const TalkScript& script, const TalkContext& context) {
    // what a player holding the chord and pressing accept every frame gives: text marked done,
    // waits, delays and page waits passed; the draws and sounds go nowhere
    TalkPads pads;
    pads.accept = true;
    pads.chord = true;
    Draw2D draw;
    std::vector<int> sounds;
    for (int frame = 1; frame <= talk_finish_frame_cap; ++frame) {
        draw.quads.clear();
        sounds.clear();
        if (!talk_interp_step(interp, script, context, pads, draw, sounds)) {
            return frame;
        }
    }
    return -1;
}

int talk_script_finish(const TalkScript& script, const TalkContext& context,
                       std::uint32_t script_flags) {
    TalkInterp interp;
    talk_interp_start(interp, script_flags, 1);
    return talk_interp_finish(interp, script, context);
}

bool talk_skip_update(TalkSkip& skip, const PadRecord& pad, bool right_click) {
    if (skip.skipped) {
        // a hold carried on past the threshold takes the chain too
        if (skip.hold != 0 && (pad.b.cur & skip.hold) != 0) {
            skip.held += 1;
            if (skip.held >= talk_skip_hold_updates) {
                skip.group = true;
            }
        } else {
            skip.hold = 0;
        }
        return false;
    }
    const bool back = (pad.b.rising & pad_back) != 0;
    if (!back && !right_click) {
        return false;
    }
    skip.skipped = true;
    skip.hold = back ? pad_back : pad_left_trigger;
    skip.held = 0;
    // Shift (the keyboard's left stick click) or a pad's left stick click held: the chain at once
    skip.group = (pad.b.cur & pad_left_thumb) != 0;
    return true;
}

void talk_skip_hint_draw(const TalkContext& context, bool more, Draw2D& draw) {
    const GlyphText text = glyphs_from_text(*context.font, full_width(more ? hint_all : hint_one));
    // drawn at 0,0 full size, then scaled into the corner
    TalkInterp scratch;
    TalkPads pads;
    Draw2D glyphs;
    std::vector<int> sounds;
    wide_text_draw(scratch, context, text, 0, 0, 0, hint_texture, hint_color, 0x7FFFFFFF, 0, pads,
                   glyphs, sounds);
    float width = 0.0f;
    for (const Quad2D& quad : glyphs.quads) {
        width = std::max(width, quad.x + quad.w);
    }
    const float left = hint_right - width * hint_scale;
    for (Quad2D quad : glyphs.quads) {
        quad.x = left + quad.x * hint_scale;
        quad.y = hint_top + quad.y * hint_scale;
        quad.w *= hint_scale;
        quad.h *= hint_scale;
        quad.alpha *= hint_alpha;
        quad.flags |= sprite_smooth; // scaled
        submit_quad(draw, quad);
    }
}
