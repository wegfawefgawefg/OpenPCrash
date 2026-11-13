// Days, today's line-up, the area select's gates and what entering and winning write (decomp
// docs/port/progression.md "Tournaments", "Days"; u_06c3e0.c, u_02e2fc.c, u_012ff5.c, u_0451f0.c).
#include "story/story.hpp"

namespace {

// the Aqualine ranker (tournaments 48) leaves the schedule for good once won
// (RumbleContext::fill_opponent_queues tests the literal 0x30)
constexpr int aqualine_ranker = 48;
// days_played stops at 9999 (day_advance_end)
constexpr int days_played_max = 9999;
// the area select has three area slots; slot 3 is the chip shop's word
constexpr int area_slots = 3;

// the tournament fill_opponent_queues settles on for a slot's scheduled one, -1 for none
int lineup_tournament(const Story& story, const StoryTables& tables, int scheduled, int roll) {
    if (scheduled < 0) {
        return -1;
    }
    int tournament = scheduled;
    if (tournament == tables.burning_chrome_tournaments[0] && story.game_beaten) {
        // rand_float_crt(3.0f), out-of-range rolls taken as 0
        tournament = tables.burning_chrome_tournaments[roll >= 0 && roll < 3 ? roll : 0];
    }
    if (tournament == aqualine_ranker && story.tournament_won[aqualine_ranker] == 1) {
        return -1;
    }
    return tournament;
}

} // namespace

// decomp: shop_daily_record 0x6CDDD
const DayRow& day_row(const StoryTables& tables, int day) {
    const unsigned index = static_cast<unsigned>(day) % static_cast<unsigned>(day_count);
    return tables.days[index];
}

bool lineup_wants_roll(const Story& story, const StoryTables& tables, int day) {
    if (!story.game_beaten) {
        return false;
    }
    for (int slot = 0; slot < area_slots; ++slot) {
        if (game_tournament_slot(story, tables, day, slot) ==
            tables.burning_chrome_tournaments[0]) {
            return true;
        }
    }
    return false;
}

void lineup_refill(Story& story, const StoryTables& tables, int day, int burning_chrome_roll) {
    // tournaments_refill walks the three area slots; rumble[3] (free play) isn't touched
    for (int slot = 0; slot < area_slots; ++slot) {
        const int scheduled = game_tournament_slot(story, tables, day, slot);
        story.lineup[slot] = lineup_tournament(story, tables, scheduled, burning_chrome_roll);
    }
}

// decomp: day_advance_end 0x2E32D
void day_advance(Story& story, const StoryTables& tables, int burning_chrome_roll) {
    // day + 1, wrapping 60 to 0; a wrap after the ending is one more post-game cycle
    int day = story.day + 1;
    if (day >= day_count) {
        day = 0;
        if (story.game_beaten && story.completed_cycles < 0x7FFFFFFF) {
            story.completed_cycles += 1;
        }
    }
    story.days_played =
        story.days_played + 1 > days_played_max ? days_played_max : story.days_played + 1;
    story.day = day;
    lineup_refill(story, tables, day, burning_chrome_roll);
    story.map_spin_money = 0;
}

void area_pick(Story& story, const StoryTables& tables, int area) {
    // stage_selector_join(area, the tournament's night): a slot without one counts as day
    const int tournament = area >= 0 && area < 4 ? story.lineup[area] : -1;
    const int night =
        tournament >= 0 ? tables.tournaments[static_cast<std::size_t>(tournament)].night : 0;
    story.rumble_selected = area * 2 + night;
}

// decomp: area_entry_status 0x1435E
EntryStatus area_entry_status(const Story& story, const StoryTables& tables, int area) {
    if (area == 3) {
        return EntryStatus::Shop;
    }
    const int tournament = area >= 0 && area < 3 ? story.lineup[area] : -1;
    if (tournament < 0) {
        return EntryStatus::NoTournament;
    }
    // the ranker locks come before the fee (an earlier finding had it the other way)
    const int kind = tournament_kind(tables, area, tournament);
    if (kind == 1 && class_wins(story, tables, area) < 8) {
        return EntryStatus::RankerLocked;
    }
    if (kind == 2 && area_rankers_beaten(story, tables) < 3) {
        return EntryStatus::FirstRankerLocked;
    }
    // fee_ok: money (31 bits, unsigned) covers the rule's fee
    const int rule = tables.tournaments[static_cast<std::size_t>(tournament)].rule;
    const unsigned fee =
        static_cast<unsigned>(tables.rules[static_cast<std::size_t>(rule)].entry_fee);
    return static_cast<unsigned>(story.money) >= fee ? EntryStatus::Allowed : EntryStatus::NoFee;
}

void tournament_enter(Story& story, const StoryTables& tables, int area) {
    // the fee (money_add of minus the rule's fee; a slot without a tournament charges rule 0's)
    const int tournament = area >= 0 && area < 3 ? story.lineup[area] : -1;
    const int rule = tournament >= 0 ? tables.tournaments[static_cast<std::size_t>(tournament)].rule
                                     : tables.tournaments[0].rule;
    money_add(story,
              -static_cast<std::int64_t>(tables.rules[static_cast<std::size_t>(rule)].entry_fee));

    // what the first-win talks compare with after the battle
    story.class_wins_at_entry = class_wins(story, tables, area);
    story.ranker_wins_at_entry = area_ranker_wins(story, tables, area);
}

// decomp: money_add 0xB1E3B
void money_add(Story& story, std::int64_t amount) {
    // money_add 0xB1EF1: the amount and the result both held to 99,999,999
    constexpr std::int64_t money_max = 99999999;
    amount = amount < -money_max ? -money_max : amount > money_max ? money_max : amount;
    std::int64_t money = static_cast<std::int64_t>(story.money) + amount;
    money = money < 0 ? 0 : money > money_max ? money_max : money;
    story.money = static_cast<int>(money);
}

// decomp: actor_settle_damage 0x766DE
void opponent_beaten(Story& story, const OpponentTicket& ticket) {
    switch (ticket.role) {
    case 0:
        if (ticket.pool >= 0 && ticket.pool < pool_count && ticket.alternate >= 0 &&
            ticket.alternate < 8) {
            story.pools[ticket.pool].alternates[ticket.alternate] = 1;
        }
        break;
    case 1:
        if (ticket.pool >= 0 && ticket.pool < pool_count) {
            story.pools[ticket.pool].primary = 1;
        }
        break;
    case 2:
        if (ticket.tournament >= 0 && ticket.tournament < tournament_count) {
            story.tournament_won[ticket.tournament] = 1;
        }
        break;
    default:
        break;
    }
}
