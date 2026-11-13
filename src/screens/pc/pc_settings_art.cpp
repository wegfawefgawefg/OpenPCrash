#include "screens/pc/pc_settings_art.hpp"

#include "cook/cook_pc_art.hpp"
#include "cook/sexp_read.hpp"
#include "screens/pc/pc_settings.hpp"
#include "screens/pc/pc_settings_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// EXIT's plain stretches make the blank button: its ends and a filler column run
constexpr int blank_button = 2108, blank_button_lit = 2109;
constexpr float cap_w = 24, filler_u0 = 30, filler_u1 = 40;
// the rows of a button that hold its word, the rest is frame
constexpr float word_top = 8, word_bottom = 29;
// the titles: the sound config's header (blue, "SOUND CONFIG") and the options' frame ("OPTION")
constexpr int header_sprite = 2519, option_frame = 2106;
// the frame rate's plate: the fade's solid sprite (screen_fade_quad_draw's 0xE7C)
constexpr int fps_plate_sprite = 0xe7c;

// pc_art.sexp from the core pack (src/cook/cook_pc_art.cpp): the fonts' baselines and spaces,
// each letter's cut, each button's lit twin
void read_cuts(PcArt& art, Ui& ui) {
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(ui.data, pc_art_path, parsed, error)) {
        std::fprintf(stderr, "pc art: %s\n", error.c_str());
        return;
    }
    const auto font_of = [&](gsexp::Node name) -> ArtFont* {
        return name.is_atom("thin") ? &art.thin : name.is_atom("button") ? &art.button : nullptr;
    };
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const gsexp::Node node = form.node();
        if (form.head().is_atom("font")) {
            if (ArtFont* font = font_of(node.child_at(1))) {
                font->baseline = atom_int(form.find("baseline").child_at(1));
                font->space = atom_int(form.find("space").child_at(1));
            }
        } else if (form.head().is_atom("glyph")) {
            if (ArtFont* font = font_of(node.child_at(1))) {
                ArtGlyph glyph;
                glyph.sprite = atom_int(node.child_at(3));
                glyph.x0 = atom_int(node.child_at(4));
                glyph.x1 = atom_int(node.child_at(5));
                glyph.baseline = atom_int(node.child_at(6));
                glyph.height = atom_int(node.child_at(7));
                font->glyphs[static_cast<char>(atom_int(node.child_at(2)))] = glyph;
            }
        } else if (form.head().is_atom("lit")) {
            art.lit_of[atom_int(node.child_at(1))] = atom_int(node.child_at(2));
        }
    }
}

const ArtGlyph* glyph_of(const ArtFont& font, char c) {
    const auto found = font.glyphs.find(c);
    return found != font.glyphs.end() ? &found->second : nullptr;
}

// Q and Z, which no label has: Q is O with R's leg across its foot; Z is T's bar, the mirrored
// diagonal of N and L's foot
bool can_make(const ArtFont& font, char c) {
    if (c == 'Q') {
        return glyph_of(font, 'O') && glyph_of(font, 'R');
    }
    if (c == 'Z') {
        return glyph_of(font, 'T') && glyph_of(font, 'N') && glyph_of(font, 'L');
    }
    return false;
}

int glyph_width(const ArtFont& font, char c) {
    if (const ArtGlyph* g = glyph_of(font, c)) {
        return g->x1 - g->x0;
    }
    if (can_make(font, c)) {
        return glyph_width(font, c == 'Q' ? 'O' : 'N');
    }
    return font.space;
}

int font_width(const ArtFont& font, const std::string& text) {
    int width = 0;
    for (const char c : text) {
        width += glyph_width(font, c);
    }
    return width;
}

// a glyph's rows v0..v1 (from its sprite's top) at x, its top at `top`
void glyph_rows(Ui& ui, const ArtGlyph& g, float u0, float u1, float v0, float v1, float x,
                float top, int blend, std::uint32_t color, std::uint32_t flags) {
    pc_piece_draw(ui, g.sprite, u0, v0, u1, v1, x, top + v0, u1 - u0, v1 - v0, blend, color, flags);
}

