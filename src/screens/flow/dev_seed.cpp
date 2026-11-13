// Port-only story jumps for playtests and headless runs (docs/playthrough.md), run with the
// garages' and money's (dev_world_seed) at boot and after the title's New Game:
//   PHANTOMCRASH_DEV_WON=ids         those tournaments won (comma separated); "story" is the 24 class
//                                    tournaments and the three Area Rankers, "classes" the 24 alone
//   PHANTOMCRASH_DEV_BEATEN=1        the game beaten (the post-game: Burning Chrome, the cycles)
//   PHANTOMCRASH_DEV_DAY=N           day N (0..59) with its line-up drawn, as a night would leave it
//   PHANTOMCRASH_DEV_MONEY=N         again here, since --state starts a new game's day 0 after boot
// A won tournament only sets tournament_won: the story's counters read nothing else, and its pools
// stay as they were (its fixed opponent comes back once they are done, as in play).
#include "screens/flow/front_end.hpp"

#include "story/rumble.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// the class tournaments of Shinjuku, Shibuya and Aqualine, and the three Area Rankers
// (docs/port/progression.md, "Tournaments")
constexpr int class_tournaments[24] = {3,  4,  5,  6,  7,  8,  9,  10, 20, 21, 22, 23,
                                       24, 25, 26, 27, 37, 38, 39, 40, 41, 42, 43, 44};
constexpr int area_rankers[3] = {14, 31, 48};

void won(Story& story, int t) {
    if (t >= 0 && t < tournament_count) {
        story.tournament_won[t] = 1;
    }
}

} // namespace

void dev_story_seed(FrontEnd& fe) {
    World& w = *fe.world;
    bool changed = false;
    if (const char* list = std::getenv("PHANTOMCRASH_DEV_WON")) {
        const std::string text = list;
        if (text == "story" || text == "classes") {
            for (const int t : class_tournaments) {
                won(w.story, t);
            }
            if (text == "story") {
                for (const int t : area_rankers) {
                    won(w.story, t);
                }
            }
        } else {
            for (const char* at = list; *at;) {
                char* end = nullptr;
                const long t = std::strtol(at, &end, 10);
                if (end == at) {
                    break;
                }
                won(w.story, static_cast<int>(t));
                at = *end == ',' ? end + 1 : end;
            }
        }
        changed = true;
    }
    if (const char* beaten = std::getenv("PHANTOMCRASH_DEV_BEATEN")) {
        if (std::atoi(beaten) != 0) {
            game_beaten_set(w.story);
            changed = true;
        }
    }
    if (const char* money = std::getenv("PHANTOMCRASH_DEV_MONEY")) {
        w.story.money = std::atoi(money);
    }
    int day = w.story.day;
    if (const char* text = std::getenv("PHANTOMCRASH_DEV_DAY")) {
        day = std::atoi(text) % 60;
        day = day < 0 ? day + 60 : day;
        changed = true;
    }
    if (changed) {
        // the line-up for the day, as day_advance_end's tournaments_refill draws it
        w.story.day = day;
        tournaments_refill(w, fe.story_tables, fe.campaign.roster, day);
        std::printf("dev: day %d, line-up %d %d %d%s\n", day, w.story.lineup[0],
                    w.story.lineup[1], w.story.lineup[2], w.story.game_beaten ? ", beaten" : "");
    }
}
