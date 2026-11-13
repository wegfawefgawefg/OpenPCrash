// pc_sim: the world alone, with no window, GPU or sound (cmake/sim.cmake). It builds the stage
// viewer's fight from the cooked packs exactly as the game does (viewer_world.cpp) and steps it,
// for three hand-run checks:
//   --hash N       the whole world's hash every N steps, the same lines as the game's --hash, to
//                  compare between compilers and machines (docs/numbers.md "Determinism");
//   --rollback N   every N steps, a copy of the world N steps back re-stepped on the same inputs
//                  must reach the live world's hash (state kept outside the world, or a read of
//                  something a copy doesn't carry, shows here);
//   --time         the world's size and the step, copy, hash and re-simulation times
//                  (docs/multiplayer.md "Measured").
// The rest of the command line is the game's (cli.hpp): --cooked, --stage, --build, --opponents,
// --demo, --ai-level, --seed, --script, --day, --frames (the steps to run, 3600 if not given).
// It reads packs the game has cooked and never cooks.
#include "cli.hpp"
#include "cook/cook.hpp"
#include "battle_world.hpp"
#include "cook/data_hash.hpp"
#include "cook/pack_library.hpp"
#include "input/script_input.hpp"
#include "stage/stage_data.hpp"
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "story/story_tables.hpp"
#include "viewer_world.hpp"
#include "world/world.hpp"
#include "world/world_hash.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// pc_sim never cooks: a pack the game hasn't cooked (or one stale against data/) is an error
std::string pack_path(const std::string& cooked_dir, const std::string& name) {
    return cooked_dir + "/" + name + ".pack";
}
bool cook_all(const CookOptions&, CookReport&, std::string& error) {
    error = "the core pack is missing or stale: run the game once to cook";
    return false;
}
bool cook_packs(const CookOptions&, const CookPlan&, const std::vector<std::string>& names,
                CookReport&, std::string& error) {
    error = "pack " + (names.empty() ? std::string() : names[0]) +
            " is missing or stale: run the game once to cook";
    return false;
}

namespace {

using Clock = std::chrono::steady_clock;

double micros(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

struct SimFlags {
    int rollback = 0;
    bool time = false;
    bool replay = false;
    int parts = -1; // --parts N: the world's named parts' hashes after step N (0: before any)
};

// the sim's own flags out of argv; the rest go to parse_cli
std::vector<char*> take_flags(int argc, char** argv, SimFlags& flags) {
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rollback") == 0 && i + 1 < argc) {
            flags.rollback = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--time") == 0) {
            flags.time = true;
        } else if (std::strcmp(argv[i], "--replay") == 0) {
            flags.replay = true;
        } else if (std::strcmp(argv[i], "--parts") == 0 && i + 1 < argc) {
            flags.parts = std::atoi(argv[++i]);
        } else {
            rest.push_back(argv[i]);
        }
    }
    return rest;
}

// one step's input: the script's pad for seat 0, or nothing
Inputs next_inputs(InputScript& script) {
    Inputs in;
    if (!script.lines.empty()) {
        in.pads[0] = next_script_pad(script);
    }
    return in;
}

void print_times(const char* what, std::vector<double>& us) {
    if (us.empty()) {
        return;
    }
    std::sort(us.begin(), us.end());
    double sum = 0;
    for (double u : us) {
        sum += u;
    }
    std::printf("%s: mean %.1f us, median %.1f, p99 %.1f, max %.1f (%zu)\n", what,
                sum / static_cast<double>(us.size()), us[us.size() / 2],
                us[us.size() * 99 / 100], us.back(), us.size());
}

// what the fight did, from each step's events and fighters: shots, hits, kills, fighters coming
// in (the replacements), cloaks started, crates taken
struct Seen {
    int shots = 0, hits = 0, destroyed = 0, spawns = 0, cloaks = 0, crates = 0;
    int most_stage_events = 0;
    long long ended = -1; // the step the battle's end state first left 0
    bool present[fighter_slots] = {};
    int camo[fighter_slots] = {};
};

void count_events(const World& world, Seen& seen) {
    for (int i = 0; i < world.combat.event_count; ++i) {
        const CombatEventKind kind = world.combat.events[static_cast<std::size_t>(i)].kind;
        seen.shots += kind == event_muzzle ? 1 : 0;
        seen.hits += kind == event_hit_fighter ? 1 : 0;
        seen.destroyed += kind == event_destroyed ? 1 : 0;
    }
    for (const StageEvent& e : stage_events(world.stage)) {
        seen.crates += e.kind == StageEventKind::GetItem ? 1 : 0;
    }
    seen.most_stage_events = std::max(seen.most_stage_events, world.stage.event_count);
    if (seen.ended < 0 && world.battle_state.end_state != 0) {
        seen.ended = world.step;
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        seen.spawns += f.present && !seen.present[slot] ? 1 : 0;
        seen.cloaks += f.camo.state == camo_pressed && seen.camo[slot] != camo_pressed ? 1 : 0;
        seen.present[slot] = f.present;
        seen.camo[slot] = f.camo.state;
    }
}

