// PC CONFIG's CHEATS page (docs/settings.md, "CHEATS"): opened from GAME's CHEATS row between
// fights. Money, songs, paints and stickers, garages and a day for testing and playing with builds,
// and the price rules (TUNE PRICES, SHOP PRICES, MONEY WON). They act on the world's story and
// garages (story/cheat_rules.hpp), so the save keeps them and any use marks the game as cheated.
#include "screens/flow/front_end.hpp"
#include "screens/garage/part_tables.hpp"
#include "screens/pc/pc_settings_screen.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <cstdio>

namespace {

enum CheatRow {
    cheat_infinite_money,
    cheat_add_money,
    cheat_music,
    cheat_paints,
    cheat_garages,
    cheat_skip_day,
    cheat_tune_prices,
    cheat_shop_prices,
    cheat_money_won,
    cheat_marked,
    cheat_row_count
};

struct CheatText {
    const char* label;
    const char* help;
};

// labels in the key config's letters
constexpr CheatText cheat_text[cheat_row_count] = {
    {"INFINITE MONEY", "ON: the money stays at 999,999,990 NY whatever you buy."},
    {"ADD MONEY", "Press for 1,000,000 NY more, as often as you like."},
    {"UNLOCK ALL MUSIC", "Own every song, for the playlists and the fights."},
    {"PAINTS AND STICKERS", "Own every paint and sticker the shops sell."},
    {"ALL GARAGES", "Open all ten garages without paying for them."},
    {"SKIP DAY", "Go on to the next day, as a night in the garage does."},
    {"TUNE PRICES", "Plus Tech's tuning prices divided. RETAIL is the original's; FREE costs "
                    "nothing."},
    {"SHOP PRICES",
     "The shops' prices divided, buying and selling alike. RETAIL is the original's."},
    {"MONEY WON", "A fight's prize multiplied. x1 is the original's."},
    {"THIS GAME", "CHEATED once a cheat or a rule other than the original's is used. The save "
                  "and bug reports say so."},
};

// 1,000,000 NY in units of 10 NY
constexpr int money_step = 100000;

// "1,000,000" (the label font's comma is the number font's)
std::string grouped(long long value) {
    std::string digits = std::to_string(value);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

std::string divisor_text(int divisor) {
    return divisor == 1 ? "RETAIL" : divisor <= 0 ? "FREE" : "1/" + std::to_string(divisor);
}

// the next choice in a list, from the current one; false at either end
template <typename T, std::size_t N> bool step_choice(const int (&choices)[N], T& value, int step) {
    int at = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (choices[i] == value) {
            at = static_cast<int>(i);
        }
    }
    const int next = at + step;
    if (next < 0 || next >= static_cast<int>(N)) {
        return false;
    }
    value = static_cast<T>(choices[next]);
    return true;
}

int songs_owned(const Story& story) {
    int count = 0;
    for (int song = 0; song < all_songs; ++song) {
        count += story.music_owned[song] != 0 ? 1 : 0;
    }
    return count;
}

int garages_open(const Garages& g) {
    int count = 0;
    for (int i = 0; i < garage_count; ++i) {
        count += (g.unlocked >> i) & 1u;
    }
    return count;
}

// a paint or sticker the part tables have and the player doesn't own
bool paints_missing(const PartTables& t, const Garages& g) {
    for (int c = 0; c < color_count; ++c) {
        if (paint_part(t, 6, c) && !(g.colors_owned[c] & 1)) {
            return true;
        }
    }
    for (int c = 0; c < sticker_count; ++c) {
        if (paint_part(t, 7, c) && !(g.stickers_owned[c] & 1)) {
            return true;
        }
    }
    return false;
}

void paints_unlock(const PartTables& t, Garages& g) {
    for (int c = 0; c < color_count; ++c) {
        if (paint_part(t, 6, c)) {
            g.colors_owned[c] |= 1;
        }
    }
    for (int c = 0; c < sticker_count; ++c) {
        if (paint_part(t, 7, c)) {
            g.stickers_owned[c] |= 1;
        }
    }
}

} // namespace

int pc_cheats_count() {
    return cheat_row_count;
}