void made_glyph(Ui& ui, const ArtFont& font, char c, float x, float y, int blend,
                std::uint32_t color, std::uint32_t flags) {
    if (c == 'Q') {
        const ArtGlyph& o = *glyph_of(font, 'O');
        const ArtGlyph& r = *glyph_of(font, 'R');
        const float o_top = y + static_cast<float>(font.baseline - o.baseline);
        const float r_top = y + static_cast<float>(font.baseline - r.baseline);
        const float half = static_cast<float>((r.x0 + r.x1) / 2);
        const float leg = static_cast<float>(r.baseline - 5);
        glyph_rows(ui, o, static_cast<float>(o.x0), static_cast<float>(o.x1), 0,
                   static_cast<float>(o.height), x, o_top, blend, color, flags);
        const float right = x + static_cast<float>(o.x1 - o.x0);
        glyph_rows(ui, r, half, static_cast<float>(r.x1), leg, static_cast<float>(r.baseline + 3),
                   right - (static_cast<float>(r.x1) - half), r_top, blend, color, flags);
        return;
    }
    const ArtGlyph& t = *glyph_of(font, 'T');
    const ArtGlyph& n = *glyph_of(font, 'N');
    const ArtGlyph& l = *glyph_of(font, 'L');
    const float w = static_cast<float>(n.x1 - n.x0);
    const float n_top = y + static_cast<float>(font.baseline - n.baseline);
    const float t_top = y + static_cast<float>(font.baseline - t.baseline);
    const float l_top = y + static_cast<float>(font.baseline - l.baseline);
    // the diagonal: N between its stems, mirrored
    glyph_rows(ui, n, static_cast<float>(n.x0 + 2), static_cast<float>(n.x1 - 2), 2,
               static_cast<float>(n.baseline - 1), x + 2, n_top, blend, color, flags | 0x400);
    // T's bar, from its middle out to N's width
    const float t_mid = static_cast<float>(t.x0 + t.x1) * 0.5f;
    glyph_rows(ui, t, t_mid - w * 0.5f, t_mid + w * 0.5f, 0, 3, x, t_top, blend, color, flags);
    // L's foot
    glyph_rows(ui, l, static_cast<float>(l.x0), static_cast<float>(l.x0) + w,
               static_cast<float>(l.baseline - 1), static_cast<float>(l.baseline + 3), x, l_top,
               blend, color, flags);
}

} // namespace

void pc_art_load(PcArt& art, Ui& ui) {
    if (art.loaded) {
        return;
    }
    art.loaded = true;
    read_cuts(art, ui);
    std::printf("pc art: %zu label glyphs, %zu button glyphs\n", art.thin.glyphs.size(),
                art.button.glyphs.size());
}

void pc_piece_draw(Ui& ui, int sprite, float u0, float v0, float u1, float v1, float x, float y,
                   float w, float h, int blend, std::uint32_t color, std::uint32_t flags) {
    if (u1 <= u0 || v1 <= v0 || w <= 0 || h <= 0) {
        return;
    }
    Sprite piece = sprite_of(ui, sprite);
    piece.tex.rect[0] = u0;
    piece.tex.rect[1] = v0;
    piece.tex.rect[2] = u1;
    piece.tex.rect[3] = v1;
    piece.scale[0] = w / (u1 - u0);
    piece.scale[1] = h / (v1 - v0);
    piece.pos[0] = x;
    piece.pos[1] = y;
    piece.blend = blend;
    piece.color = color;
    piece.flags |= flags;
    sprite_submit(ui, piece);
}

bool pc_text_drawable(const PcArt& art, char c) {
    return c == ' ' || glyph_of(art.thin, c) || can_make(art.thin, c);
}

int pc_text_width(const PcArt& art, const std::string& text) {
    return font_width(art.thin, text);
}

