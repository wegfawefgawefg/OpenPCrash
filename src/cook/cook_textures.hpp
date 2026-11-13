#pragma once
// A PNG to BC blocks: format by its alpha and the original's format (cook_textures.cpp), mips by a
// 2x2 box down to 1x1, each level whole 4x4 blocks, top level first. Encoders: rgbcx (BC1, BC3)
// and bc7enc (BC7) from bc7enc_rdo.
#include "cook/cook_plan.hpp"
#include "cook/pack_format.hpp"

#include <string>
#include <vector>

struct CookedTexture {
    TextureFormat format = TextureFormat::bc1;
    std::uint32_t width = 0, height = 0, levels = 0;
    std::vector<std::uint8_t> blocks;
};

// once before the first cook_texture (thread-safe)
void cook_textures_init();
bool cook_texture(const std::string& path, const TextureHint* hint, bool mips, CookedTexture& out,
                  std::string& error);
