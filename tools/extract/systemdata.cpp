#include "systemdata.hpp"

#include "extract.hpp"
#include "png.hpp"
#include "sexp_out.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

// the loose pictures the renderer loads by name (shaders_init 0xBEE60, render_device_setup):
// bump_light.tga (bump and effopt's lookup, D_00A7B8D4), yurayuraTest.bmp and wave02.bmp (the water
// bump), muteki01.tga (the shield bubble), env0.bmp, kagero_a01/a02.bmp, hishakai_test1.tga
// an uncompressed true-colour TGA (type 2, 24 or 32 bits), which this SDL_image build leaves out
SDL_Surface* load_tga(const std::vector<std::uint8_t>& file) {
    const std::uint8_t* bytes = file.data();
    const std::size_t size = file.size();
    SDL_Surface* out = nullptr;
    if (bytes && size >= 18 && bytes[2] == 2 && (bytes[16] == 24 || bytes[16] == 32)) {
        const int width = bytes[12] | bytes[13] << 8, height = bytes[14] | bytes[15] << 8;
        const int depth = bytes[16] / 8;
        const bool top_down = bytes[17] & 0x20;
        const std::size_t first = 18 + bytes[0];
        if (first + static_cast<std::size_t>(width) * height * depth <= size) {
            out = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ABGR8888);
            for (int y = 0; y < height && out; ++y) {
                const int row = top_down ? y : height - 1 - y;
                std::uint8_t* to = static_cast<std::uint8_t*>(out->pixels) + y * out->pitch;
                const std::uint8_t* from = bytes + first + static_cast<std::size_t>(row) * width * depth;
                for (int x = 0; x < width; ++x, from += depth, to += 4) {
                    to[0] = from[2];
                    to[1] = from[1];
                    to[2] = from[0];
                    to[3] = depth == 4 ? from[3] : 255;
                }
            }
        }
    }
    if (!out) {
        SDL_SetError("not an uncompressed true-colour TGA");
    }
    return out;
}

bool convert(const std::vector<std::uint8_t>& bytes, bool tga, const std::string& to, int& width,
             int& height) {
    SDL_Surface* loaded =
        tga ? load_tga(bytes) : SDL_LoadBMP_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true);
    SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888) : nullptr;
    bool ok = false;
    if (rgba) {
        width = rgba->w;
        height = rgba->h;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            std::memcpy(&pixels[static_cast<std::size_t>(y) * width * 4],
                        static_cast<const std::uint8_t*>(rgba->pixels) +
                            static_cast<std::size_t>(y) * rgba->pitch,
                        static_cast<std::size_t>(width) * 4);
        }
        ok = write_png(to, width, height, pixels);
    }
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(loaded);
    return ok;
}

} // namespace

bool extract_systemdata(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/textures/systemdata", made);
    int written = 0;
    for (const std::string& name : disc_list(disc.source, "Media/systemdata")) {
        const std::size_t dot = name.rfind('.');
        const std::string stem = name.substr(0, dot);
        const bool tga = dot != std::string::npos && name.substr(dot) == ".tga";
        const std::string file = "textures/systemdata/" + stem + ".png";
        int width = 0, height = 0;
        if (!convert(disc_file(disc.source, "Media/systemdata/" + name), tga,
                     options.out_dir + "/" + file, width, height)) {
            std::fprintf(stderr, "systemdata: can't convert %s: %s\n", name.c_str(),
                         SDL_GetError());
            continue;
        }
        manifest.entries.push_back("(picture (source " + sx_string("Media/systemdata/" + name) +
                                   ") (size " + sx_int(width) + " " + sx_int(height) +
                                   ") (file " + sx_string(file) + "))");
        ++written;
    }
    std::printf("systemdata: %d pictures\n", written);
    return written > 0;
}
