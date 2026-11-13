#pragma once
// Texture sets (kind 3, *_tex.bin): Xbox D3D texture headers over DXT1/DXT5 blocks, swizzled
// A8R8G8B8 and swizzled P8 with a palette (docs/port/data.md section 5), decoded to RGBA8, top
// level only (the converted folder holds PNGs; the game makes its own mips).
#include <cstdint>
#include <string>
#include <vector>

struct DecodedTexture {
    int width = 0;
    int height = 0;
    int levels = 0;                 // mip levels on the disc (dropped)
    std::string format;             // DXT1, DXT5, A8R8G8B8, P8
    std::vector<std::uint8_t> rgba; // width * height * 4, R G B A
};

// every texture of the set, in set order; error says why on an empty result
std::vector<DecodedTexture> decode_texture_set(const std::vector<std::uint8_t>& bytes,
                                               std::string& error);

struct Disc;
struct ExtractOptions;
struct Manifest;

// the textures family: every texture set (registry kind 3) to data/textures/<set>/<nn>.png, with
// one manifest entry per set; false on a write failure
bool extract_textures(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
