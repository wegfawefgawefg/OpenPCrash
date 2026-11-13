#pragma once
// The PC settings' look, made only of the original's own art (docs/settings.md, "The look"): the
// letters of the key config's white labels, the option buttons' yellow words and the screens'
// italic titles, cut out of their sprites so new words read as if the original had them. The
// letters' columns are found by the cook, which scans the sprites' PNGs for the text's pixels
// (src/cook/cook_pc_art.cpp); the cut-outs are drawn from the cooked sprites like any other.
#include "screens/ui/ui.hpp"

#include <cstdint>
#include <map>
#include <string>

// one letter: a column run of a sprite, and where its baseline sits in that sprite
struct ArtGlyph {
    int sprite = -1;
    int x0 = 0, x1 = 0; // texel columns, the cut
    int baseline = 0;   // the text's bottom row in the sprite
    int height = 0;     // the sprite's height
};

struct ArtFont {
    std::map<char, ArtGlyph> glyphs;
    int baseline = 0; // where every glyph's baseline is put, from the top of a line
    int space = 7;
};

struct PcArt {
    bool loaded = false;
    ArtFont thin;              // the key config's label font, white (with the number font's digits)
    ArtFont button;            // the option buttons' word font, normal (yellow on black)
    std::map<int, int> lit_of; // a button sprite to its lit twin (black on yellow)
};

// reads the cuts once (pc_art.sexp in the core pack)
void pc_art_load(PcArt& art, Ui& ui);

// the thin label font: uppercase letters, digits and a few signs; width in pixels
int pc_text_width(const PcArt& art, const std::string& text);
// the label font has the character (cut or made, or the space)
bool pc_text_drawable(const PcArt& art, char c);
void pc_text_draw(Ui& ui, const PcArt& art, const std::string& text, float x, float y, int blend,
                  std::uint32_t color = 0xffffffff, std::uint32_t flags = 0);

// a 192x40 option button with a word on it, normal or lit, built in columns that never overlap
// (so a pulsing lit button fades as one piece)
inline constexpr float button_w = 192, button_h = 40;
void pc_button_draw(Ui& ui, const PcArt& art, const std::string& word, float x, float y, bool lit,
                    int blend, std::uint32_t flags = 0);
PointerRect pc_button_rect(float x, float y);

// the header strip with "PC CONFIG" (the sound config's, its SOUND replaced by PC cut from the
// options' OPTION)
void pc_title_draw(Ui& ui, int blend);

// a bar sprite stretched to width w by its middle, the rounded ends kept
void pc_bar_draw(Ui& ui, int sprite, float x, float y, float w, int blend, std::uint32_t color,
                 std::uint32_t flags = 0);
// SHOW FRAME RATE: "60 FPS" in the label font on a dim plate, top right over everything but the
// fade (layer 0x7FFE); nothing while the setting is off
struct PcHost;
void pc_fps_draw(Ui& ui, PcArt& art, const PcHost& host);

// one texel rectangle of a sprite at x, y (scaled to w, h)
void pc_piece_draw(Ui& ui, int sprite, float u0, float v0, float u1, float v1, float x, float y,
                   float w, float h, int blend, std::uint32_t color = 0xffffffff,
                   std::uint32_t flags = 0);