void pc_text_draw(Ui& ui, const PcArt& art, const std::string& text, float x, float y, int blend,
                  std::uint32_t color, std::uint32_t flags) {
    const ArtFont& font = art.thin;
    for (const char c : text) {
        const auto found = font.glyphs.find(c);
        if (found == font.glyphs.end()) {
            if (can_make(font, c)) {
                made_glyph(ui, font, c, x, y, blend, color, flags);
            }
            x += static_cast<float>(glyph_width(font, c));
            continue;
        }
        const ArtGlyph& g = found->second;
        const float w = static_cast<float>(g.x1 - g.x0);
        const float top = y + static_cast<float>(font.baseline - g.baseline);
        pc_piece_draw(ui, g.sprite, static_cast<float>(g.x0), 0, static_cast<float>(g.x1),
                      static_cast<float>(g.height), x, top, w, static_cast<float>(g.height), blend,
                      color, flags);
        x += w;
    }
}

void pc_fps_draw(Ui& ui, PcArt& art, const PcHost& host) {
    if (!host.settings.show_fps) {
        return;
    }
    pc_art_load(art, ui);
    const std::string text = std::to_string(host.fps) + " FPS";
    const float w = static_cast<float>(pc_text_width(art, text));
    const float right = 604, top = 18;
    // the plate: the fade's solid sprite, half black
    Sprite plate = sprite_of(ui, fps_plate_sprite);
    const float sw = static_cast<float>(std::max(sprite_width(ui, fps_plate_sprite), 1));
    const float sh = static_cast<float>(std::max(sprite_height(ui, fps_plate_sprite), 1));
    plate.pos[0] = right - w - 6;
    plate.pos[1] = top;
    plate.scale[0] = (w + 10) / sw;
    plate.scale[1] = 24 / sh;
    plate.blend = 0x7ffe;
    plate.color = 0x90000000;
    plate.flags = 4;
    sprite_submit(ui, plate);
    pc_text_draw(ui, art, text, right - w, top - 2, 0x7ffe, 0xffffffff);
}

PointerRect pc_button_rect(float x, float y) {
    return {x, y, x + button_w, y + button_h};
}

void pc_button_draw(Ui& ui, const PcArt& art, const std::string& word, float x, float y, bool lit,
                    int blend, std::uint32_t flags) {
    const int blank = lit ? blank_button_lit : blank_button;
    const ArtFont& font = art.button;
    const float width = static_cast<float>(font_width(font, word));
    const float start = x + std::floor((button_w - width) * 0.5f);
    // a column of the blank's filler, frame rows and all
    auto filler = [&](float from, float to) {
        pc_piece_draw(ui, blank, filler_u0, 0, filler_u1, button_h, from, y, to - from, button_h,
                      blend, 0xffffffff, flags);
    };
    pc_piece_draw(ui, blank, 0, 0, cap_w, button_h, x, y, cap_w, button_h, blend, 0xffffffff,
                  flags);
    filler(x + cap_w, start);
    float at = start;
    for (const char c : word) {
        const auto found = font.glyphs.find(c);
        if (found == font.glyphs.end()) {
            filler(at, at + static_cast<float>(font.space));
            at += static_cast<float>(font.space);
            continue;
        }
        const ArtGlyph& g = found->second;
        const float w = static_cast<float>(g.x1 - g.x0);
        const auto lit_sprite = art.lit_of.find(g.sprite);
        const int sprite = lit && lit_sprite != art.lit_of.end() ? lit_sprite->second : g.sprite;
        // the frame above and below from the blank, the letter's rows moved onto the baseline
        const float shift = static_cast<float>(g.baseline - font.baseline);
        pc_piece_draw(ui, blank, filler_u0, 0, filler_u1, word_top, at, y, w, word_top, blend,
                      0xffffffff, flags);
        pc_piece_draw(ui, sprite, static_cast<float>(g.x0), word_top + shift,
                      static_cast<float>(g.x1), word_bottom + shift, at, y + word_top, w,
                      word_bottom - word_top, blend, 0xffffffff, flags);
        pc_piece_draw(ui, blank, filler_u0, word_bottom, filler_u1, button_h, at, y + word_bottom,
                      w, button_h - word_bottom, blend, 0xffffffff, flags);
        at += w;
    }
    filler(at, x + button_w - cap_w);
    pc_piece_draw(ui, blank, button_w - cap_w, 0, button_w, button_h, x + button_w - cap_w, y,
                  cap_w, button_h, blend, 0xffffffff, flags);
}

