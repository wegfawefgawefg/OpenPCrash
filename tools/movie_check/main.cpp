// pc_movie_check: decodes every movie offline and prints what it found (docs/movies.md,
// "Checking"), and runs the movie screens without a window or a sound device.
//
//   pc_movie_check --data DIR [--png DIR] [--screens] [--movie N] [--realtime N [--no-device]]
//
// Decoding: per movie the picture's size, frame rate, frame count and length against its sound's,
// the decode time per frame, and with --png four frames each as PNG (at 1 s and a quarter, half
// and three quarters of the way) and the last. --screens plays every movie screen at 60 updates a
// second with the sound engine rendering offline, once untouched and once pressing accept or any
// button every update, and prints when each ends and where it goes. --realtime N plays movie N in
// real time through an SDL sound device (run it with SDL_AUDIO_DRIVER=dummy or disk, never on
// speakers) and prints its clock against the wall; --no-device opens none, so nothing plays the
// sound and the picture has to go on by itself.
#include "movie/movie.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// screens_run.cpp
void run_movie_screens(const std::string& data_dir);
void run_movie_realtime(const std::string& data_dir, int movie, bool device);

namespace {

bool save_png(const MovieDecoder& decoder, const std::string& path) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(decoder.width) * decoder.height * 4);
    movie_frame_to_rgba(decoder, rgba.data(), decoder.width * 4);
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        decoder.width, decoder.height, SDL_PIXELFORMAT_RGBA32, rgba.data(), decoder.width * 4);
    const bool saved = surface && IMG_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    return saved;
}

// one movie through the decoder: counts, timing, and the PNGs
bool check_movie(int index, const MovieFile& file, const std::string& png_dir) {
    MovieDecoder decoder;
    if (!open_movie_decoder(decoder, file.video.c_str())) {
        std::printf("%2d %-17s can't be decoded (%s)\n", index, file.name.c_str(),
                    file.video.c_str());
        return false;
    }
    const double rate = decoder.frame_rate;
    const double sound = file.sound_seconds;
    std::vector<int> dumps;
    for (double t : {1.0, sound * 0.25, sound * 0.5, sound * 0.75}) {
        dumps.push_back(static_cast<int>(t * rate));
    }
    const int width = decoder.width;
    const int height = decoder.height;
    int odd_sizes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (decode_movie_frame(decoder)) {
        const MoviePlanes planes = movie_planes(decoder);
        odd_sizes += planes.width != width || planes.height != height ? 1 : 0;
        const int frame = decoder.frames - 1;
        for (int dump : dumps) {
            if (dump == frame && !png_dir.empty()) {
                char name[64];
                std::snprintf(name, sizeof name, "/%02d_%s_%05d.png", index,
                              file.name.substr(0, file.name.rfind('.')).c_str(), frame);
                if (!save_png(decoder, png_dir + name)) {
                    std::printf("   can't write %s%s: %s\n", png_dir.c_str(), name, SDL_GetError());
                }
            }
        }
    }
    const double took =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const int frames = decoder.frames;
    // and the last frame (the one pl_mpeg upstream drops when a stream ends on B-frames)
    if (!png_dir.empty() && frames > 0) {
        char name[64];
        std::snprintf(name, sizeof name, "/%02d_%s_%05d_last.png", index,
                      file.name.substr(0, file.name.rfind('.')).c_str(), frames - 1);
        save_png(decoder, png_dir + name);
    }
    const double length = frames / rate;
    std::printf("%2d %-17s %dx%d %.3f fps %6d frames %8.3f s  sound %8.3f s  picture-sound "
                "%+7.3f s  %.2f ms/frame%s\n",
                index, file.name.c_str(), width, height, rate, frames, length, sound,
                length - sound, frames ? took * 1000.0 / frames : 0.0,
                odd_sizes ? "  (frames of another size)" : "");
    close_movie_decoder(decoder);
    return frames > 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string data_dir = PHANTOMCRASH_DATA_DIR_DEFAULT;
    std::string png_dir;
    bool screens = false;
    int only = -1;
    int realtime = -1;
    bool device = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--png" && i + 1 < argc) {
            png_dir = argv[++i];
        } else if (arg == "--movie" && i + 1 < argc) {
            only = std::atoi(argv[++i]);
        } else if (arg == "--realtime" && i + 1 < argc) {
            realtime = std::atoi(argv[++i]);
        } else if (arg == "--no-device") {
            device = false;
        } else if (arg == "--screens") {
            screens = true;
        } else {
            std::fprintf(stderr, "pc_movie_check --data DIR [--png DIR] [--screens] [--movie N] "
                                 "[--realtime N [--no-device]]\n");
            return 2;
        }
    }

    MoviePlayer player;
    if (!load_movie_files(player, data_dir)) {
        return 1;
    }
    if (screens) {
        run_movie_screens(data_dir);
        return 0;
    }
    if (realtime >= 0) {
        run_movie_realtime(data_dir, realtime, device);
        return 0;
    }
    int good = 0;
    for (int i = 0; i < movie_count; ++i) {
        if (only < 0 || only == i) {
            good += check_movie(i, player.files[i], png_dir) ? 1 : 0;
        }
    }
    std::printf("%d movies decoded\n", good);
    return good == (only < 0 ? movie_count : 1) ? 0 : 1;
}
