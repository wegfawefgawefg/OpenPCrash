#include "cook/cook_textures.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <bc7enc.h>
#include <cstring>
#include <mutex>
#include <rgbcx.h>

namespace {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

bool load_png(const std::string& path, Image& out, std::string& error) {
    SDL_Surface* loaded = IMG_Load(path.c_str());
    SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888) : nullptr;
    SDL_DestroySurface(loaded);
    if (!rgba) {
        error = "can't load " + path + ": " + SDL_GetError();
        return false;
    }
    out.width = rgba->w;
    out.height = rgba->h;
    out.rgba.resize(static_cast<std::size_t>(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y) {
        std::memcpy(&out.rgba[static_cast<std::size_t>(y) * rgba->w * 4],
                    static_cast<const std::uint8_t*>(rgba->pixels) +
                        static_cast<std::size_t>(y) * rgba->pitch,
                    static_cast<std::size_t>(rgba->w) * 4);
    }
    SDL_DestroySurface(rgba);
    return true;
}

// the next mip down: a 2x2 box, an odd edge's last texel kept (what the renderer made at load
// before the cook)
Image half_size(const Image& in) {
    Image out;
    out.width = in.width > 1 ? in.width / 2 : 1;
    out.height = in.height > 1 ? in.height / 2 : 1;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int k = 0; k < 4; ++k) {
                    const int sx = std::min(x * 2 + (k & 1), in.width - 1);
                    const int sy = std::min(y * 2 + (k >> 1), in.height - 1);
                    sum += in.rgba[(static_cast<std::size_t>(sy) * in.width + sx) * 4 + c];
                }
                out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4 + c] =
                    static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

// opaque: BC1. Alpha the original kept in DXT1 or DXT5 at this size: BC3, as shipped. Alpha from
// an uncompressed original, a 2D sprite or a replaced (resized) picture: BC7.
TextureFormat pick_format(const Image& image, const TextureHint* hint) {
    bool opaque = true;
    for (std::size_t i = 3; i < image.rgba.size() && opaque; i += 4) {
        opaque = image.rgba[i] == 255;
    }
    if (opaque) {
        return TextureFormat::bc1;
    }
    const bool as_shipped = hint && (hint->format == "DXT1" || hint->format == "DXT5") &&
                            hint->width == image.width && hint->height == image.height;
    return as_shipped ? TextureFormat::bc3 : TextureFormat::bc7;
}

void encode_level(const Image& level, TextureFormat format, std::uint8_t* out) {
    bc7enc_compress_block_params bc7;
    if (format == TextureFormat::bc7) {
        bc7enc_compress_block_params_init(&bc7);
    }
    const std::uint32_t block_bytes = texture_block_bytes(format);
    std::uint8_t block[64];
    for (int by = 0; by < (level.height + 3) / 4; ++by) {
        for (int bx = 0; bx < (level.width + 3) / 4; ++bx) {
            // a partial block repeats its edge texels
            for (int y = 0; y < 4; ++y) {
                const int sy = std::min(by * 4 + y, level.height - 1);
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(bx * 4 + x, level.width - 1);
                    std::memcpy(&block[(y * 4 + x) * 4],
                                &level.rgba[(static_cast<std::size_t>(sy) * level.width + sx) * 4],
                                4);
                }
            }
            switch (format) {
            case TextureFormat::bc1:
                rgbcx::encode_bc1(10, out, block, false, false);
                break;
            case TextureFormat::bc3:
                rgbcx::encode_bc3(10, out, block);
                break;
            case TextureFormat::bc7:
                bc7enc_compress_block(out, block, &bc7);
                break;
            }
            out += block_bytes;
        }
    }
}

} // namespace

void cook_textures_init() {
    static std::once_flag once;
    std::call_once(once, [] {
        rgbcx::init();
        bc7enc_compress_block_init();
    });
}

bool cook_texture(const std::string& path, const TextureHint* hint, bool mips, CookedTexture& out,
                  std::string& error) {
    Image image;
    if (!load_png(path, image, error)) {
        return false;
    }
    out.format = pick_format(image, hint);
    out.width = static_cast<std::uint32_t>(image.width);
    out.height = static_cast<std::uint32_t>(image.height);
    std::vector<Image> levels;
    levels.push_back(std::move(image));
    while (mips && (levels.back().width > 1 || levels.back().height > 1)) {
        levels.push_back(half_size(levels.back()));
    }
    out.levels = static_cast<std::uint32_t>(levels.size());
    std::uint64_t total = 0;
    for (const Image& level : levels) {
        total += texture_level_bytes(out.format, level.width, level.height);
    }
    out.blocks.resize(total);
    std::uint8_t* at = out.blocks.data();
    for (const Image& level : levels) {
        encode_level(level, out.format, at);
        at += texture_level_bytes(out.format, level.width, level.height);
    }
    return true;
}
