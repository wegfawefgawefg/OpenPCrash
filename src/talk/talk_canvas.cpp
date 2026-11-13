#include "talk/talk_canvas.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// the title-safe viewport passes 0 and 12 draw in (viewport_rect, one view: 32,16 576x448)
constexpr int safe_left = 32;
constexpr int safe_top = 16;
constexpr int safe_right = 32 + 576;
constexpr int safe_bottom = 16 + 448;

const CanvasTexture* texture_for(TalkCanvas& canvas, const std::string& path) {
    const auto found = canvas.loaded.find(path);
    if (found != canvas.loaded.end()) {
        return found->second.rgba.empty() ? nullptr : &found->second;
    }
    CanvasTexture& texture = canvas.loaded[path];
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        std::fprintf(stderr, "talk canvas: %s: %s\n", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) {
        return nullptr;
    }
    texture.width = rgba->w;
    texture.height = rgba->h;
    texture.rgba.resize(static_cast<std::size_t>(rgba->w) * static_cast<std::size_t>(rgba->h) * 4);
    for (int row = 0; row < rgba->h; ++row) {
        std::copy_n(static_cast<const std::uint8_t*>(rgba->pixels) + row * rgba->pitch, rgba->w * 4,
                    texture.rgba.data() + static_cast<std::size_t>(row) * rgba->w * 4);
    }
    SDL_DestroySurface(rgba);
    return &texture;
}

void draw_quad(TalkCanvas& canvas, const SlotBindings& bindings, const Quad2D& quad) {
    const CanvasTexture* texture = nullptr;
    if (quad.slot >= 0) {
        const auto bound = bindings.files.find(quad.slot);
        if (bound == bindings.files.end()) {
            return; // nothing loaded in that slot: the original draws whatever was there last
        }
        texture = texture_for(canvas, bound->second);
        if (!texture) {
            return;
        }
    }

    // whole-pixel corners (positions are floored unless flag 0x10), clipped to the viewport
    const int x0 = static_cast<int>(std::floor(quad.x));
    const int y0 = static_cast<int>(std::floor(quad.y));
    const int x1 = x0 + static_cast<int>(std::lround(quad.w));
    const int y1 = y0 + static_cast<int>(std::lround(quad.h));
    const bool clipped = overlay_clipped(quad.layer);
    const int left = std::max(x0, clipped ? safe_left : 0);
    const int top = std::max(y0, clipped ? safe_top : 0);
    const int right = std::min(x1, clipped ? safe_right : canvas.width);
    const int bottom = std::min(y1, clipped ? safe_bottom : canvas.height);
    if (left >= right || top >= bottom) {
        return;
    }

    // the colour multiplies the texel; alpha is texel x colour x z
    const float cr = static_cast<float>(quad.color >> 16 & 0xFF) / 255.0f;
    const float cg = static_cast<float>(quad.color >> 8 & 0xFF) / 255.0f;
    const float cb = static_cast<float>(quad.color & 0xFF) / 255.0f;
    const float ca = static_cast<float>(quad.color >> 24 & 0xFF) / 255.0f * quad.alpha;
    const float du = x1 > x0 ? (quad.u1 - quad.u0) / static_cast<float>(x1 - x0) : 0.0f;
    const float dv = y1 > y0 ? (quad.v1 - quad.v0) / static_cast<float>(y1 - y0) : 0.0f;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            float r = cr, g = cg, b = cb, a = ca;
            if (texture) {
                // point sampling at the texel the pixel's centre falls in, the sheet's declared
                // size mapped onto the PNG's
                const float u = quad.u0 + (static_cast<float>(x - x0) + 0.5f) * du;
                const float v = quad.v0 + (static_cast<float>(y - y0) + 0.5f) * dv;
                int tx = static_cast<int>(u * static_cast<float>(texture->width) / quad.sheet_w);
                int ty = static_cast<int>(v * static_cast<float>(texture->height) / quad.sheet_h);
                tx = std::clamp(tx, 0, texture->width - 1);
                ty = std::clamp(ty, 0, texture->height - 1);
                const std::uint8_t* texel =
                    texture->rgba.data() + (static_cast<std::size_t>(ty) * texture->width + tx) * 4;
                r *= static_cast<float>(texel[0]) / 255.0f;
                g *= static_cast<float>(texel[1]) / 255.0f;
                b *= static_cast<float>(texel[2]) / 255.0f;
                a *= static_cast<float>(texel[3]) / 255.0f;
            }
            if (a <= 0.0f) {
                continue; // alpha kill
            }
            std::uint8_t* out =
                canvas.rgba.data() + (static_cast<std::size_t>(y) * canvas.width + x) * 4;
            const float keep = 1.0f - a;
            out[0] = static_cast<std::uint8_t>(std::lround(r * a * 255.0f + out[0] * keep));
            out[1] = static_cast<std::uint8_t>(std::lround(g * a * 255.0f + out[1] * keep));
            out[2] = static_cast<std::uint8_t>(std::lround(b * a * 255.0f + out[2] * keep));
            out[3] = 255;
        }
    }
}

} // namespace

void canvas_draw(TalkCanvas& canvas, const SlotBindings& bindings, Draw2D& draw,
                 std::uint32_t clear) {
    canvas.rgba.resize(static_cast<std::size_t>(canvas.width) * canvas.height * 4);
    for (std::size_t i = 0; i < canvas.rgba.size(); i += 4) {
        canvas.rgba[i] = static_cast<std::uint8_t>(clear >> 16);
        canvas.rgba[i + 1] = static_cast<std::uint8_t>(clear >> 8);
        canvas.rgba[i + 2] = static_cast<std::uint8_t>(clear);
        canvas.rgba[i + 3] = 255;
    }
    sort_quads(draw);
    for (const Quad2D& quad : draw.quads) {
        draw_quad(canvas, bindings, quad);
    }
}

bool canvas_save_png(const TalkCanvas& canvas, const std::string& path, int scale) {
    scale = std::max(scale, 1);
    const int width = canvas.width * scale;
    const int height = canvas.height * scale;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::copy_n(canvas.rgba.data() +
                            (static_cast<std::size_t>(y / scale) * canvas.width + x / scale) * 4,
                        4, pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4);
        }
    }
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4);
    if (!surface) {
        return false;
    }
    const bool saved = IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}
