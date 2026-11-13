#include "battle_world.hpp"

#include "battle/battle_data.hpp"
#include "battle/battle_spawn.hpp"
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

// the row an entry is posed and drawn as: an opponent's loadout, the rental, or the garage's
// machine appended after the table (garage_row)
int entry_rig(const BattleWorldSetup& setup, const RosterTables& roster, const RosterEntry& entry,
              bool rented, int garage_row) {
    if (entry.player >= 0) {
        return rented && setup.rental_build >= 0 ? setup.rental_build : garage_row;
    }
    return entry.ticket.opponent >= 0 ? opponent_entry(roster, entry.ticket.opponent).loadout : 0;
}

} // namespace

// decomp: result_screen_setup 0x94F6E, demo_battle_enter 0x4A620
BattleWorldPlan battle_world_plan(World& world, const BattleWorldSetup& setup,
                                  const RosterTables& roster, const StoryTables& story) {
    const int context = rumble_slot_of_selector(world.story.rumble_selected);
    RumbleSlot& rumble = world.rumble[context];
    // the demo: the context refilled for the attract demo's tournament or today's, and six
    // opponents drawn over its roster, the player's entry too (the session puts them back)
    if (setup.demo) {
        const int tournament =
            setup.demo_tournament >= 0 ? setup.demo_tournament : world.story.lineup[context];
        if (tournament >= 0) {
            fill_opponent_queues(world, story, roster, context, tournament);
        }
        rumble_roster_draw(world, roster, context, 0);
        rumble.entrants = roster_size;
    }
    const bool rented = world.battle_state.result.rented;
    const int garage_row = static_cast<int>(roster.builds.size());
    const RosterEntry& player = rumble.roster[0];
    BattleWorldPlan plan;
    plan.extra.push_back(build_record_of(player.build));
    plan.player_rig = entry_rig(setup, roster, player, rented, garage_row);

    // every build that may fight today: the roster's, and every ticket's in the three queues
    // (the replacements' rigs must be in the battle's data before it starts)
    for (int i = 0; i < roster_size; ++i) {
        if (rumble.roster[i].player != -2) {
            plan.rigs.push_back(entry_rig(setup, roster, rumble.roster[i], rented, garage_row));
        }
    }
    for (const OpponentQueue& q : rumble.queues) {
        for (int t = 0; t < q.count && t < queue_capacity; ++t) {
            if (q.tickets[t].opponent >= 0) {
                plan.rigs.push_back(opponent_entry(roster, q.tickets[t].opponent).loadout);
            }
        }
    }
    // and every opponent the fight can queue after it starts: release_opponent puts a pool's
    // primary in once its alternates are beaten, rumble_fixed_opponent_queue the fixed opponent
    // once the pools are done. Retail loads each replacement's files as it is drawn
    // (battle_fighter_loader_thread); without its rig here such a fighter had no pose or leg
    // motion and showed nothing but its name
    const int tournament = world.story.lineup[context];
    if (tournament >= 0 && tournament < static_cast<int>(story.tournaments.size())) {
        const TournamentRow& t = story.tournaments[static_cast<std::size_t>(tournament)];
        std::vector<int> opponents{t.fixed_opponent};
        for (const int pool : t.pools) {
            if (pool >= 0 && pool < static_cast<int>(story.pools.size())) {
                opponents.push_back(story.pools[static_cast<std::size_t>(pool)].primary);
            }
        }
        for (const int opponent : opponents) {
            if (opponent >= 0) {
                plan.rigs.push_back(opponent_entry(roster, opponent).loadout);
            }
        }
    }
    std::sort(plan.rigs.begin(), plan.rigs.end());
    plan.rigs.erase(std::unique(plan.rigs.begin(), plan.rigs.end()), plan.rigs.end());
    return plan;
}

std::shared_ptr<BattleData> battle_world_data(World& world, const BattleWorldPlan& plan,
                                              PackLibrary& packs, std::string& error) {
    auto battle = std::make_shared<BattleData>();
    if (!load_battle_data(*battle, packs, world.story.rumble_selected, plan.rigs, error,
                          plan.extra) ||
        battle->entries.empty()) {
        error = error.empty() ? "no entry points" : error;
        return nullptr;
    }
    world.battle = battle;
    return battle;
}

bool battle_world_spawn(World& world, const BattleWorldSetup& setup, const RosterTables& roster,
                        const StageData& stage, const SpawnHook& look, std::string& error) {
    const int selector = world.story.rumble_selected;
    const int context = rumble_slot_of_selector(selector);
    const RumbleSlot& rumble = world.rumble[context];
    const bool rented = world.battle_state.result.rented;
    const int garage_row = static_cast<int>(roster.builds.size());

    // battle_task_begin: the battle state, then each slot with a roster entry in slot order.
    // The phase: 0, free play's 4, the ending's 3, a demo's 1
    world.battle_phase = setup.demo ? 1 : setup.phase;
    world.combat = CombatWorld{};
    for (Fighter& f : world.fighters) {
        f = Fighter{};
    }
    // 6, 0x2A free play, 0x2F the test drive, 0xA the ending; the result screen's demo 0x1E, the
    // attract demo 1
    const BattleResult before = world.battle_state.result;
    battle_state_begin(world, setup.menu_state);
    if (setup.demo) {
        world.battle_state.result = before; // the sheet over it reads the fight's
    } else {
        const int today = world.story.lineup[context];
        world.battle_state.result.tournament_won_at_start =
            today >= 0 && tournament_won(world.story, today);
    }
    for (int i = 0; i < rumble.entrants && i < roster_size; ++i) {
        if (rumble.roster[i].player == -2) {
            continue;
        }
        battle_slot_prepare(world, i);
        BattleSlot& slot = world.battle_state.slots[i];
        slot.rig = entry_rig(setup, roster, rumble.roster[i], rented, garage_row);
        if (i > 0 && look && !look(i, slot.rig, error)) {
            return false;
        }
        battle_spawn(world, i);
    }
    // PHANTOMCRASH_DEV_DURABILITY=N (headless captures of the death scene): the human's durability
    if (const char* low = std::getenv("PHANTOMCRASH_DEV_DURABILITY")) {
        world.fighters[0].combat.durability = std::max(1, std::atoi(low));
    }
    // the stage: battle_stage_init with the world's generator after the fighters' draws
    StageSetup s;
    s.selector = selector;
    s.crate_tournament = stage_crate_tournament(stage, selector, world.story.day);
    s.gates_locked = battle_ranker_bout(world);
    // free play keeps the training stage's 8 fixed props; free play and the test drive never
    // step the supply craft (battle_stage_init's reads of the menu state)
    s.free_play = !setup.demo && setup.menu_state == app_free_play;
    s.craft_frozen =
        !setup.demo && (setup.menu_state == app_free_play || setup.menu_state == app_test_drive);
    s.ending = !setup.demo && setup.menu_state == app_ending_data; // the ending's routes, no crates
    stage_world_init(world.stage, stage, s, world.random);
    return true;
}
