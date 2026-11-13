// Today's opponents: the queues, the draw and the hand back (decomp src/hand/u_0451f0.c); the
// rosters drawn from them, tournaments_refill and rumble_contexts_reset are rumble_roster.cpp's.
#include "story/rumble.hpp"

#include "story/roster_tables.hpp"
#include "world/world.hpp"

#include <cstdlib>
#include <cstring>

namespace {

// OpponentQueue::append's guard: rows of rumble_opponent_entries
constexpr int opponent_rows = 0x4a3;
// opponent_level_bonus caps at 99
constexpr int level_cap = 99;
// the Aqualine ranker's tournament (the literal 0x30 in fill_opponent_queues)
constexpr int tournament_aqualine_ranker = 0x30;

std::int8_t* ticket_byte(Story& story, const OpponentTicket& t) {
    switch (t.role) {
    case 0:
        if (t.pool >= 0 && t.pool < pool_count && t.alternate >= 0 && t.alternate < 8) {
            return &story.pools[t.pool].alternates[t.alternate];
        }
        return nullptr;
    case 1:
        return t.pool >= 0 && t.pool < pool_count ? &story.pools[t.pool].primary : nullptr;
    case 2:
        return t.tournament >= 0 && t.tournament < tournament_count
                   ? &story.tournament_won[t.tournament]
                   : nullptr;
    default:
        return nullptr;
    }
}

RumbleTicket make_ticket(int opponent, int role, int pool, int alternate, int tournament) {
    RumbleTicket t;
    t.opponent = opponent;
    t.progress = {role, pool, alternate, tournament};
    return t;
}

const TournamentRow& tournament_row(const StoryTables& tables, int t) {
    return tables.tournaments[static_cast<std::size_t>(t >= 0 && t < tournament_count ? t : 0)];
}

// every alternate beaten or empty
// decomp: PoolProgress::alternates_done 0x96D60
bool alternates_done(const PoolProgress& p) {
    for (const std::int8_t a : p.alternates) {
        if (a == 0) {
            return false;
        }
    }
    return true;
}

// queue_opponent 0x45907: by the progress byte, 0 to queue 0 and 1 to queue 1
bool queue_opponent(RumbleSlot& r, Story& story, const RumbleTicket& t) {
    if (t.opponent == -1) {
        return false;
    }
    const int flag = ticket_flag(story, t.progress);
    if (flag == 1) {
        queue_append(r.queues[1], t);
    } else if (flag == 0) {
        queue_append(r.queues[0], t);
    } else {
        return false;
    }
    return true;
}

// the post-game cycles added, capped at 99 (as a short)
// decomp: opponent_level_bonus 0x46664
int level_bonus(const Story& story, int x) {
    x = static_cast<std::int16_t>(x + static_cast<std::int16_t>(story.completed_cycles));
    return x > level_cap ? level_cap : x;
}

// a ticket handed back to a queue's ready end (release_opponent's three arms)
void hand_back(OpponentQueue& q, const RumbleTicket& t) {
    if (q.ready >= 0 && q.ready < queue_capacity) {
        q.tickets[q.ready] = t;
    }
    q.ready += 1;
    if (q.count < q.ready) {
        q.ready = q.count;
    }
}

int& slot_tournament(World& world, int slot) {
    return world.story.lineup[slot];
}

} // namespace

// decomp: rumble_context_by_selector 0x465F4
int rumble_slot_of_selector(int selector) {
    return selector >= 0 && selector < 8 ? selector / 2 : 0;
}

// decomp: opponent_ticket_flag 0x5FBB7
int ticket_flag(const Story& story, const OpponentTicket& t) {
    std::int8_t* byte = ticket_byte(const_cast<Story&>(story), t);
    return byte ? *byte : -1;
}

bool ticket_live(const RumbleTicket& t) {
    // flag_at is the byte's GameState offset: pools[0].primary is offset 0, and a cleared or
    // zeroed ticket has 0 (here an alternate of -1 or a role of -1)
    const OpponentTicket& p = t.progress;
    switch (p.role) {
    case 0:
        return p.pool >= 0 && p.alternate >= 0;
    case 1:
        return p.pool > 0;
    case 2:
        return p.tournament >= 0;
    default:
        return false;
    }
}

// decomp: OpponentQueue::clear 0x461C0
void queue_clear(OpponentQueue& q) {
    for (RumbleTicket& t : q.tickets) {
        t = RumbleTicket{};
    }
    q.out = q.ready = q.count = q.drawable = 0;
}

