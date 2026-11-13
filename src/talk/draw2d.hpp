#pragma once
// The 2D the original draws, as a list a renderer implements: textured quads in 640x480 screen
// pixels with a texel rectangle on a texture slot, an ARGB colour, an alpha multiplier and the
// sprite's layer (Sprite.blend), drawn in the original's overlay order (decomp docs/port/render.md,
// "Sprites"). The game draws it through the renderer's sprite layer (render/vk_sprites.hpp);
// talk_canvas.cpp draws it on the CPU for the conversation debug player.
#include <cstdint>
#include <vector>

// Sprite.flags (pass_draw_overlay, render_command_submit_pass)
inline constexpr std::uint32_t sprite_colour_only = 0x1; // no texture: the colour adds
inline constexpr std::uint32_t sprite_texels = 0x2;      // texel coordinates
inline constexpr std::uint32_t sprite_smooth = 0x10;     // bilinear, no pixel snap
inline constexpr std::uint32_t sprite_blink = 0x20;   // z pulses over 80 ticks from the last input
inline constexpr std::uint32_t sprite_hidden = 0x100; // z forced to 0
inline constexpr std::uint32_t sprite_shown = 0x200;  // z forced to 1
inline constexpr std::uint32_t sprite_mirror_u = 0x400;
inline constexpr std::uint32_t sprite_mirror_v = 0x800;
inline constexpr std::uint32_t sprite_blink_soft = 0x2000; // the pulse held over 0.3
inline constexpr std::uint32_t sprite_blink_hard = 0x4000; // the pulse on or off
inline constexpr std::uint32_t sprite_blinks = sprite_blink | sprite_blink_soft | sprite_blink_hard;

struct Quad2D {
    int slot = -1;        // texture slot (registry handle + portrait bias); -1 a solid colour
    float u0 = 0, v0 = 0; // texel rectangle on the slot's texture
    float u1 = 0, v1 = 0;
    float sheet_w = 1, sheet_h = 1; // the size the registry declares for the sheet
    float x = 0, y = 0;             // top left, screen pixels
    float w = 0, h = 0;
    std::uint32_t color = 0xFFFFFFFF; // ARGB, multiplies the texel
    float alpha = 1.0f;               // Sprite.z: fades and blinks
    int layer = 0x101;                // Sprite.blend: pass and sort key
    int sequence = 0;                 // submission order, the tie break
    // Sprite.rot: the pivot's offset from the top left and the angle about z in radians
    // (pass_draw_overlay turns the corners about it)
    float rot[3] = {};
    std::uint32_t flags = 0; // sprite_* bits, and the port's quad_full_window
    // Sprite.view: -1 the pass's own viewport; 0..3 a split-screen view's (the HUD's sprites get
    // their player's view, so pass 12 draws them through that viewport, render.md)
    int view = -1;
    // the port's widescreen anchor (QuadAnchor): which clip the sprite layer gives the quad
    std::uint8_t anchor = 0;
    // port-only: a blurred copy (a widescreen backdrop), the blur's radius in texels; 0 sharp
    float blur = 0;
    // port-only, with blur: the period in texel rows (and columns) of the scanlines (a grid)
    // drawn into the picture; the blur's taps step by whole periods that way, so the lines stay
    // sharp and in phase while the picture between them softens. 0 none
    float blur_period[2] = {0, 0};
    // port-only: the alpha at the quad's left and right edges as screen x goes, 1 none (a picture
    // fading into its widescreen continuation over a band, so there's no seam)
    float fade[2] = {1, 1};
};

// port-only: how a quad sits in a window wider than 4:3 (docs/screens.md "Widescreen"). The front
// end resolves anchor_auto from the screen's anchor table (src/screens/ui/ui_anchors.hpp) and moves
// the quad; the sprite layer then clips it: centre to the box's title-safe area as retail, left
// and right to that area moved out with them, free to the whole window
enum QuadAnchor : std::uint8_t {
    anchor_auto = 0, // the anchor table decides; centre when nothing resolves it
    anchor_centre,
    anchor_left,
    anchor_right,
    anchor_free, // in place, clipped to the window: world points, extended backgrounds and bars
};

// port-only flag: a layer the original draws over its whole picture (fades, the letterbox, the
// pause's dim, the hit static, the dissolve's frame, veils). Its 640x480 position is stretched over
// the whole window, unclipped, where the rest of the 2D keeps its height in the centred 4:3 box
// and is anchored out from it: the port's 3D fills the window, retail's filled the screen
// (docs/screens.md "Widescreen")
inline constexpr std::uint32_t quad_full_window = 0x10000000;

struct Draw2D {
    std::vector<Quad2D> quads;
};

// overlay passes by layer (render_sprite_quad): 0..8 under the 3D (pass 0), 0x1000..0x7FFE
// pass 14, 0x7FFF the fade (pass 15, the full 640x480), the rest pass 12
int overlay_pass(int layer);
// passes 0 and 12 draw in the 576x448 title-safe viewport at 32,16; 14 and 15 the full screen
bool overlay_clipped(int layer);
void submit_quad(Draw2D& draw, Quad2D quad);
// the draw order: by pass, then layer, then submission (retail's quicksort leaves equal layers in
// no defined order; this keeps them in submission order)
void sort_quads(Draw2D& draw);
