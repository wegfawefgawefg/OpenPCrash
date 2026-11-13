// pc_movie_check --screens: every movie screen run at 60 updates a second against the sound engine
// rendering offline (no device), untouched and then with a button held down, printing the movies
// it played, when each ended, where the screen goes, and how far the picture strayed from the
// sound's clock.
#include "audio/audio.hpp"
#include "movie/movie_screens.hpp"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int updates_per_second = 60;
constexpr int frames_per_update = audio_rate / updates_per_second;
constexpr int update_cap = updates_per_second * 60 * 12; // past the longest screen (credits)

const char* screen_name(MovieScreenKind kind) {
    switch (kind) {
    case MovieScreenKind::phantagram_logo:
        return "phantagram_logo";
    case MovieScreenKind::dolby_logo:
        return "dolby_logo";
    case MovieScreenKind::attract:
        return "attract";
    case MovieScreenKind::opening:
        return "opening";
    case MovieScreenKind::ending:
        return "ending";
    case MovieScreenKind::credits:
        return "credits";
    case MovieScreenKind::secret:
        return "secret";
    }
    return "?";
}

struct Run {
    int updates = 0;
    int movie = -1;      // playing now
    int movie_start = 0; // the update it started on
    double worst_sync = 0;
    int fade_left = -1; // updates until the simulated fade is black, -1 none
};

// a movie changed or ended: what it played for, and the last frame it showed
void report_movie(const Run& run, const MoviePlayer& player, int last_frame) {
    if (run.movie < 0) {
        return;
    }
    const double seconds = (run.updates - run.movie_start) / double(updates_per_second);
    std::printf("    %-15s %8.3f s  up to frame %5d\n", player.files[run.movie].name.c_str(),
                seconds, last_frame);
}

void run_screen(MovieScreenKind kind, bool press, MoviePlayer& player, Audio& audio,
                const std::vector<EndingSubtitle>& subtitles) {
    std::printf("  %s, %s\n", screen_name(kind), press ? "a button held" : "untouched");
    MovieScreen screen;
    start_movie_screen(screen, kind);
    Run run;
    std::vector<float> mix(static_cast<std::size_t>(frames_per_update) * 2);
    int subtitle = -1;
    MovieScreenStep step;
    for (; run.updates < update_cap; ++run.updates) {
        MovieScreenInput input;
        input.accept = press;
        input.any = press;
        input.fade_black = run.fade_left == 0;
        const int movie_before = player.movie;
        const int frame_before = player.image.frame;
        step = step_movie_screen(screen, player, audio, input, subtitles, 1.0 / updates_per_second);
        if (player.movie != movie_before || (player.movie >= 0 && player.movie != run.movie)) {
            report_movie(run, player, frame_before);
            run.movie = player.movie;
            run.movie_start = run.updates;
        }
        // the picture against the clock it follows: within a frame
        if (step.draw && player.movie >= 0 && player.image.frame >= 0 && !player.picture_over) {
            const double shown = player.image.frame / player.decoder.frame_rate;
            const double off = player.clock - shown;
            const double frame = 1.0 / player.decoder.frame_rate;
            const double stray = off < 0 ? -off : (off > frame ? off - frame : 0.0);
            run.worst_sync = stray > run.worst_sync ? stray : run.worst_sync;
        }
        if (step.subtitle != subtitle) {
            if (step.subtitle >= 0) {
                std::printf("    subtitle %2d at update %4d (movie %7.3f s): %s\n", step.subtitle,
                            run.updates, player.clock, subtitles[step.subtitle].text.c_str());
            }
            subtitle = step.subtitle;
        }
        if (step.fade_out >= 0) {
            std::printf("    fade out over %.0f ticks at %.3f s\n", step.fade_out,
                        run.updates / double(updates_per_second));
            run.fade_left = static_cast<int>(step.fade_out);
        } else if (run.fade_left > 0) {
            --run.fade_left;
        }
        if (step.fade_in >= 0) {
            run.fade_left = -1;
        }
        if (step.done) {
            break;
        }
        // the sound engine's next 1/60 s
        pump_audio_streams(audio);
        render_audio(audio, mix.data(), frames_per_update);
    }
    report_movie(run, player, player.image.frame);
    std::printf("    %s after %.3f s (%d updates): result %d, next state 0x%X; picture off its "
                "clock by %.1f ms at worst\n",
                step.done ? "done" : "STILL RUNNING", run.updates / double(updates_per_second),
                run.updates, step.result, step.next_state, run.worst_sync * 1000.0);
    end_movie_screen(screen, player, audio);
}

} // namespace

void run_movie_screens(const std::string& data_dir) {
    AudioOptions options;
    options.device = false;
    options.stream_thread = false;
    AudioPtr audio = init_audio(data_dir, options);
    if (!audio) {
        std::printf("no sound engine\n");
        return;
    }
    load_sound_bank(*audio, bank_sys, "sys");
    MoviePlayer player;
    load_movie_files(player, data_dir);
    std::vector<EndingSubtitle> subtitles;
    if (!load_ending_subtitles(subtitles, data_dir)) {
        std::printf("no ending subtitles\n");
    }
    const MovieScreenKind kinds[] = {
        MovieScreenKind::phantagram_logo, MovieScreenKind::dolby_logo, MovieScreenKind::attract,
        MovieScreenKind::attract,         MovieScreenKind::opening,    MovieScreenKind::ending,
        MovieScreenKind::credits,         MovieScreenKind::secret,
    };
    for (bool press : {false, true}) {
        for (MovieScreenKind kind : kinds) {
            run_screen(kind, press, player, *audio, subtitles);
        }
    }
}

// one movie in real time through an SDL device (SDL_AUDIO_DRIVER=dummy or disk, never speakers),
// its sound read on the engine's own thread, the loop at about 60 Hz: the movie's clock against
// the wall each second and when it's done. Without a device nothing pulls the mix, as on a
// machine with no sound output, and the picture has to go on by itself.
void run_movie_realtime(const std::string& data_dir, int movie, bool device) {
    AudioOptions options;
    options.device = device;
    AudioPtr audio = init_audio(data_dir, options);
    if (!audio) {
        std::printf("no sound engine\n");
        return;
    }
    MoviePlayer player;
    load_movie_files(player, data_dir);
    open_movie_player(player);
    play_movie(player, *audio, movie);
    const Uint64 start = SDL_GetTicksNS();
    Uint64 last = start;
    int second = 0;
    while (!movie_done(player)) {
        SDL_DelayNS(SDL_NS_PER_SECOND / updates_per_second);
        const Uint64 now = SDL_GetTicksNS();
        update_audio(*audio);
        service_movie(player, *audio, (now - last) / double(SDL_NS_PER_SECOND));
        last = now;
        const double wall = (now - start) / double(SDL_NS_PER_SECOND);
        if (wall >= second + 1 || movie_done(player)) {
            ++second;
            std::printf("  wall %7.3f s  movie clock %7.3f s  sound %7.3f s  frame %5d\n", wall,
                        player.clock, movie_audio_seconds(*audio), player.image.frame);
        }
        if (wall > player.files[movie].sound_seconds + 30) {
            std::printf("  not done 30 s after its sound's length\n");
            break;
        }
    }
    close_movie_player(player, *audio);
}
