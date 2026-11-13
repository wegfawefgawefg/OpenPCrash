#include "input/dev_route.hpp"

#include "battle/battle_dev.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

// "a+start", "0x110": a pad word (--press's names), or 0 when a part isn't one
bool pad_word(const std::string& text, unsigned& bits) {
    static const struct {
        const char* name;
        unsigned bit;
    } names[] = {{"up", 0x1},     {"down", 0x2}, {"left", 0x4}, {"right", 0x8}, {"start", 0x10},
                 {"back", 0x20},  {"a", 0x100},  {"b", 0x200},  {"x", 0x400},   {"y", 0x800},
                 {"lt", 0x40},    {"rt", 0x80}};
    bits = 0;
    std::stringstream parts(text);
    std::string key;
    while (std::getline(parts, key, '+')) {
        bool known = false;
        for (const auto& n : names) {
            if (key == n.name) {
                bits |= n.bit;
                known = true;
            }
        }
        if (!known) {
            char* end = nullptr;
            const unsigned long word = std::strtoul(key.c_str(), &end, 0);
            if (key.empty() || *end != 0) {
                return false;
            }
            bits |= static_cast<unsigned>(word);
        }
    }
    return true;
}

// seat 0's fighter in the world, or null
const Fighter* human(const World& world) {
    for (const Fighter& f : world.fighters) {
        if (f.present && f.controller == 0) {
            return &f;
        }
    }
    return nullptr;
}

// PHANTOMCRASH_DEV_ROUTE_SLOTS: every fight slot as a fight action reads it
void slots_print(const World& world) {
    if (!std::getenv("PHANTOMCRASH_DEV_ROUTE_SLOTS") || !world.battle_state.live) {
        return;
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const BattleSlot& s = world.battle_state.slots[slot];
        const RosterEntry& e = world.rumble[world.battle_state.rumble].roster[slot];
        std::printf("  slot %d: %s opponent %d role %d queue %d scene 0x%x/%d durability %d "
                    "immune %d parked %d kills %d\n",
                    slot, f.present ? "in" : "--", e.ticket.opponent, e.ticket.progress.role,
                    e.queue, s.scene.state, s.scene.step, f.combat.durability,
                    f.combat.damage.immune_frames.raw >> 16, s.parked ? 1 : 0, f.combat.kills);
    }
}

} // namespace

bool load_dev_route(DevRoute& route, const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "can't read " + path;
        return false;
    }
    const std::size_t slash = path.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    const std::string file = path.substr(dir.size());
    std::string text;
    int number = 0;
    while (std::getline(in, text)) {
        ++number;
        const std::size_t hash = text.find('#');
        if (hash != std::string::npos) {
            text.resize(hash);
        }
        std::stringstream line(text);
        std::string state, action;
        DevRouteStep step;
        if (!(line >> state)) {
            continue;
        }
        // include FILE: another route's steps here (beside this one unless absolute)
        if (state == "include" && (line >> action)) {
            if (!load_dev_route(route, action[0] == '/' ? action : dir + action, error)) {
                return false;
            }
            continue;
        }
        step.where = file + ":" + std::to_string(number);
        if (!(line >> step.wait >> action)) {
            error = step.where + ": want STATE WAIT ACTION";
            return false;
        }
        step.state = state == "*" ? -1 : static_cast<int>(std::strtol(state.c_str(), nullptr, 16));
        step.action = action;
        unsigned bits = 0;
        const bool known = action == "-" || action == "kill" || action == "killall" ||
                           action == "die" || action == "door" || action.rfind("win", 0) == 0 ||
                           action == "quit" || action == "pose" || action.rfind("shot:", 0) == 0 ||
                           action.rfind("click:", 0) == 0 || action.rfind("every:", 0) == 0 ||
                           pad_word(action, bits);
        if (!known) {
            error = step.where + ": no action " + action;
            return false;
        }
        route.steps.push_back(step);
    }
    return true;
}

