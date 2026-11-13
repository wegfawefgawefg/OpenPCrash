#pragma once
// The 2D sprite layer: the original's overlay passes (decomp docs/port/render.md, "Sprites, text
// and the 2D overlay"; render_sprite_quad, pass_draw_overlay, spritemat). Draws the game's one 2D
// list (talk/draw2d.hpp: menus, conversations, movies, fades) in the original's 640x480 screen
// pixels, fitted 4:3 into the window with bars. Passes 0, 12 and 14 draw through the 576x448
// title-safe viewport at (32, 16), so they are clipped to it; 15 (the fade) through the whole
// 640x480, as the original sets them (render.md "Views and cameras"). A quad flagged
// quad_full_window (the fades, the letterbox, the static...) is placed the same way but has the
// 640x480 stretched over the whole window and no clip, so it covers the port's full-window 3D.
// In a wide window each pass's viewport reaches past the box to the window's edges
// (sprite_frame.hpp) and each quad is clipped by its anchor (talk/draw2d.hpp QuadAnchor): centre
// to the box's title-safe area as before, left and right to that area moved out by the frame's
// edge, free to the window.
#include "render/renderer.hpp"
#include "render/sprite_frame.hpp"
#include "talk/draw2d.hpp"

#include <cstdint>
#include <vector>

// what a texture slot holds now: a renderer texture and its size, or -1
struct SlotTexture {
    int texture = -1;
    int width = 1, height = 1;
};

// a sprite sheet or a movie picture without mips (the overlay samples level 0 only); -1 on failure
int renderer_upload_sprite_texture(Renderer& renderer, int width, int height,
                                   const std::vector<std::uint8_t>& rgba);
// replaces an uploaded sprite texture's pixels, same size (the movie picture each frame)
bool renderer_update_sprite_texture(Renderer& renderer, int texture,
                                    const std::vector<std::uint8_t>& rgba);
// the list the next renderer_draw_frame draws, slots resolved through slot_textures (indexed by
// slot; a quad with slot -1 is a solid colour). It is sorted there: pass, layer, submission.
void renderer_set_sprites(Renderer& renderer, const Draw2D& draw,
                          const std::vector<SlotTexture>& slot_textures);
// adds to that list (the battle's effects over the HUD's set: src/render/scenery.cpp)
void renderer_add_sprites(Renderer& renderer, const Draw2D& draw,
                          const std::vector<SlotTexture>& slot_textures);

// the split-screen views' rectangles in the 640x480 screen (x, y, width, height), which a quad
// with a view draws through in passes 0 and 12 (positions relative to that view's corner plus the
// safe origin, as retail's per-view viewports place them), and where each view's 3D is drawn (its
// 2D is centred on that in a wide window); count 0 clears them
void renderer_set_sprite_views(Renderer& renderer, const float (*rects)[4],
                               const float (*drawn)[4], int count);
// the frame a view's 2D (or the one view's, -1) is placed in at the window's shape now
// (render/sprite_frame.hpp), and the views' count and rectangles as set
SpriteFrame renderer_sprite_frame(const Renderer& renderer, int view);
int renderer_sprite_view_count(const Renderer& renderer);
const float* renderer_sprite_view_rect(const Renderer& renderer, int view);

// inside the renderer (vk_frame.cpp, vk_device.cpp)
struct RendererState;
enum class SpriteLayer { Under, Over, Over12, Over14 };
// Under: pass 0, inside the scene pass before the meshes (writes every pass's vertices; the caller
// sets the meshes' viewport and scissor after); Over: passes 12, 14, 15, a pass of their own on
// the final target after the composite; Over12 and Over14 the same split around pass 13's 3D
// (12 alone, then 14 and 15)
void vk_sprites_record(RendererState& vk, SpriteLayer layer);
void vk_sprites_destroy(RendererState& vk);