// the front end's fight (--state 6, as the game's --state): a new game's day 0 with today's
// line-up and drawn opponents, then the battle as battle_run_start builds it (battle_world.cpp).
// The game also rolls Sonic Amp's stock with CRT rand (music_state_new, the profile), which pc_sim
// doesn't: compare the fight hash with the game's, the world hash between compilers. The Area
// Rankers' defeat lines are 0 ticks here (the game measures them from the scripts).
bool campaign_start(World& world, const Options& options, PackLibrary& packs,
                    const StageData& stage, StoryTables& story, RosterTables& roster,
                    std::string& error) {
    if (!load_story_tables(story, options.data_dir, error) ||
        !load_roster_tables(roster, options.data_dir, error)) {
        return false;
    }
    // init_front_end: game_state_new, the tables, rumble_contexts_reset; then --state's new game
    game_state_new(world, story);
    world.story_tables = &story;
    world.roster_tables = &roster;
    rumble_contexts_reset(world, story, roster);
    story_new_game(world.story, story, 0);
    tournaments_refill(world, story, roster, 0);
    rumble_dev_entry(world, roster);
    // battle_run_start
    BattleWorldSetup setup;
    setup.menu_state = options.start_state;
    const BattleWorldPlan plan = battle_world_plan(world, setup, roster, story);
    return battle_world_data(world, plan, packs, error) &&
           battle_world_spawn(world, setup, roster, stage, nullptr, error);
}

} // namespace