// decomp: OpponentQueue::append 0x4624D
void queue_append(OpponentQueue& q, const RumbleTicket& t) {
    if (t.opponent < 0 || t.opponent >= opponent_rows || q.count >= queue_capacity) {
        return;
    }
    q.tickets[q.count] = t;
    q.count += 1;
    q.drawable += 1;
    q.ready += 1;
    if (q.count < q.ready) {
        q.ready = q.count;
    }
}

// decomp: OpponentQueue::insert_at_ready 0x462FB
void queue_insert_at_ready(OpponentQueue& q, const RumbleTicket& t) {
    if (t.opponent < 0 || t.opponent >= opponent_rows) {
        return;
    }
    // retail writes over whatever sits at `ready` (a drawn ticket's stale copy), no shift
    if (q.ready >= 0 && q.ready < queue_capacity) {
        q.tickets[q.ready] = t;
    }
    q.count += 1;
    q.ready += 1;
    if (q.count < q.ready) {
        q.ready = q.count;
    }
}

// decomp: opponent_fits_tournament 0x46391
bool opponent_fits_tournament(const StoryTables& tables, const RosterTables& roster, int entry,
                              int tournament) {
    if (entry == -1) {
        return false;
    }
    const OpponentEntry& e = opponent_entry(roster, entry);
    const RosterBuild& build = roster_build_row(roster, e.loadout);
    const TournamentRow& t = tournament_row(tables, tournament);
    const RuleRow& rule = tables.rules[static_cast<std::size_t>(t.rule)];
    if (t.level_min > e.level || t.level_max < e.level) {
        return false;
    }
    if (rule.body_class != 7 && build_body_class(roster, build) != rule.body_class) {
        return false;
    }
    // one listed leg kind (8 and -1 skipped) must match; the last one tested decides
    bool found = true;
    for (const int leg : rule.leg_classes) {
        if (leg >= 0 && leg != 8) {
            if (leg_kind_group_of(build_leg_class(roster, build)) != leg) {
                found = false;
            } else {
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }
    const float weight = build_weight_float(roster, build);
    if (rule.weight_min != -1.0f && rule.weight_min > weight) {
        return false;
    }
    if (rule.weight_max != -1.0f && rule.weight_max < weight) {
        return false;
    }
    if (rule.extra == 1 && build_camo_frames(roster, build) > 0) {
        return false;
    }
    return true;
}

// decomp: RumbleContext::fill_opponent_queues 0x451F0
void fill_opponent_queues(World& world, const StoryTables& tables, const RosterTables& roster,
                          int slot, int tournament) {
    Story& story = world.story;
    RumbleSlot& r = world.rumble[slot];
    if (tournament < 0) {
        return;
    }
    // Burning Chrome: 52 is one of 52..54 once the game is beaten (rand_float_crt(3), the world's
    // generator here)
    if (tournament == tables.burning_chrome_tournaments[0] && story.game_beaten) {
        int roll = rand_int(world.random, 3);
        if (roll < 0 || roll >= 3) {
            roll = 0;
        }
        tournament = tables.burning_chrome_tournaments[roll];
    }
    // the Aqualine ranker never comes back once won
    if (tournament == tournament_aqualine_ranker &&
        story.tournament_won[tournament_aqualine_ranker] == 1) {
        slot_tournament(world, slot) = -1;
        return;
    }
    slot_tournament(world, slot) = tournament;
    const TournamentRow& t = tournament_row(tables, tournament);
    r.regular_quota = t.regular_quota;
    r.entrants = t.max_opponents + 1;
    r.fixed_queued = false;
    for (bool& queued : r.pool_queued) {
        queued = false;
    }
    if (r.entrants > roster_size) {
        r.entrants = roster_size;
    }
    for (OpponentQueue& q : r.queues) {
        queue_clear(q);
    }
    for (int i = 0; i < 8; ++i) {
        const int pool = t.pools[i];
        if (pool < 0) {
            continue;
        }
        const PoolRow& row = tables.pools[static_cast<std::size_t>(pool)];
        for (int j = 0; j < 8; ++j) {
            if (opponent_fits_tournament(tables, roster, row.alternates[j], tournament)) {
                queue_opponent(r, story, make_ticket(row.alternates[j], 0, pool, j, -1));
            }
        }
        if (alternates_done(story.pools[pool]) &&
            opponent_fits_tournament(tables, roster, row.primary, tournament)) {
            if (queue_opponent(r, story, make_ticket(row.primary, 1, pool, -1, -1))) {
                r.pool_queued[i] = true;
            }
        }
    }
    if ((story.tournament_won[tournament] == 1 || t.rule == rule_first_ranker ||
         t.rule == rule_area_ranker) &&
        rumble_pools_done(world, tables, slot)) {
        if (queue_opponent(r, story, make_ticket(t.fixed_opponent, 2, -1, -1, tournament))) {
            r.fixed_queued = true;
        }
    }
    if (t.reserve_pool >= 0) {
        const PoolRow& row = tables.pools[static_cast<std::size_t>(t.reserve_pool)];
        for (int k = 0; k < 8; ++k) {
            if (opponent_fits_tournament(tables, roster, row.alternates[k], tournament)) {
                queue_append(r.queues[2], make_ticket(row.alternates[k], 0, t.reserve_pool, k, -1));
            }
        }
    }
    for (RosterEntry& entry : r.roster) {
        entry = RosterEntry{};
        entry.ticket = RumbleTicket{0, {0, 0, -1, -1}};
        entry.player = -2;
    }
}

// decomp: rumble_pools_done 0x4596D
bool rumble_pools_done(const World& world, const StoryTables& tables, int slot) {
    const TournamentRow& t = tournament_row(tables, world.story.lineup[slot]);
    for (const int pool : t.pools) {
        if (pool < 0) {
            continue;
        }
        const PoolProgress& p = world.story.pools[pool];
        if (!alternates_done(p) || p.primary == 0) {
            return false;
        }
    }
    return true;
}

bool choose_opponent(World& world, int slot, RumbleTicket& ticket, int& queue) {
    RumbleSlot& r = world.rumble[slot];
    const int out = r.queues[0].out + r.queues[1].out + r.queues[2].out;
    ticket = RumbleTicket{};
    // rand_float(1) < 0 never holds, but it draws
    rand_table_next(world.random);
    if (r.entrants <= out) {
        return false;
    }
    for (OpponentQueue& q : r.queues) {
        if (q.drawable <= 0) {
            q.drawable = q.ready;
        }
    }
    // rand_float(1) < 0.5 is a draw under 16384, < 0.7 one of 22937 or less (value / 32768)
    int pick = 0;
    const bool regular_wanted = r.regular_quota > r.queues[0].out + r.queues[1].out ||
                                rand_table_next(world.random) < 16384;
    if (regular_wanted && (r.queues[0].drawable > 0 || r.queues[1].drawable > 0)) {
        if (r.queues[0].drawable > 0) {
            if (r.queues[1].drawable > 0) {
                pick = rand_table_next(world.random) <= 22937 ? 1 : 2;
            } else {
                pick = 1;
            }
        } else {
            pick = 2;
        }
    } else if (r.queues[2].drawable > 0) {
        pick = 3;
    }
    if (pick == 0) {
        return false;
    }
    OpponentQueue& q = r.queues[pick - 1];
    if (q.drawable <= 0 || q.ready <= 0) {
        return false;
    }
    queue = pick;
    // roll in [0, 1): (drawable - 1) x value / 32768, truncated (exact in float: 22 bits)
    int index = static_cast<int>((static_cast<std::int64_t>(q.drawable - 1) *
                                  rand_table_next(world.random)) >> 15);
    if (index < 0) {
        index = 0;
    }
    ticket = q.tickets[index];
    if (ticket.opponent != -1 && ticket_live(ticket)) {
        for (int i = index; i < q.count - 1 && i + 1 < queue_capacity; ++i) {
            q.tickets[i] = q.tickets[i + 1];
        }
        q.drawable -= 1;
        q.ready -= 1;
        if (q.ready < 0) {
            q.ready = 0;
        }
        q.out += 1;
        return true;
    }
    return false;
}

void materialize_actor(World& world, const RosterTables& roster, int slot, int index,
                       const RumbleTicket& ticket, int queue) {
    RumbleSlot& r = world.rumble[slot];
    RosterEntry& e = r.roster[index];
    if (ticket.opponent < 0 || !ticket_live(ticket)) {
        return;
    }
    const OpponentEntry& row = opponent_entry(roster, ticket.opponent);
    e.build = roster_build_row(roster, row.loadout);
    e.pilot = pilot_row(roster, row.pilot);
    e.ai_profile = static_cast<std::int16_t>(row.ai_profile);
    e.level = static_cast<std::int16_t>(row.level);
    e.player = -1;
    e.ticket = ticket;
    // the pilot's pay at the level, then x (1 + level / 64) above 0 (float, as retail)
    CurveRow pay;
    pay.base = static_cast<float>(static_cast<unsigned>(e.pilot.value_base));
    pay.at_mid = static_cast<float>(static_cast<unsigned>(e.pilot.value_at_mid));
    pay.at_max = static_cast<float>(static_cast<unsigned>(e.pilot.value_at_max));
    pay.mid = static_cast<std::uint8_t>(e.pilot.value_mid);
    pay.negative = e.pilot.value_negative;
    e.destruction_value = curve_unsigned(pay, static_cast<std::int8_t>(e.level));
    if (e.level > 0) {
        const float scale = static_cast<float>(e.level) / 64.0f + 1.0f;
        e.destruction_value = static_cast<int>(
            static_cast<float>(static_cast<unsigned>(e.destruction_value)) * scale);
    }
    e.queue = queue;
    // names[roster]: the build's name, 0x12 bytes in retail (here the whole UTF-8 name)
    const std::string& name = row.loadout >= 0 &&
                                      static_cast<std::size_t>(row.loadout) < roster.build_names.size()
                                  ? roster.build_names[static_cast<std::size_t>(row.loadout)]
                                  : roster.build_names.front();
    std::memset(e.name, 0, sizeof e.name);
    std::memcpy(e.name, name.c_str(), name.size() < sizeof e.name - 1 ? name.size() : sizeof e.name - 1);
    // New Game+: the cycles added to the level and every tune (destruction value already set)
    if (world.story.game_beaten) {
        e.level = static_cast<std::int16_t>(level_bonus(world.story, e.level));
        e.build.body_tune = static_cast<std::int8_t>(level_bonus(world.story, e.build.body_tune));
        e.build.leg_tune = static_cast<std::int8_t>(level_bonus(world.story, e.build.leg_tune));
        e.build.chip_tune = static_cast<std::int8_t>(level_bonus(world.story, e.build.chip_tune));
        for (RosterWeapon& w : e.build.weapon) {
            w.tune = static_cast<std::int8_t>(level_bonus(world.story, w.tune));
        }
    }
}

void release_opponent(World& world, const StoryTables& tables, const RosterTables& roster,
                      int slot, const RosterEntry& entry, bool killed_by_player, bool fixed) {
    RumbleSlot& r = world.rumble[slot];
    Story& story = world.story;
    if (entry.ticket.opponent != -1) {
        switch (entry.queue) {
        case 1:
            r.queues[0].out -= 1;
            if (killed_by_player) {
                // shot down by the player: it comes back as a beaten opponent
                r.queues[0].count -= 1;
                r.queues[1].count += 1;
                hand_back(r.queues[1], entry.ticket);
            } else {
                hand_back(r.queues[0], entry.ticket);
            }
            break;
        case 2:
            r.queues[1].out -= 1;
            hand_back(r.queues[1], entry.ticket);
            break;
        case 3:
            r.queues[2].out -= 1;
            hand_back(r.queues[2], entry.ticket);
            break;
        default:
            break;
        }
    }
    // a pool whose alternates are all done sends its primary in, once
    const int tournament = story.lineup[slot];
    const TournamentRow& t = tournament_row(tables, tournament);
    for (int i = 0; i < 8; ++i) {
        const int pool = t.pools[i];
        if (pool < 0) {
            continue;
        }
        const PoolRow& row = tables.pools[static_cast<std::size_t>(pool)];
        PoolProgress& p = story.pools[pool];
        if (alternates_done(p) && opponent_fits_tournament(tables, roster, row.primary, tournament) &&
            !r.pool_queued[i] && p.primary == 0) {
            queue_insert_at_ready(r.queues[0], make_ticket(row.primary, 1, pool, -1, -1));
            r.pool_queued[i] = true;
        }
    }
    if (fixed) {
        rumble_fixed_opponent_queue(world, tables, slot);
    }
}

// decomp: rumble_fixed_opponent_queue 0x460A3
void rumble_fixed_opponent_queue(World& world, const StoryTables& tables, int slot) {
    RumbleSlot& r = world.rumble[slot];
    const int tournament = world.story.lineup[slot];
    if (tournament < 0 || r.fixed_queued || !rumble_pools_done(world, tables, slot)) {
        return;
    }
    if (world.story.tournament_won[tournament] == 0) {
        const TournamentRow& t = tournament_row(tables, tournament);
        queue_insert_at_ready(r.queues[0], make_ticket(t.fixed_opponent, 2, -1, -1, tournament));
        r.fixed_queued = true;
    }
}
