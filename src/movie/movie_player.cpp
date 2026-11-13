// The movie player (docs/movies.md): one movie at a time, its picture decoded up to the clock its
// sound keeps, as Sofdec syncs the picture to the ADX sound.
#include "audio/audio.hpp"
#include "movie/movie.hpp"

#include <SDL3/SDL_log.h>
#include <cmath>
#include <fstream>
#include <gsexp/sexp.hpp>
#include <sstream>

namespace {

// the sound counts as over this close to its length (the resampler may hold back a few frames)
constexpr double sound_end_slack = 0.02;
// or when it hasn't moved for this long within half a second of its end
constexpr double sound_stall_seconds = 0.25;
// or for this long anywhere (no sound device): the clock goes on in wall time
constexpr double sound_lost_seconds = 1.0;

void stop_playing(MoviePlayer& player, Audio& audio) {
    if (player.sound) {
        stop_movie_audio(audio);
    }
    close_movie_decoder(player.decoder);
    player.movie = -1;
    player.error = false;
    player.sound = false;
    player.sound_over = false;
    player.sound_stalled = 0;
    player.clock = 0;
    player.picture_over = false;
}

// the decoder's last frame into the image
void show_frame(MoviePlayer& player) {
    MovieImage& image = player.image;
    const std::size_t bytes = static_cast<std::size_t>(player.decoder.width) *
                              static_cast<std::size_t>(player.decoder.height) * 4;
    if (image.rgba.size() != bytes) {
        image.rgba.assign(bytes, 0);
    }
    image.width = player.decoder.width;
    image.height = player.decoder.height;
    movie_frame_to_rgba(player.decoder, image.rgba.data(), image.width * 4);
    image.frame = player.decoder.frames - 1;
    ++image.serial;
}

// the clock: the sound's while it plays, wall time after it or without it
void advance_clock(MoviePlayer& player, const Audio& audio, double seconds) {
    if (!player.sound || player.sound_over) {
        player.clock += seconds;
        return;
    }
    const double length = player.files[player.movie].sound_seconds;
    const double heard = movie_audio_seconds(audio);
    if (heard >= length - sound_end_slack) {
        player.sound_over = true;
    } else if (heard <= player.clock) {
        // not moving: near its end it has ended; anywhere else no device is pulling the mix (none,
        // or lost), and the picture goes on in wall time rather than wait for one
        player.sound_stalled += seconds;
        const double limit = heard > length - 0.5 ? sound_stall_seconds : sound_lost_seconds;
        player.sound_over = player.sound_stalled >= limit;
    } else {
        player.sound_stalled = 0;
    }
    player.clock = heard > player.clock ? heard : player.clock;
}

} // namespace

bool load_movie_files(MoviePlayer& player, const std::string& data_dir) {
    const std::string path = data_dir + "/audio/movies.sexp";
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    if (in) {
        text << in.rdbuf();
    }
    const gsexp::ParseResult parsed = gsexp::parse_owned(text.str());
    if (!in || !parsed.ok) {
        SDL_Log("movie: can't read %s", path.c_str());
        return false;
    }
    player.files.assign(movie_count, MovieFile{});
    int found = 0;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const int index = form.get_int("index").value_or(-1);
        if (!form.head().is_atom("movie") || index < 0 || index >= movie_count) {
            continue;
        }
        MovieFile& file = player.files[index];
        file.name = form.get_string("name").value_or("");
        file.video = data_dir + "/" + form.get_string("file").value_or("");
        const gsexp::FormView sound(form.find("audio"));
        if (sound.valid() && sound.get_string("file")) {
            file.sound = data_dir + "/" + *sound.get_string("file");
            file.sound_seconds = sound.get_float("seconds").value_or(0.0f);
        }
        ++found;
    }
    if (found != movie_count) {
        SDL_Log("movie: %s has %d of the %d movies", path.c_str(), found, movie_count);
    }
    return found > 0;
}

bool open_movie_player(MoviePlayer& player) {
    if (player.opened) {
        return false;
    }
    player.opened = true;
    player.error = false;
    return true;
}

void close_movie_player(MoviePlayer& player, Audio& audio) {
    stop_playing(player, audio);
    player.opened = false;
}

void play_movie(MoviePlayer& player, Audio& audio, int movie) {
    stop_playing(player, audio);
    player.movie = movie;
    const bool known = movie >= 0 && movie < static_cast<int>(player.files.size()) &&
                       !player.files[movie].video.empty();
    if (!known || !open_movie_decoder(player.decoder, player.files[movie].video.c_str())) {
        SDL_Log("movie: can't play movie %d", movie);
        player.error = true;
        return;
    }
    // the first picture is up at once, the sound starts with it
    if (decode_movie_frame(player.decoder)) {
        show_frame(player);
    }
    const MovieFile& file = player.files[movie];
    player.sound = !file.sound.empty() && play_movie_audio(audio, file.sound);
}

void service_movie(MoviePlayer& player, const Audio& audio, double seconds) {
    if (player.movie < 0 || player.error) {
        return;
    }
    advance_clock(player, audio, seconds);

    // decode up to the frame whose time has come; only the last one is converted
    const double rate = player.decoder.frame_rate > 0 ? player.decoder.frame_rate : 29.97;
    const int due = static_cast<int>(std::floor(player.clock * rate));
    bool decoded = false;
    while (player.decoder.frames - 1 < due && decode_movie_frame(player.decoder)) {
        decoded = true;
    }
    if (decoded) {
        show_frame(player);
    }
    // the last picture has had its frame's time
    if (player.decoder.ended && player.clock * rate >= player.decoder.frames) {
        player.picture_over = true;
    }
}

bool movie_done(const MoviePlayer& player) {
    if (player.error) {
        return true;
    }
    return player.movie >= 0 && player.picture_over && (!player.sound || player.sound_over);
}
