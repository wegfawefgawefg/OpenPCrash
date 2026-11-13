#pragma once
// The first-run screen's look: plain shapes through SDL's 2D renderer and text through SDL_ttf in
// Jost (assets/fonts, SIL Open Font License), close to the original's menus (blue bands with a
// white edge, black buttons with white frames and yellow words) without any of its art, which
// isn't there yet on a first run. Laid out on the original's 640x480 doubled, the 1280x960 the
// game opens at, letterboxed into any window.
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <map>
#include <string>

inline constexpr float setup_w = 1280, setup_h = 960;

enum class SetupFont { title, heading, body, button, small, count };

struct SetupArt {
    SDL_Renderer* renderer = nullptr;
    TTF_Font* fonts[static_cast<int>(SetupFont::count)] = {};
    // rendered text by font, colour and words, made once
    std::map<std::string, SDL_Texture*> texts;
};

bool setup_art_open(SetupArt& art, SDL_Renderer* renderer, const std::string& font_dir,
                    std::string& error);
void setup_art_close(SetupArt& art);

struct SetupColor {
    Uint8 r, g, b, a;
};
inline constexpr SetupColor setup_white{255, 255, 255, 255};
inline constexpr SetupColor setup_yellow{252, 196, 0, 255};
inline constexpr SetupColor setup_black{0, 0, 0, 255};
inline constexpr SetupColor setup_grey{176, 186, 214, 255};
inline constexpr SetupColor setup_blue{18, 58, 170, 255};
inline constexpr SetupColor setup_deep{6, 20, 64, 255};

void fill_rect(SetupArt& art, SDL_FRect rect, SetupColor color);
void fill_round_rect(SetupArt& art, SDL_FRect rect, float radius, SetupColor color);
// a frame of width thickness around rect, filled inside
void framed_round_rect(SetupArt& art, SDL_FRect rect, float radius, float thickness,
                       SetupColor frame, SetupColor fill);
// the screen's frame: the blue bands top and bottom with their swoops, the title in the top one
void draw_setup_frame(SetupArt& art, const char* title, const std::string& footer_left,
                      const std::string& footer_right);
// text by its top centre (align 0), top left (-1) or top right (1); wrap > 0 wraps it at that
// width; returns its height
float draw_text(SetupArt& art, SetupFont font, const std::string& text, float x, float y,
                SetupColor color, int align = 0, float wrap = 0);
// a button in the original's style; lit: the focused one (yellow, black words)
void draw_button(SetupArt& art, SDL_FRect rect, const std::string& label, bool lit);
// a bar of permille, framed
void draw_bar(SetupArt& art, SDL_FRect rect, int permille);