PcRowView pc_cheats_view(FrontEnd& fe, int row) {
    PcRowView view;
    if (row < 0 || row >= cheat_row_count || !fe.world) {
        return view;
    }
    const World& w = *fe.world;
    const CheatRules& r = w.story.cheats;
    view.label = cheat_text[row].label;
    view.help = cheat_text[row].help;
    view.arrows = false;
    switch (row) {
    case cheat_infinite_money:
        view.value = r.infinite_money ? "ON" : "OFF";
        view.arrows = true;
        break;
    case cheat_add_money:
        view.value = "+" + grouped(money_step * 10LL) + " NY";
        view.dim = r.infinite_money || w.story.money >= money_cap;
        break;
    case cheat_music:
        view.value = std::to_string(songs_owned(w.story)) + "/" + std::to_string(all_songs);
        break;
    case cheat_paints:
        view.value = paints_missing(fe.shop.tables, w.garages) ? "UNLOCK" : "ALL OWNED";
        break;
    case cheat_garages:
        view.value = std::to_string(garages_open(w.garages)) + "/" + std::to_string(garage_count);
        break;
    case cheat_skip_day:
        view.value = "TO DAY " + std::to_string((w.story.day + 1) % day_count + 1);
        break;
    case cheat_tune_prices:
        view.value = divisor_text(r.tune_divisor);
        view.arrows = true;
        break;
    case cheat_shop_prices:
        view.value = divisor_text(r.shop_divisor);
        view.arrows = true;
        break;
    case cheat_money_won:
        view.value = "x" + std::to_string(r.money_won);
        view.arrows = true;
        break;
    case cheat_marked:
        view.value = r.cheated ? "CHEATED" : "RETAIL";
        view.dim = true;
        break;
    default:
        break;
    }
    return view;
}

bool pc_cheats_step(FrontEnd& fe, int row, int step) {
    if (!fe.world || step == 0) {
        return false;
    }
    Story& story = fe.world->story;
    CheatRules& r = story.cheats;
    bool changed = false;
    switch (row) {
    case cheat_infinite_money:
        r.infinite_money = !r.infinite_money;
        changed = true;
        break;
    case cheat_tune_prices:
        changed = step_choice(tune_divisors, r.tune_divisor, step);
        break;
    case cheat_shop_prices:
        changed = step_choice(shop_divisors, r.shop_divisor, step);
        break;
    case cheat_money_won:
        changed = step_choice(money_won_factors, r.money_won, step);
        break;
    default:
        return false;
    }
    if (changed) {
        r.cheated = r.cheated || !cheat_rules_retail(r);
        cheat_money_hold(story);
        std::printf("cheats: %s\n", pc_cheats_text(r).c_str());
    }
    return changed;
}

bool pc_cheats_accept(FrontEnd& fe, int row) {
    if (!fe.world) {
        return false;
    }
    World& w = *fe.world;
    Story& story = w.story;
    switch (row) {
    case cheat_add_money:
        if (story.cheats.infinite_money || story.money >= money_cap) {
            return false;
        }
        money_add(story, money_step);
        break;
    case cheat_music:
        if (songs_owned(story) == all_songs) {
            return false;
        }
        for (int song = 0; song < all_songs; ++song) {
            if (story.music_owned[song] == 0) {
                story.music_owned[song] = 1; // bought, not heard yet
            }
        }
        break;
    case cheat_paints:
        if (!paints_missing(fe.shop.tables, w.garages)) {
            return false;
        }
        paints_unlock(fe.shop.tables, w.garages);
        break;
    case cheat_garages:
        if (garages_open(w.garages) == garage_count) {
            return false;
        }
        w.garages.unlocked = (1u << garage_count) - 1;
        break;
    case cheat_skip_day:
        // the night's work (night_step): tonight's Sonic Amp stock, day + 1, the line-up refilled
        // with drawn opponents, the spin money zeroed
        music_shop_generate_stock(fe.music, w.profile.music);
        day_advance(story, fe.story_tables, 0);
        tournaments_refill(w, fe.story_tables, fe.campaign.roster, story.day);
        break;
    default:
        return false;
    }
    story.cheats.cheated = true;
    std::printf("cheats: %s used; money %d, day %d\n", cheat_text[row].label, story.money,
                story.day);
    return true;
}

bool pc_cheats_allowed(const FrontEnd& fe) {
    return fe.world && !fe.screen.pc.paused && fe.options_entry_state != app_title;
}

std::string pc_cheats_text(const CheatRules& r) {
    if (!r.cheated && cheat_rules_retail(r)) {
        return "retail";
    }
    return std::string(r.cheated ? "cheated" : "retail") +
           (r.infinite_money ? ", infinite money" : "") + ", tune " + divisor_text(r.tune_divisor) +
           ", shop " + divisor_text(r.shop_divisor) + ", money won x" + std::to_string(r.money_won);
}
