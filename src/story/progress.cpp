// The counters behind the predicates and gates, and a new game's progress (decomp
// docs/port/progression.md "Predicates", "A new game"; src/od/u_0933f0.c, u_06c3e0.c, u_0b9460.c).
#include "story/story.hpp"

namespace {

// the song every new game owns, as heard (music_state_new: music_owned[77] = 2)
constexpr int first_song = 77;
constexpr int first_song_state = 2;
// new_game_reset: money 20000 (200,000 NY), the beaten bit kept
constexpr int new_game_money = 20000;
// music_owned_count counts the first 0xC0 bytes, not just the 78 songs
constexpr int music_owned_counted = 0xC0;

} // namespace

void story_boot(Story& story, const StoryTables& tables) {
    // events cleared (events_done_clear 0xBA1A1)
    for (std::uint32_t& word : story.events_done) {
        word = 0;
    }

    // pools and tournaments (pool_progress_new): 0 where the table has someone, else -1
    for (int i = 0; i < pool_count; ++i) {
        const PoolRow& row = tables.pools[static_cast<std::size_t>(i)];
        PoolProgress& progress = story.pools[i];
        progress.primary = row.primary != -1 ? 0 : -1;
        for (int k = 0; k < 8; ++k) {
            progress.alternates[k] = row.alternates[k] != -1 ? 0 : -1;
        }
    }
    for (int t = 0; t < tournament_count; ++t) {
        const bool fixed = tables.tournaments[static_cast<std::size_t>(t)].fixed_opponent != -1;
        story.tournament_won[t] = fixed ? 0 : -1;
    }

    // songs (music_state_new; tonight's Sonic Amp stock belongs to the shop)
    for (std::uint8_t& owned : story.music_owned) {
        owned = 0;
    }
    story.music_owned[first_song] = first_song_state;

    // the beaten bit, the post-game cycles, and the records' days
    story.game_beaten = false;
    story.completed_cycles = 0;
    story.days_played = 0;
}

// decomp: new_game_reset 0x2BE46
void story_new_game(Story& story, const StoryTables& tables, int burning_chrome_roll) {
    story.day = 0;
    story.money = new_game_money;
    lineup_refill(story, tables, 0, burning_chrome_roll);
}

// decomp: tournament_won 0x96D9B
bool tournament_won(const Story& story, int tournament) {
    return tournament >= 0 && tournament < tournament_count &&
           story.tournament_won[tournament] == 1;
}

// decomp: class_wins 0x96DC3
int class_wins(const Story& story, const StoryTables& tables, int area) {
    if (area < 0 || area >= 3) {
        return 0;
    }
    int count = 0;
    for (int k = 0; k < 8; ++k) {
        if (tournament_won(story, tables.class_tournaments[area][k])) {
            ++count;
        }
    }
    return count;
}

// decomp: area_ranker_wins 0x96E21
int area_ranker_wins(const Story& story, const StoryTables& tables, int area) {
    if (area < 0 || area >= 3) {
        return 0;
    }
    return tournament_won(story, tables.area_ranker_tournaments[area]) ? 1 : 0;
}

// decomp: area_ranker_beaten 0x96E7E
bool area_ranker_beaten(const Story& story, const StoryTables& tables, int area) {
    return area_ranker_wins(story, tables, area) >= 1;
}

// decomp: area_rankers_beaten 0x96E9A
int area_rankers_beaten(const Story& story, const StoryTables& tables) {
    int count = 0;
    for (int area = 0; area < 3; ++area) {
        if (area_ranker_beaten(story, tables, area)) {
            ++count;
        }
    }
    return count;
}

// decomp: story_progress 0xD139B
int story_progress(const Story& story, const StoryTables& tables) {
    int wins = 0;
    for (int area = 0; area < 3; ++area) {
        wins += class_wins(story, tables, area) + area_ranker_wins(story, tables, area);
    }
    return wins;
}

// decomp: music_owned_count 0x2A0F8
int music_owned_count(const Story& story) {
    int count = 0;
    for (int i = 0; i < music_owned_counted; ++i) {
        if (story.music_owned[i] != 0) {
            ++count;
        }
    }
    return count;
}

// decomp: tournament_kind 0x96F8F
int tournament_kind(const StoryTables& tables, int area, int tournament) {
    if (tournament == tables.first_ranker_tournament) {
        return 2;
    }
    if (area >= 0 && area < 3 && tournament == tables.area_ranker_tournaments[area]) {
        return 1;
    }
    return 0;
}

// decomp: game_tournament_slot 0x6CE11
int game_tournament_slot(const Story& story, const StoryTables& tables, int day, int slot) {
    if (slot < 0 || slot >= 4) {
        return 0;
    }
    const int tournament = day_row(tables, day).tournament[slot];
    // the First Ranker never comes round again once beaten: Burning Chrome takes its place
    if (tournament == tables.first_ranker_tournament && story.game_beaten) {
        return tables.burning_chrome_tournaments[0];
    }
    return tournament;
}

// decomp: day_slot_of_selector 0x6CE73
int day_slot_of_selector(const Story& story) {
    return story.rumble_selected / 2;
}

// decomp: game_day_tournament 0x6CE9C
int game_day_tournament(const Story& story, const StoryTables& tables) {
    // free play and the test drive (selectors 6, 7) are slot 3, whose schedule word is the chip
    // shop's row: retail reads it as a tournament id, so on day 58 (14, an Area Ranker bout)
    // their doors stop ending them (docs/bugs.md). They have no tournament today.
    const int slot = day_slot_of_selector(story);
    if (slot < 0 || slot > 2) {
        return -1;
    }
    return game_tournament_slot(story, tables, story.day, slot);
}

int tournament_rule(const StoryTables& tables, int tournament) {
    if (tournament < 0 || tournament >= tournament_count) {
        return -1;
    }
    return tables.tournaments[static_cast<std::size_t>(tournament)].rule;
}

// decomp: game_beaten_set 0x6DEF1
void game_beaten_set(Story& story) {
    story.game_beaten = true;
}
