// A manual report's picture: the renderer's PNG of the paused fight, made small on the worker (at
// most 512 wide, JPEG at 60) so it costs well under 100 KiB.
#include "report/report_internal.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>

std::string base64(const std::string& bytes) {
    static constexpr char digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left = bytes.size() - i;
        const auto byte = [&](std::size_t at) {
            return at < bytes.size()
                       ? static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at]))
                       : 0u;
        };
        const std::uint32_t v = byte(i) << 16 | byte(i + 1) << 8 | byte(i + 2);
        out += digits[(v >> 18) & 63];
        out += digits[(v >> 12) & 63];
        out += left > 1 ? digits[(v >> 6) & 63] : '=';
        out += left > 2 ? digits[v & 63] : '=';
    }
    return out;
}

std::string report_shot_jpeg(const std::string& png_path) {
    SDL_Surface* full = IMG_Load(png_path.c_str());
    if (!full) {
        return {};
    }
    const int w = std::min(full->w, 512);
    const int h = std::max(1, full->h * w / std::max(1, full->w));
    SDL_Surface* small = SDL_ScaleSurface(full, w, h, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(full);
    if (!small) {
        return {};
    }
    std::string jpeg;
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    if (io && IMG_SaveJPG_IO(small, io, false, 60)) {
        const Sint64 size = SDL_GetIOSize(io);
        void* data = SDL_GetPointerProperty(SDL_GetIOProperties(io),
                                            SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
        if (data && size > 0) {
            jpeg.assign(static_cast<const char*>(data), static_cast<std::size_t>(size));
        }
    }
    SDL_CloseIO(io);
    SDL_DestroySurface(small);
    return jpeg.empty() ? std::string() : base64(jpeg);
}