void dev_route_update(DevRoute& route, int state, bool blank, const World& world) {
    route.press = 0;
    route.click = false;
    if (state != route.state_seen) {
        route.state_seen = state;
        route.since = 0;
    }
    ++route.since;
    // a win or every step ends once its state has gone
    if (route.win_active && route.next < route.steps.size() &&
        route.steps[route.next].state != state) {
        route.win_active = false;
        route.hold = 0;
        ++route.next;
    }
    if (route.next >= route.steps.size() || blank) {
        return;
    }
    const DevRouteStep& step = route.steps[route.next];
    if ((step.state >= 0 && step.state != state) || route.since < step.wait) {
        return;
    }
    route.since = 0;
    const std::string& a = step.action;
    std::printf("route: %s in state 0x%x: %s\n", step.where.c_str(), state, a.c_str());
    if (a.rfind("win", 0) == 0) {
        // kill until the fixed opponent is down (or N kills: a tournament may have none), then
        // the door; the step holds until the fight's state goes
        const int most = a.size() > 4 ? std::atoi(a.c_str() + 4) : 30;
        route.win_active = true;
        route.hold = dev_shield;
        const Fighter* me = human(world);
        if (world.battle_state.live && me) {
            const bool done =
                world.battle_state.result.fixed_opponent_destroyed || me->combat.kills >= most;
            route.fight |= done ? dev_door : dev_kill;
            std::printf("route: win: %d kills, fixed opponent %s, durability %d, scene 0x%x: %s\n",
                        me->combat.kills,
                        world.battle_state.result.fixed_opponent_destroyed ? "down" : "up",
                        me->combat.durability, world.battle_state.slots[me->slot].scene.state,
                        done ? "door" : "kill");
            slots_print(world);
        }
        return; // the same step again
    }
    if (a.rfind("every:", 0) == 0) {
        // every WAIT updates the comma-separated actions (pad words, kill) until the state goes:
        // a lesson's lines accepted, its drones shot down
        route.win_active = true;
        std::stringstream items(a.substr(6));
        std::string item;
        while (std::getline(items, item, ',')) {
            unsigned bits = 0;
            if (item == "kill") {
                route.fight |= dev_kill;
            } else if (pad_word(item, bits)) {
                route.press |= bits;
            }
        }
        return; // the same step again
    }
    ++route.next;
    if (a == "-") {
        return;
    }
    if (a == "kill" || a == "killall" || a == "die" || a == "door") {
        slots_print(world);
    }
    if (a == "kill") {
        route.fight |= dev_kill;
    } else if (a == "killall") {
        route.fight |= dev_kill_all;
    } else if (a == "die") {
        route.fight |= dev_die;
    } else if (a == "door") {
        route.fight |= dev_door;
    } else if (a == "pose") {
        if (const Fighter* me = human(world)) {
            const double degrees = 180.0 / 3.14159265358979;
            std::printf("route: pose: at %.3f %.3f %.3f heading %.2f pitch %.2f aim %.2f "
                        "motion %d step %lld\n",
                        fx_float(me->position.x), fx_float(me->position.y),
                        fx_float(me->position.z),
                        fx_float(angle_radians(me->heading)) * degrees,
                        fx_float(angle_radians(me->camera.pitch)) * degrees,
                        fx_float(angle_radians(me->aim_yaw)) * degrees, me->motion_state,
                        static_cast<long long>(world.step));
        }
    } else if (a == "quit") {
        route.quit = true;
    } else if (a.rfind("shot:", 0) == 0) {
        // a bare name lands beside --capture's picture
        const std::string name = a.substr(5);
        route.shot = name.find('/') == std::string::npos ? route.shot_dir + name : name;
    } else if (a.rfind("click:", 0) == 0) {
        route.click = std::sscanf(a.c_str() + 6, "%f:%f", &route.click_x, &route.click_y) == 2;
    } else {
        pad_word(a, route.press);
        route.start = (route.press & 0x10) != 0;
    }
}
