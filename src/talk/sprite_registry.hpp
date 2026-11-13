#pragma once
// The render registry and the portrait tables, read from the ui family's data (data/ui/images.sexp,
// data/ui/layouts/portraits.sexp; docs/ui-assets.md), and the texture slots a screen has loaded.
// A sprite id is a texel rectangle on a texture slot; the slot holds whichever texture set was
// loaded into it last (decomp docs/port/data.md section 16), so a screen binds its sets first.
#include "talk/draw2d.hpp"

#include <map>
#include <string>
#include <vector>

struct RegistrySprite {
    int slot = -1;
    int asset = -1;     // the texture set the ui family cut it from (the set its screen loads)
    float rect[4] = {}; // left, top, right, bottom in texels
    float sheet[2] = {1, 1};
};

// PortraitSprite (portrait_sprite_table): a base, three mouth phases with offsets, the eyes overlay
struct PortraitSprite {
    int base = -1;
    int phase[3] = {-1, -1, -1};
    int overlay = -1;
    int phase_offset[3][2] = {};
    int overlay_offset[2] = {};
};

struct PortraitActor {
    int texture_asset = -1; // the texture set loaded for it
    int expression[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// a type 3 texture set in the manifest: its descriptor's first slot and its textures' PNGs
struct TextureSetFiles {
    int first_slot = -1;
    std::vector<std::string> files; // full paths, by texture index
};

struct SpriteRegistry {
    std::vector<RegistrySprite> sprites;         // by registry id
    std::map<int, TextureSetFiles> texture_sets; // by asset id
    std::vector<PortraitActor> actors;           // 28
    std::vector<PortraitSprite> portraits;       // 180
    int bias[28] = {};                           // portrait_bias_table, by cast slot
};

// texture slot -> the PNG it holds now (data/textures/<set>/<index>.png)
struct SlotBindings {
    std::map<int, std::string> files;
};

bool load_sprite_registry(SpriteRegistry& registry, const std::string& data_dir,
                          std::string& error);
// texture_set_load: a set's textures into its descriptor's slots (from the manifest), or into
// first_slot when given (portraits load at 534 + bias)
bool bind_texture_set(SlotBindings& bindings, const SpriteRegistry& registry, int asset,
                      int first_slot = -1);
// render_sprite_submit: a registry sprite at x, y (top left) scaled by the sprite's texel size,
// its slot moved by bias; nothing for an id the registry doesn't have
void submit_sprite(Draw2D& draw, const SpriteRegistry& registry, int id, float x, float y,
                   int layer, std::uint32_t color, float alpha, int bias = 0);