int main(int argc, char** argv) {
    SimFlags flags;
    std::vector<char*> rest = take_flags(argc, argv, flags);
    auto state = std::make_unique<Options>();
    Options& options = *state;
    if (!parse_cli(static_cast<int>(rest.size()), rest.data(), options)) {
        std::fprintf(stderr, "pc_sim [--hash N] [--rollback N] [--time] and the game's viewer "
                             "flags (--cooked, --stage, --build, --opponents, --demo, --seed, "
                             "--script, --frames)\n");
        return 2;
    }
    const int steps = options.frames > 0 ? options.frames : 3600;

    // the packs, the generator's table and the stage's tables
    PackLibrary packs;
    std::string error;
    if (!packs_start(packs, options.data_dir, options.cooked_dir, error) ||
        !load_rand_table(packs.core, error)) {
        std::fprintf(stderr, "pc_sim: %s\n", error.c_str());
        return 1;
    }
    data_hash_print(packs);
    auto stage = std::make_unique<StageData>();
    if (!load_stage_data(*stage, packs.core, error)) {
        std::fprintf(stderr, "pc_sim: %s\n", error.c_str());
        return 1;
    }
    // player 1's pad: --script, or --battle-script as the game's front end fights read it
    InputScript script;
    const std::string& script_path =
        options.script.empty() ? options.battle_script : options.script;
    if (!script_path.empty() && !load_input_script(script, script_path, error)) {
        std::fprintf(stderr, "pc_sim: %s\n", error.c_str());
        return 1;
    }

    // the fight: the front end's (--state) or the viewer's, the generator at --seed (0 when not
    // given, as a capture's)
    auto world = std::make_unique<World>();
    world->random.index = (options.seed >= 0 ? options.seed : 0) % rand_table_size;
    auto story = std::make_unique<StoryTables>();
    auto roster = std::make_unique<RosterTables>();
    const bool started =
        options.start_state >= 0
            ? campaign_start(*world, options, packs, *stage, *story, *roster, error)
            : viewer_world_start(*world, options, packs, nullptr, error);
    if (!started) {
        std::fprintf(stderr, "pc_sim: %s\n", error.c_str());
        return 1;
    }
    if (options.start_state < 0) {
        viewer_stage_start(*world, options, *stage);
    }
    // the replay's record: the fight's first world and every step's fight hash
    auto first = std::make_unique<World>(*world);
    std::vector<std::uint64_t> fight_hashes;

    // step it: the hash lines, the rollback check, the timings
    std::vector<Inputs> inputs;
    inputs.reserve(static_cast<std::size_t>(steps));
    auto snapshot = std::make_unique<World>(*world);
    std::int64_t snapshot_at = 0;
    int rollback_checks = 0, rollback_misses = 0;
    std::vector<double> step_us, copy_us, hash_us, resim_us;
    Seen seen;
    if (flags.parts == 0) {
        world_hash_parts_print(*world);
    }
    std::vector<double> by_count[fighter_slots + 1]; // step times by live fighters after it
    for (int s = 0; s < steps; ++s) {
        inputs.push_back(next_inputs(script));
        const Clock::time_point t0 = Clock::now();
        step_world(*world, inputs.back());
        const Clock::time_point t1 = Clock::now();
        step_us.push_back(micros(t1 - t0));
        int running = 0;
        for (const Fighter& f : world->fighters) {
            running += f.present && f.combat.durability > 0 ? 1 : 0;
        }
        by_count[running].push_back(step_us.back());
        world_hash_print(*world, options.hash);
        if (flags.replay) {
            fight_hashes.push_back(world_fight_hash(*world));
        }
        count_events(*world, seen);
        if (world->step == flags.parts) {
            world_hash_parts_print(*world);
        }

        // the rollback check: the snapshot re-stepped to now must be the live world
        if (flags.rollback > 0 && world->step - snapshot_at >= flags.rollback) {
            auto again = std::make_unique<World>(*snapshot);
            for (std::int64_t k = snapshot_at; k < world->step; ++k) {
                step_world(*again, inputs[static_cast<std::size_t>(k)]);
            }
            ++rollback_checks;
            if (world_hash(*again) != world_hash(*world)) {
                ++rollback_misses;
                std::printf("rollback: steps %lld..%lld re-stepped differ\n",
                            static_cast<long long>(snapshot_at),
                            static_cast<long long>(world->step));
            }
            *snapshot = *world;
            snapshot_at = world->step;
        }

        // the timings: every 60 steps a copy, a hash, and a rollback's 8 re-run steps
        if (flags.time && s >= 60 && s % 60 == 0) {
            const Clock::time_point c0 = Clock::now();
            auto copy = std::make_unique<World>(*world);
            const Clock::time_point c1 = Clock::now();
            const std::uint64_t hash = world_hash(*copy);
            const Clock::time_point c2 = Clock::now();
            copy_us.push_back(micros(c1 - c0));
            hash_us.push_back(micros(c2 - c1));
            // the world 8 steps back, copied and stepped 8 times on the logged inputs
            if (s >= 68) {
                World back = *world;
                const Clock::time_point r0 = Clock::now();
                World redo = back;
                for (int k = 0; k < 8; ++k) {
                    step_world(redo, inputs[static_cast<std::size_t>(s - 8 + k)]);
                }
                resim_us.push_back(micros(Clock::now() - r0));
            }
            if (hash == 0) {
                std::printf("hash 0\n"); // keeps the hash from being optimized away
            }
        }
    }

    // the replay (battle_replay.cpp's check): the first world in battle phase 2 on the same
    // inputs must match the live fight's hash at every step
    if (flags.replay) {
        auto again = std::make_unique<World>(*first);
        again->battle_phase = 2;
        int mismatch = -1;
        for (std::size_t k = 0; k < inputs.size(); ++k) {
            step_world(*again, inputs[k]);
            if (mismatch < 0 && world_fight_hash(*again) != fight_hashes[k]) {
                mismatch = static_cast<int>(k);
                // where: the live fight stepped again to the same step
                auto live = std::make_unique<World>(*first);
                for (std::size_t j = 0; j <= k; ++j) {
                    step_world(*live, inputs[j]);
                }
                std::printf("replay: step %zu differs in %s\n", k,
                            world_hash_diff(*again, *live).c_str());
            }
        }
        std::printf("replay: %zu steps, %s %d\n", inputs.size(),
                    mismatch < 0 ? "every step matches" : "first differs at step", mismatch);
    }

    // what happened, and how long it took
    int present = 0, alive = 0;
    for (const Fighter& f : world->fighters) {
        present += f.present ? 1 : 0;
        alive += f.present && f.combat.durability > 0 ? 1 : 0;
    }
    std::printf("pc_sim: %d steps, %d fighters (%d alive), rng %d, final hash %016llx\n", steps,
                present, alive, world->random.index,
                static_cast<unsigned long long>(world_hash(*world)));
    std::printf("seen: %d shots, %d hits, %d destroyed, %d spawns, %d cloaks, %d crates taken, "
                "most stage events in a step %d; battle end state %d (from step %lld), the "
                "human's durability %d\n",
                seen.shots, seen.hits, seen.destroyed, seen.spawns, seen.cloaks, seen.crates,
                seen.most_stage_events, world->battle_state.end_state, seen.ended,
                world->fighters[0].combat.durability);
    if (flags.rollback > 0) {
        std::printf("rollback: %d checks of %d steps, %d differ\n", rollback_checks,
                    flags.rollback, rollback_misses);
    }
    if (flags.time) {
        std::printf("sizeof(World) %zu: fighters %zu x %d, combat %zu, stage %zu, ai %zu x %d, "
                    "huds %zu x %d, rumble %zu x %d, garages %zu, story %zu, profile %zu\n",
                    sizeof(World), sizeof(Fighter), fighter_slots, sizeof(CombatWorld),
                    sizeof(StageWorld), sizeof(AiController), fighter_slots, sizeof(FighterHud),
                    fighter_slots, sizeof(RumbleSlot), rumble_count, sizeof(Garages),
                    sizeof(Story), sizeof(Profile));
        print_times("step", step_us);
        for (int n = fighter_slots; n >= 1; --n) {
            const std::string what = "step with " + std::to_string(n) + " fighters alive";
            print_times(what.c_str(), by_count[n]);
        }
        print_times("copy", copy_us);
        print_times("hash", hash_us);
        print_times("copy and 8 re-run steps", resim_us);
    }
    return 0;
}
