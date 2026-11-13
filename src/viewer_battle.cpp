// The stage viewer's battle: the fight in the world (viewer_world.cpp), each fighter drawn with
// its own build, the --dummy target, and the AI trace.
#include "viewer_battle.hpp"

#include "render/scenery.hpp"
#include "view/combat_viewer.hpp"
#include "viewer_world.hpp"

#include <algorithm>
#include <cstdio>

namespace {

const char* handler_name(AiHandler handler) {
    static const char* names[] = {"stand_fire",    "stand_fire_upd", "route",        "route_upd",
                                  "pause2",        "pause2_upd",     "combat",       "combat_upd",
                                  "lost_target",   "search_upd",     "pause4",       "pause4_upd",
                                  "back_off",      "back_off_upd",   "wander",       "wander_upd",
                                  "ram_start",     "ram_jump",       "ram_coast",    "crate_start",
                                  "crate",         "rk_aim",         "rk_aim_upd",   "rk_hold",
                                  "rk_hold_upd",   "rk_route",       "rk_route_upd", "rk_attack",
                                  "rk_attack_upd", "rk_strafe",      "rk_strafe_upd"};
    const auto i = static_cast<std::size_t>(handler);
    return i < sizeof names / sizeof names[0] ? names[i] : "?";
}

} // namespace

bool viewer_battle_start(State& state, Scenery& scenery, Renderer& renderer, PackLibrary& packs,
                         std::string& error) {
    const Options& options = state.options;
    World& world = state.world;
    // the fight in the world, each opponent's look loaded just before it spawns
    const auto look = [&](int slot, int build, std::string& why) {
        return load_scenery_fighter(scenery, renderer, packs, slot, build, why);
    };
    if (!viewer_world_start(world, options, packs, look, error)) {
        return false;
    }
    const std::vector<std::string>& opponents = options.opponents;
    const int spawned = 1 + std::min(static_cast<int>(opponents.size()), fighter_slots - 1);
    // --dummy: a still target after the others (src/view/combat_viewer.hpp)
    const int dummy_slot = spawned;
    if (options.dummy >= 0 && dummy_slot < fighter_slots) {
        if (!load_scenery_fighter(scenery, renderer, packs, dummy_slot, options.dummy, error)) {
            return false;
        }
        viewer_spawn_dummy(world, options, dummy_slot);
    }
    state.view.follow = options.follow;
    return true;
}

void viewer_trace_ai(const World& world) {
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        if (!f.present || f.controller != -1) {
            continue;
        }
        const AiController& c = world.ai[static_cast<std::size_t>(slot)];
        std::printf("ai %lld slot %d state %d %s mode %d target %d mem %d dist %.2f pos %.2f %.2f "
                    "%.2f hd %.1f motion %d axes %.2f %.2f %.2f %.2f buttons %x route %d/%d "
                    "kind %d seg %d from %.2f %.2f %.2f timers %d %d rng %d\n",
                    static_cast<long long>(world.step), slot, c.state, handler_name(c.handler),
                    c.mode, c.target, c.target_memory, fx_float(c.target_distance),
                    fx_float(f.position.x), fx_float(f.position.y), fx_float(f.position.z),
                    fx_float(angle_degrees(f.heading)), f.motion_state,
                    fx_float(c.record.control_axis[0]), fx_float(c.record.control_axis[1]),
                    fx_float(c.record.control_axis[2]), fx_float(c.record.control_axis[3]),
                    c.record.control_buttons, c.route.current, c.route.point_index, c.node_kind,
                    c.route.progress_steps, fx_float(c.route.from.x), fx_float(c.route.from.y),
                    fx_float(c.route.from.z), c.state_timer, c.reeval_timer, world.random.index);
    }
}
