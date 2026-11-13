// The movie screens' tasks (docs/movies.md, "Screens"), step for step as the decomp has them:
// phantagram_logo_* (src/od/u_02ec60.c), dolby_logo_* (u_01bf00.c), attract_movie_screen_*
// (src/o1/u_012ff5.c), opening_movie_* (src/o1/u_02e2fc.c), ending_movie_task* (u_01c2f0.c),
// credits_task* (u_03c1f0.c), secret_movies_task* (u_01c5b0.c). `frame_cadence` is 60 in every
// movie state, so its multiples are written out as updates.
#include "movie/movie_screens.hpp"

#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "world/app_states.hpp"

#include <fstream>
#include <gsexp/sexp.hpp>
#include <sstream>

namespace {

constexpr int cadence = 60;       // frame_cadence of the movie states
constexpr int step_finished = -1; // the task asked for its state change

// the next state each state's update picks for a result (decomp src/od/u_0a1e10.c)
int next_state(MovieScreenKind kind, int result) {
    switch (kind) {
    case MovieScreenKind::phantagram_logo:
        return app_logo;
    case MovieScreenKind::dolby_logo:
        return app_attract_movie;
    case MovieScreenKind::attract:
        return app_title_return;
    case MovieScreenKind::opening:
        // state_opening_movie_update: name_entry_target_pilot, then 0x16 (event trigger row 0
        // turns it into Salsa's welcome first); result 1 would go to 0x28 but nothing sets it
        return result == 0 ? app_name_entry : app_title_return;
    case MovieScreenKind::ending:
        return app_credits;
    case MovieScreenKind::credits:
        return result == 0 ? app_day_advance : app_secret_movies;
    case MovieScreenKind::secret:
        return app_day_advance;
    }
    return -1;
}

void leave(MovieScreen& screen, MovieScreenStep& out, int result) {
    screen.step = step_finished;
    screen.result = result;
    out.done = true;
    out.result = result;
    out.next_state = next_state(screen.kind, result);
}

// movie_player_service then movie_draw_fullscreen
void show(MovieScreenStep& out, MoviePlayer& player, const Audio& audio, double seconds) {
    service_movie(player, audio, seconds);
    out.draw = true;
}

// phantagram_logo_* and dolby_logo_*: a second unskippable, then accept or the end; the
// Phantagram logo cuts to black on leaving, the Dolby one doesn't touch the fade
void step_logo(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
               const MovieScreenInput& input, double seconds, int movie, bool cut) {
    if (screen.step == 0) {
        if (!open_movie_player(player)) {
            leave(screen, out, 0);
            return;
        }
        play_movie(player, audio, movie);
        screen.opened = true;
        screen.frames = cadence;
        out.fade_reset = true;
        screen.step = 1;
        return;
    }
    show(out, player, audio, seconds);
    if (screen.frames >= 0) {
        --screen.frames;
        return;
    }
    if (input.accept || movie_done(player)) {
        out.fade_out = cut ? 0.0f : -1.0f;
        leave(screen, out, 0);
    }
}

// attract_movie_screen_*: phantomcrash and opening by turns, a second unskippable, then any
// button or the end, 16 updates more with nothing drawn, and a cut to black
void step_attract(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
                  const MovieScreenInput& input, double seconds) {
    switch (screen.step) {
    case 0:
        if (!open_movie_player(player)) {
            leave(screen, out, 0);
            return;
        }
        play_movie(player, audio, player.attract_next % 2);
        player.attract_next = (player.attract_next + 1) % 2;
        screen.opened = true;
        screen.frames = cadence;
        out.fade_reset = true;
        screen.step = 1;
        return;
    case 1:
        show(out, player, audio, seconds);
        if (--screen.frames < 0) {
            screen.step = 2;
        }
        return;
    case 2:
        show(out, player, audio, seconds);
        if (movie_done(player) || input.any) {
            screen.frames = 16;
            screen.step = 3;
        }
        return;
    default:
        if (--screen.frames < 0) {
            out.fade_out = 0.0f;
            leave(screen, out, 0);
        }
        return;
    }
}

// opening_movie_*: opening.sfd, then opening02.sfd, each a second unskippable and then skipped
// by accept (opening_movie_skip with the spawn argument 0); opening02 starts a fade out 35 s and
// 27 updates in
void step_opening(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
                  const MovieScreenInput& input, double seconds) {
    switch (screen.step) {
    case 0: // opening_movie_start
        screen.frames = cadence;
        open_movie_player(player);
        screen.opened = true;
        play_movie(player, audio, movie_opening);
        out.fade_reset = true;
        screen.step = 1;
        return;
    case 1: // opening_movie_first
        --screen.frames;
        show(out, player, audio, seconds);
        if (screen.frames < 0) {
            screen.frames = cadence * 117; // counted down, never read
            screen.step = 2;
        }
        return;
    case 2: // opening_movie_first_wait
        show(out, player, audio, seconds);
        if (movie_done(player) || input.accept) {
            screen.step = 3;
        }
        screen.frames -= screen.frames > 0 ? 1 : 0;
        return;
    case 3: // opening_movie_second_start: nothing drawn this update
        play_movie(player, audio, movie_opening02);
        screen.count = 0;
        screen.frames = cadence;
        screen.step = 4;
        return;
    case 4: // opening_movie_second
        show(out, player, audio, seconds);
        ++screen.count;
        if (--screen.frames < 0) {
            screen.step = 5;
        }
        return;
    default: // opening_movie_second_wait
        show(out, player, audio, seconds);
        ++screen.count;
        if (screen.count == cadence * 35 + 27) {
            out.fade_out = static_cast<float>(cadence * 2 - 23);
            out.fade_out_mode = -1;
        }
        if (movie_done(player) || input.accept) {
            leave(screen, out, 0);
            return;
        }
        screen.frames -= screen.frames > 0 ? 1 : 0;
        return;
    }
}

// ending_movie_subtitles_draw: the row whose span holds the update count / 2 (frames at 30 a
// second); a row is passed for good once its end is reached
void ending_subtitle(MovieScreen& screen, MovieScreenStep& out,
                     const std::vector<EndingSubtitle>& rows) {
    out.draw = true;
    if (screen.subtitle < static_cast<int>(rows.size())) {
        const EndingSubtitle& row = rows[screen.subtitle];
        const int frame = screen.count / 2;
        if (row.start <= frame && row.end > frame) {
            out.subtitle = screen.subtitle;
        } else if (row.end <= frame) {
            ++screen.subtitle;
        }
    }
    ++screen.count;
}

// ending_movie_task*: no skip at all
void step_ending(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
                 const std::vector<EndingSubtitle>& rows, double seconds) {
    if (screen.step == 0) {
        if (!open_movie_player(player)) {
            leave(screen, out, 0);
            return;
        }
        screen.opened = true;
        out.fade_reset = true;
        screen.frames = cadence;
        screen.count = 0;
        screen.subtitle = 0;
        play_movie(player, audio, movie_ending);
        screen.step = 1;
        return;
    }
    service_movie(player, audio, seconds);
    ending_subtitle(screen, out, rows);
    if (screen.step == 1) {
        screen.step = --screen.frames < 0 ? 2 : 1;
    } else if (movie_done(player)) {
        leave(screen, out, 0);
    }
}

// credits_task*: staff and epilogue unskippable, a cut to black, then the_end on a loop for 30 s
// with a half-second fade in each time round; accept after 2 s (with sys cue 5) goes to the
// night, waiting it out to the secret movies
void step_credits(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
                  const MovieScreenInput& input, double seconds) {
    switch (screen.step) {
    case 0: // credits_task
        open_movie_player(player);
        screen.opened = true;
        play_movie(player, audio, movie_staff);
        screen.frames = cadence;
        out.fade_reset = true;
        screen.step = 1;
        return;
    case 1:
        show(out, player, audio, seconds);
        if (--screen.frames < 0) {
            screen.step = 2;
        }
        return;
    case 2: // the original services staff.sfd only on even task timer ticks; the port every
            // update (docs/movies.md)
        if (movie_done(player)) {
            play_movie(player, audio, movie_epilogue);
            screen.step = 3;
        }
        show(out, player, audio, seconds);
        return;
    case 3:
        show(out, player, audio, seconds);
        if (movie_done(player)) {
            out.fade_out = 1.0f;
            screen.step = 4;
        }
        return;
    case 4: // nothing drawn while the fade finishes
        if (input.fade_black) {
            screen.frames = cadence * 30;
            screen.step = 5;
        }
        return;
    case 5: // credits_the_end_loop_next
        play_movie(player, audio, movie_the_end);
        show(out, player, audio, seconds);
        out.fade_in = cadence * 0.5f;
        screen.step = 6;
        return;
    default: // credits_the_end_loop
        show(out, player, audio, seconds);
        if (movie_done(player)) {
            screen.step = 5;
        }
        if (screen.frames == cadence / 2) {
            out.fade_out = cadence * 0.5f;
        }
        if (--screen.frames < 0) {
            out.fade_out = 0.0f;
            leave(screen, out, 1);
            return;
        }
        if (screen.frames < cadence * 28 && input.accept) {
            play_sound(audio, bank_sys, se_menu_accept); // input_accept_pressed_sound(mask, 0, 5)
            out.fade_out = 0.0f;
            leave(screen, out, 0);
        }
        return;
    }
}

// secret_movies_task*: kudryavka with no skip, then cloud, skipped by accept after 2 s
void step_secret(MovieScreen& screen, MovieScreenStep& out, MoviePlayer& player, Audio& audio,
                 const MovieScreenInput& input, double seconds) {
    switch (screen.step) {
    case 0:
        open_movie_player(player);
        screen.opened = true;
        play_movie(player, audio, movie_kudryavka);
        screen.frames = cadence;
        out.fade_reset = true;
        screen.step = 1;
        return;
    case 1:
        show(out, player, audio, seconds);
        if (--screen.frames < 0) {
            screen.step = 2;
        }
        return;
    case 2:
        show(out, player, audio, seconds);
        if (movie_done(player)) {
            play_movie(player, audio, movie_cloud);
            screen.frames = cadence * 2;
            screen.step = 3;
        }
        return;
    default:
        show(out, player, audio, seconds);
        screen.frames -= screen.frames > 0 ? 1 : 0;
        if (movie_done(player) || (screen.frames == 0 && input.accept)) {
            out.fade_out = 0.0f;
            leave(screen, out, 0);
        }
        return;
    }
}

} // namespace