void pc_title_draw(Ui& ui, int blend) {
    // the strip, SOUND covered with the band's own blue from past the title's end
    Sprite strip = sprite_of(ui, header_sprite);
    strip.blend = blend;
    sprite_submit(ui, strip);
    pc_piece_draw(ui, header_sprite, 604, 30, 620, 67, 296, 30, 153, 37, blend);
    // P from OPTION and C from CONFIG, each cut in bands that follow the italic slant so no
    // neighbour's edge comes along, set one word space before CONFIG (as BUTTON CONFIG has it)
    pc_piece_draw(ui, option_frame, 481, 30, 502, 48, 397, 30, 21, 18, blend);
    pc_piece_draw(ui, option_frame, 480, 48, 502, 57, 396, 48, 22, 9, blend);
    pc_piece_draw(ui, option_frame, 478, 57, 502, 67, 394, 57, 24, 10, blend);
    pc_piece_draw(ui, header_sprite, 449, 30, 477, 48, 417, 30, 28, 18, blend);
    pc_piece_draw(ui, header_sprite, 449, 48, 473, 67, 417, 48, 24, 19, blend);
}

void pc_bar_draw(Ui& ui, int sprite, float x, float y, float w, int blend, std::uint32_t color,
                 std::uint32_t flags) {
    const float sw = static_cast<float>(sprite_width(ui, sprite));
    const float sh = static_cast<float>(sprite_height(ui, sprite));
    const float cap = std::min(12.0f, std::floor(sw / 3.0f));
    const float middle = std::max(0.0f, w - cap * 2.0f);
    pc_piece_draw(ui, sprite, 0, 0, cap, sh, x, y, cap, sh, blend, color, flags);
    pc_piece_draw(ui, sprite, cap, 0, sw - cap, sh, x + cap, y, middle, sh, blend, color, flags);
    pc_piece_draw(ui, sprite, sw - cap, 0, sw, sh, x + cap + middle, y, cap, sh, blend, color,
                  flags);
}

// the help lines

std::string pc_help_text(const std::string& text, int lead) {
    // full-width forms (U+FF01..U+FF5E, the ideographic space), led in by `lead` ideographic
    // spaces and followed by 4, as the original's help lines are written
    std::string out;
    for (int i = 0; i < lead; ++i) {
        out += "\xE3\x80\x80";
    }
    for (const unsigned char c : text) {
        if (c == ' ') {
            out += "\xE3\x80\x80";
        } else if (c > 0x20 && c < 0x7f) {
            const unsigned code = 0xff00u + (c - 0x20u);
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(c);
        }
    }
    for (int i = 0; i < 4; ++i) {
        out += "\xE3\x80\x80";
    }
    return out;
}

std::string pc_options_help() {
    return pc_help_text("Set the screen, picture, sound and controls of this PC.",
                        help_lead_options);
}

std::string pc_page_help(int page) {
    switch (page) {
    case page_screen:
        return "Set the window, the resolution, the frame rate and the HUD's width and size.";
    case page_picture:
        return "Set how the picture is drawn: its scale, smoothing, shape, brightness, flashes, "
               "shake and focus.";
    case page_sound:
        return "Set the PC's volumes over the original's.";
    case page_input:
        return "Set the mouse, the sticks' dead zones and calibration, and the pad's vibration.";
    case page_bindings:
        return "Set the keys, mouse buttons and pad buttons.";
    case page_game:
        return "Set the conversations and pausing.";
    default:
        return "Return to the options.";
    }
}
