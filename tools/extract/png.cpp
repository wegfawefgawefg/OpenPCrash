#include "png.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

bool write_png(const std::string& path, int width, int height,
               const std::vector<std::uint8_t>& rgba) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ABGR8888,
                                                 const_cast<std::uint8_t*>(rgba.data()), width * 4);
    if (!surface) {
        return false;
    }
    const bool saved = IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}