bool load_ending_subtitles(std::vector<EndingSubtitle>& rows, const std::string& data_dir) {
    const std::string path = data_dir + "/tables/ending_subtitles.sexp";
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    if (in) {
        text << in.rdbuf();
    }
    const gsexp::ParseResult parsed = gsexp::parse_owned(text.str());
    if (!in || !parsed.ok) {
        return false;
    }
    rows.clear();
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("subtitle")) {
            continue;
        }
        EndingSubtitle row;
        row.start = form.get_int("start").value_or(-1);
        row.end = form.get_int("end").value_or(-1);
        row.text = form.get_string("text").value_or("");
        // the table ends at the first row with a negative start or end
        if (row.start < 0 || row.end < 0) {
            break;
        }
        rows.push_back(row);
    }
    return true;
}

void start_movie_screen(MovieScreen& screen, MovieScreenKind kind) {
    screen = MovieScreen{};
    screen.kind = kind;
}

MovieScreenStep step_movie_screen(MovieScreen& screen, MoviePlayer& player, Audio& audio,
                                  const MovieScreenInput& input,
                                  const std::vector<EndingSubtitle>& subtitles, double seconds) {
    MovieScreenStep out;
    if (screen.step == step_finished) {
        out.done = true;
        out.result = screen.result;
        out.next_state = next_state(screen.kind, screen.result);
        return out;
    }
    switch (screen.kind) {
    case MovieScreenKind::phantagram_logo:
        step_logo(screen, out, player, audio, input, seconds, movie_phantagram, true);
        break;
    case MovieScreenKind::dolby_logo:
        step_logo(screen, out, player, audio, input, seconds, movie_dolby, false);
        break;
    case MovieScreenKind::attract:
        step_attract(screen, out, player, audio, input, seconds);
        break;
    case MovieScreenKind::opening:
        step_opening(screen, out, player, audio, input, seconds);
        break;
    case MovieScreenKind::ending:
        step_ending(screen, out, player, audio, subtitles, seconds);
        break;
    case MovieScreenKind::credits:
        step_credits(screen, out, player, audio, input, seconds);
        break;
    case MovieScreenKind::secret:
        step_secret(screen, out, player, audio, input, seconds);
        break;
    }
    return out;
}

void end_movie_screen(MovieScreen& screen, MoviePlayer& player, Audio& audio) {
    if (screen.opened) {
        close_movie_player(player, audio);
        screen.opened = false;
    }
}
