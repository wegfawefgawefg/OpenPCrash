// The stage viewer's fight in the world: player 1's build in slot 0 and the --opponents as AIs in
// slots 1.. (src/battle/battle_spawn.hpp).
#include "viewer_world.hpp"

#include "ai/ai_links.hpp"
#include "battle/battle_spawn.hpp"
#include "cli.hpp"
#include "cook/pack_library.hpp"
#include "stage/stage_world.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// a viewer slot: a build row with its AI settings
struct SpawnSlot {
    int build = 0;
    bool ai = false;
    int opponent_id = -1; // an opponent entry, -1 for a plain build
    int role = -1;
    int level = 0;
    int profile = 4;
    int team = -1;
    int seat = 0; // a human's pad seat
};

} // namespace

bool viewer_world_start(World& world, const Options& options, PackLibrary& packs,
                        const SpawnHook& before_spawn, std::string& error) {
    // the opponent entries name their loadouts: read the table first
    AiData tables;
    if (!load_ai_data(tables, packs.core, packs, options.stage, error)) {
        return false;
    }
    std::vector<SpawnSlot> spawns;
    SpawnSlot player;
    player.build = options.build;
    player.ai = options.demo;
    player.level = options.ai_level;
    spawns.push_back(player);
    for (const std::string& text : options.opponents) {
        if (static_cast<int>(spawns.size()) >= fighter_slots) {
            break;
        }
        SpawnSlot spawn;
        spawn.ai = true;
        if (!text.empty() && text[0] == 'o') {
            const int id = std::atoi(text.c_str() + 1);
            if (id < 0 || id >= static_cast<int>(tables.opponents.size())) {
                error = "--opponents: no opponent entry " + text;
                return false;
            }
            const OpponentRow& row = tables.opponents[static_cast<std::size_t>(id)];
            spawn.build = row.loadout;
            spawn.opponent_id = id;
            spawn.role = 0;
            spawn.level = row.level;
            spawn.profile = row.profile;
            spawn.team = row.pilot >= 0 && row.pilot < static_cast<int>(tables.pilot_teams.size())
                             ? tables.pilot_teams[static_cast<std::size_t>(row.pilot)]
                             : -1;
        } else {
            spawn.build = std::atoi(text.c_str());
            spawn.level = options.ai_level;
        }
        spawns.push_back(spawn);
    }
    // --pose: each slot's seat, -1 an AI (src/viewer_pose.cpp)
    for (std::size_t slot = 0; slot < options.seats.size() && slot < spawns.size(); ++slot) {
        spawns[slot].ai = options.seats[slot] < 0;
        spawns[slot].seat = options.seats[slot];
    }

    std::vector<int> builds;
    for (const SpawnSlot& spawn : spawns) {
        builds.push_back(spawn.build);
    }
    if (options.dummy >= 0) {
        builds.push_back(options.dummy);
    }
    auto battle = std::make_shared<BattleData>();
    if (!load_battle_data(*battle, packs, options.stage, builds, error) ||
        battle->entries.empty()) {
        error = error.empty() ? "no entry points" : error;
        return false;
    }
    world.battle = battle;
    world.story.rumble_selected = options.stage;
    world.battle_phase = options.demo ? 1 : 0;
    world.debug_weapon_reach =
        fx_raw(static_cast<std::int32_t>(std::lround(options.ai_reach * 65536.0f)));
    // the battle's own state, then each slot's roster entry as the tournament's would be, spawned
    // through the battle entry (src/battle/battle_spawn.hpp); the viewer's fighters play at once
    // unless --intro asks for the entrance through the gates
    battle_state_begin(world, 6);
    RumbleSlot& rumble = world.rumble[world.battle_state.rumble];
    rumble.entrants = static_cast<int>(spawns.size());
    for (int slot = 0; slot < static_cast<int>(spawns.size()); ++slot) {
        SpawnSlot& spawn = spawns[static_cast<std::size_t>(slot)];
        if (slot > 0 && before_spawn && !before_spawn(slot, spawn.build, error)) {
            return false;
        }
        RosterEntry& entry = rumble.roster[slot];
        entry = RosterEntry{};
        entry.player = static_cast<std::int16_t>(spawn.ai ? -1 : spawn.seat);
        entry.ticket.opponent = spawn.opponent_id;
        entry.ticket.progress.role = spawn.role;
        entry.pilot.team = static_cast<std::int16_t>(spawn.team);
        entry.level = static_cast<std::int16_t>(spawn.level);
        entry.ai_profile = static_cast<std::int16_t>(spawn.profile);
        BattleSlot& run = world.battle_state.slots[slot];
        const std::vector<BuildRecord>& rows = battle->parts.builds;
        run.build = rows[spawn.build < 0 || spawn.build >= static_cast<int>(rows.size())
                             ? 0
                             : static_cast<std::size_t>(spawn.build)];
        run.rig = spawn.build;
        battle_spawn(world, slot);
        if (!options.intro) {
            run.scene = FighterScene{};
            run.ai_frozen = false;
        }
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        std::printf("slot %d: build %d %s level %d profile %d team %d at %.2f %.2f %.2f, leg class "
                    "%d radius %.3f turn %.3f deg\n",
                    slot, spawn.build, spawn.ai ? "AI" : "player", spawn.level, spawn.profile,
                    spawn.team, fx_float(f.position.x), fx_float(f.position.y),
                    fx_float(f.position.z), f.leg_class, fx_float(f.radius),
                    fx_float(angle_degrees(f.turn_step)));
    }
    return true;
}

void viewer_stage_start(World& world, const Options& options, const StageData& stage) {
    StageSetup setup;
    setup.selector = options.stage;
    setup.crate_tournament = stage_crate_tournament(stage, options.stage, options.day);
    stage_world_init(world.stage, stage, setup, world.random);
}
