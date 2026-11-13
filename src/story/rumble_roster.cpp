// The rumble contexts' rosters (decomp src/hand/u_0451f0.c, src/od/u_06c3e0.c tournaments_refill,
// src/od/u_05f6d0.c rumble_contexts_reset): the line-ups drawn from the opponent queues
// (rumble.cpp), the player's own entry, the day's tournaments refilled and every context reset
// for a new game.
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "story/story.hpp"
#include "world/world.hpp"

#include <cstdlib>

void rumble_roster_draw(World& world, const RosterTables& roster, int slot, int first) {
    for (int i = first; i < roster_size; ++i) {
        RumbleTicket ticket;
        int queue = 0;
        if (choose_opponent(world, slot, ticket, queue)) {
            materialize_actor(world, roster, slot, i, ticket, queue);
        } else {
            world.rumble[slot].roster[i].player = -2;
        }
    }
}

// decomp: rumble_roster_put 0x458A0
void rumble_roster_put(World& world, const RosterTables& roster, int slot,
                       const RosterBuild& build) {
    RosterEntry entry;
    entry.pilot = pilot_row(roster, 0);
    entry.build = build;
    entry.player = 0;
    entry.destruction_value = 0;
    entry.queue = 0;
    entry.ticket = RumbleTicket{};
    world.rumble[slot].roster[0] = entry;
}

void rumble_dev_entry(World& world, const RosterTables& roster) {
    if (const char* row = std::getenv("PHANTOMCRASH_DEV_ENTRY")) {
        const int slot = rumble_slot_of_selector(world.story.rumble_selected);
        rumble_roster_put(world, roster, slot, roster_build_row(roster, std::atoi(row)));
        world.battle_state.result = BattleResult{};
    }
}

// decomp: tournaments_refill 0x6CEDA
void tournaments_refill(World& world, const StoryTables& tables, const RosterTables& roster,
                        int day) {
    for (int s = 0; s < 3; ++s) {
        const int t = game_tournament_slot(world.story, tables, day, s);
        if (t < 0) {
            world.story.lineup[s] = -1;
            continue;
        }
        // the whole context cleared (retail keeps roster[0] across the clear, then the fill
        // empties it anyway)
        rumble_slot_clear(world.rumble[s]);
        fill_opponent_queues(world, tables, roster, s, t);
        if (world.story.lineup[s] < 0) {
            continue;
        }
        rumble_roster_draw(world, roster, s, 1);
    }
}

// decomp: rumble_contexts_reset 0x5FA3C
void rumble_contexts_reset(World& world, const StoryTables& tables, const RosterTables& roster) {
    world.story.rumble_selected = 0;
    for (int i = 0; i < rumble_count; ++i) {
        rumble_slot_clear(world.rumble[i]);
        fill_opponent_queues(world, tables, roster, i, 0);
        for (int j = 0; j < roster_size; ++j) {
            RosterEntry& e = world.rumble[i].roster[j];
            const OpponentEntry& row = opponent_entry(roster, j);
            e.build = roster_build_row(roster, row.loadout);
            e.pilot = pilot_row(roster, row.pilot);
            e.ai_profile = static_cast<std::int16_t>(row.ai_profile);
            e.level = static_cast<std::int16_t>(row.level);
            e.destruction_value = 0;
            e.ticket.opponent = -1;
            e.queue = 0;
            e.ticket.progress.role = -1;
            e.player = static_cast<std::int16_t>(j == 0 ? 0 : -1);
        }
    }
}
