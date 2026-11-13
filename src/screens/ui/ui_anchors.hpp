#pragma once
// Widescreen anchors (docs/screens.md "Widescreen"): where each piece of the original's 640x480
// layouts goes in a window wider than 4:3, read from assets/ui/anchors.sexp (the port's own data).
// A screen's rules name its sprites (registry ids), texture slots or regions of the 640x480 layout
// and give each an anchor: centre (stays in the box), left or right (moves out to the window's
// safe edge, as far as HUD WIDTH lets it), free (stays, unclipped: world points, wheels that run
// on past the box), extend (a background or bar continued past its edges with its own edge band:
// mirrored, tiled or its last texels stretched) or blur (a blurred, dimmed copy behind it fills the
// window). A screen may also put a flat colour behind everything (its backdrop). The first rule
// that matches a quad decides; nothing matching is centre.
#include "render/sprite_frame.hpp"
#include "talk/draw2d.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct PointerRect;

enum class AnchorKind : std::uint8_t { centre, left, right, free, extend, blur };
enum class ExtendMode : std::uint8_t { mirror, tile, clamp };

// a band of a picture's texel rows extended its own way (a frame's bars over its picture)
struct AnchorRows {
    float v0 = 0, v1 = 0;
    ExtendMode mode = ExtendMode::mirror;
    float band = 16;
    float split = -1;
    float soft = 0, soft_dim = 1; // (soft RADIUS [DIM]): the continuation blurred and dimmed
    float feather = 0; // (feather W): the picture's outer W pixels fade into its soft continuation
    float fill[4] = {}; // (fill U0 V0 U1 V1): a patch of the picture covering the sides instead
    int side = -1;      // (side left|right): only that side's continuation, -1 both
    float lines[2] = {}; // (lines ROWS [COLUMNS]): the picture's scanline (grid) period, texels
};

struct AnchorRule {
    // what it matches (all that are given): registry sprite ids (any of them), texture slots
    // lo..hi (the movie's 0x225, the conversations' and the font's below the registry's), the
    // middle in a rectangle of the 640x480 layout
    std::vector<int> sprites;
    int slot_lo = -1, slot_hi = -1;
    float size[2] = {-1, -1}; // or its width and height
    bool region = false;
    float rect[4] = {}; // left, top, right, bottom
    bool ticker_end = false; // (ticker-end ...): matches only a scrolling line's ends
    AnchorKind kind = AnchorKind::centre;
    ExtendMode mode = ExtendMode::mirror;
    float band = 16;   // extend: the edge band, texels
    std::vector<AnchorRows> rows; // extend: bands of rows with their own mode and band
    float split = -1; // extend: a bar's texel column where it opens to the edge-anchored 2D's width
    float soft = 0, soft_dim = 1; // extend: the continuation blurred (radius, texels) and dimmed
    float feather = 0; // extend: screen pixels over which a soft continuation fades in
    float fill[4] = {}; // extend: the sides covered with this patch of texels, mirrored both ways
    float lines[2] = {}; // extend: the picture's scanline rows (grid columns) period, texels: a
                         // soft continuation blurs by whole periods, so the lines run on sharp
    float inset = 0; // extend: texels cut off the picture's sides first (art never meant to show)
    float radius = 12; // blur: texels
    float dim = 0.55f; // blur: the copy's brightness
};

struct AnchorScreen {
    std::string name;
    std::vector<int> states; // the front end's states (AppState) it covers
    // (shows ID...): only while one of these registry sprites is drawn (a state's pages with
    // different art); a screen without it covers its states otherwise
    std::vector<int> shows;
    std::vector<AnchorRule> rules;
    std::uint32_t backdrop = 0; // ARGB behind everything over the whole window, 0 none
    bool hud = false; // takes in the hud group: HUD SIZE scales its corner pieces (anchors_apply)
};

struct UiAnchors {
    std::vector<AnchorScreen> screens;
};

// assets/ui/anchors.sexp; false with the error when it can't be read
bool load_ui_anchors(UiAnchors& anchors, const std::string& path, std::string& error);
// the screen for a front-end state, null when it has none (everything centred): the first of
// the state's screens whose shown sprites are in draw (when given), else its first without any
const AnchorScreen* ui_anchor_screen(const UiAnchors& anchors, int state,
                                     const Draw2D* draw = nullptr);

// the anchor of a sprite (-1 none) or slot drawn over rect (x, y, w, h) in its view's screen
// (view_rect: that view's rectangle, null the one view)
AnchorKind anchor_kind_of(const AnchorScreen* screen, int sprite, int slot, const float rect[4],
                          const float* view_rect, const AnchorRule** rule = nullptr);
// the slot a scrolling text line's end is asked as (ticker-end rules): its clip's left or right
// end moves with what lies beside it (a footer opened at a column)
inline constexpr int anchor_slot_ticker_end = -2;
// how far a scrolling line's clip end at (x, y) of the 640x480 moves: by the frame's edge when a
// ticker-end rule anchors it left or right, else 0
float anchor_ticker_end_shift(const AnchorScreen* screen, const SpriteFrame& frame, float x,
                              float y);
// a hit box moved with its anchor (left and right by the frame's edge)
PointerRect anchor_rect(const AnchorScreen* screen, const SpriteFrame& frame,
                        const PointerRect& rect);

// ui_anchor_shapes.cpp: q (anchored free) continued to the window's edges by an extend rule, or
// behind a blurred, dimmed copy of itself by a blur rule, into out
void extend_quad(const Quad2D& q, const SpriteFrame& f, const AnchorRule& rule,
                 std::vector<Quad2D>& out);
void blur_quad(const Quad2D& q, const SpriteFrame& f, const AnchorRule& rule,
               std::vector<Quad2D>& out);

struct Renderer;
// the 2D list as the window shows it: every quad of an anchored screen moved by its anchor,
// backgrounds and bars extended, blurred copies behind, the backdrop under all; the quads a panel
// drew (quad_panel) by the panel's screen instead
void anchors_apply(const AnchorScreen* screen, const Renderer& renderer, const Draw2D& in,
                   Draw2D& out, const AnchorScreen* panel = nullptr);
// PHANTOMCRASH_DEV_ANCHORS=N (port-only): the Nth list written to stderr, a line a quad (sprite,
// slot, place, layer, the anchor it takes), for writing the rules
void anchors_dev_dump(const AnchorScreen* screen, const Renderer& renderer, const Draw2D& draw);
