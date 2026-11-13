// Routes over the stage's ENPT graph (docs/port/ai.md "Routes"): ai_route_advance,
// ai_route_point_reached, ai_route_steer, ai_route_point_needs_jump,
// ai_route_choose_ahead with ai_route_pick_ahead, ai_route_start_nearest,
// ai_route_unstick. Distances and dot products don't see the port's z mirror.
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"
#include "mech/fighter_rates.hpp"

#include <array>

namespace {

// 0.69 (0x3F30A3D7): a walk just under the run threshold; 0.7 (0x3F333333) a run
constexpr Fx walk_stick = fx_bits(0x3F30A3D7);
constexpr Fx run_stick = fx_bits(0x3F333333);
// cos(30.0f) of 30 radians, retail's typo for 30 degrees (kept): about 81 degrees either side
constexpr Fx ahead_cone_cos = fx_f(0.15425144988758405);
// ai_route_point_needs_jump: 0.62831855 rad (36 degrees) of climb
constexpr Fx jump_climb = fx_f(0.62831855);
// the kind-7 pull, 60% of the way to the route point an update
constexpr Fx kind7_pull = fx_f(0.6);

// the kind masks
constexpr int mask_air = 0x3D;    // kinds 0, 2, 3, 4, 5
constexpr int mask_ground = 0xBD; // and 7

const NavNode& node_at(const Ai& ai, int index) {
    const std::vector<NavNode>& nodes = ai.data.nav.nodes;
    return nodes[static_cast<std::size_t>(
        index >= 0 && index < static_cast<int>(nodes.size()) ? index : 0)];
}

// bezier3_point on the four points from k: a record's start, its two controls and the
// next record's start
// decomp: bezier3_point 0x5AEA8
FxVec3 bezier(const NavNode& node, int k, Fx t) {
    const auto at = [&](int i) {
        const int j = k + i;
        return node.points[static_cast<std::size_t>(
            j < static_cast<int>(node.points.size()) ? j : node.points.size() - 1)];
    };
    const Fx s = fx(1) - t;
    const Fx t2 = fx_mul_near(t, t);
    const Fx w0 = fx_mul_near(fx_mul_near(s, s), s);
    const Fx w1 = fx_mul_near(fx_mul_near(fx_mul_near(s, fx(3)), s), t);
    const Fx w2 = fx_mul_near(fx_mul_near(s, fx(3)), t2);
    const Fx w3 = fx_mul_near(t2, t);
    return at(0) * w0 + at(1) * w1 + at(2) * w2 + at(3) * w3;
}

// progress in steps: 0.2 a step, 0.05 on kind 7 (5 and 20 steps reach 1.0 in retail's floats)
int progress_steps_to_end(int kind) {
    return kind == 7 ? 20 : 5;
}
Fx progress_of(int steps, int kind) {
    return kind == 7 ? fx_ratio(steps, 20) : fx_ratio(steps, 5);
}

// standing over an entry point (0 to 10.5 above, 5.97 across)
// decomp: stage_entry_point_find 0x447B9
bool at_entry_point(const Ai& ai, FxVec3 position) {
    for (const EntryPoint& entry : ai.world.battle->entries) {
        const Fx above = position.y - entry.position.y;
        const FxVec2 across{position.x - entry.position.x, position.z - entry.position.z};
        if (above < fx_f(10.5) && above > Fx{} && shorter_than(across, fx_f(5.97))) {
            return true;
        }
    }
    return false;
}

void enter_node(Ai& ai, int index) {
    AiRoute& route = ai.c.route;
    const NavNode& node = node_at(ai, index);
    route.node = node.id;
    route.current = index;
    route.point_count = node.segments;
    route.point_index = 0;
    route.progress_steps = 0;
    route.from = node.points.front();
    route.placed = true;
}

// ai_route_pick_ahead and ai_route_start_nearest: every point of every node of a kind in the mask
// (each segment's start and its two controls). Ahead: in the look cone first; plain: nearest
void pick_route_point(Ai& ai, FxVec3 position, FxVec3 look, int mask, bool mirrored, bool ahead) {
    AiRoute& route = ai.c.route;
    if (!route.placed) {
        route.node = 0;
        if (at_entry_point(ai, position)) {
            mask = 1; // a fresh fighter at a door takes the door paths
        }
    }
    const std::vector<NavNode>& nodes = ai.data.nav.nodes;
    std::int64_t best = static_cast<std::int64_t>(1000) << 16;
    bool found = false;
    int best_node = route.node;
    FxVec3 from{};
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) {
        const NavNode& node = nodes[static_cast<std::size_t>(n)];
        if (!(mask & (1 << node.kind))) {
            continue;
        }
        for (int k = 0; k < 3 * node.segments; ++k) {
            const FxVec3 to = node.points[static_cast<std::size_t>(k)] - position;
            const std::int64_t distance = length(to).raw;
            if (ahead) {
                const bool inside = !(ahead_cone_cos > dot(normalized(to), look));
                if (!inside && found) {
                    // retail skips without stepping its pointer: the rest of this node is this
                    // same skipped point (kept)
                    break;
                }
                if (inside && !found &&
                    distance < (static_cast<std::int64_t>(mirrored ? 100 : 20) << 16)) {
                    best = distance + (static_cast<std::int64_t>(10) << 16);
                    found = true;
                }
            }
            if (distance < best) {
                best = distance;
                best_node = n;
                from = node.points[static_cast<std::size_t>(k)];
            }
        }
    }
    // the node's first record, with the chosen point to steer at
    route.node = best_node;
    route.current = best_node;
    route.point_count = node_at(ai, best_node).segments;
    route.point_index = 0;
    route.progress_steps = 0;
    route.from = from;
    route.placed = true;
}

// ai_route_point_reached: within 6 (2 on kind 7), or across within it and over 3 above or below
bool point_reached(const Ai& ai) {
    const AiRoute& route = ai.c.route;
    const Fx radius = route_kind(ai) == 7 ? fx(2) : fx(6);
    const FxVec3 d = ai.f.position - route.from;
    return shorter_than(d, radius) || (shorter_than(FxVec2{d.x, d.z}, radius) &&
                                       fx_abs(route.from.y - ai.f.position.y) > fx(3));
}

// The port's fix for an out-of-ammo AI that never finds a door (docs/bugs.md): wandering (state
// 6), a finished node takes the first link on a shortest path to a door path (kind 0 or 1);
// retail took a random link and could wander forever
int link_toward_door(const Ai& ai, int from) {
    const std::vector<NavNode>& nodes = ai.data.nav.nodes;
    const int count = static_cast<int>(nodes.size());
    std::vector<int> first(static_cast<std::size_t>(count), -1);
    std::vector<int> open;
    const NavNode& start = node_at(ai, from);
    for (int k = 0; k < static_cast<int>(start.links.size()); ++k) {
        const int n = start.links[static_cast<std::size_t>(k)];
        if (n >= 0 && n < count && first[static_cast<std::size_t>(n)] < 0) {
            first[static_cast<std::size_t>(n)] = k;
            open.push_back(n);
        }
    }
    for (std::size_t i = 0; i < open.size(); ++i) {
        const NavNode& node = nodes[static_cast<std::size_t>(open[i])];
        if (node.kind == 0 || node.kind == 1) {
            return first[static_cast<std::size_t>(open[i])];
        }
        for (int n : node.links) {
            if (n >= 0 && n < count && first[static_cast<std::size_t>(n)] < 0) {
                first[static_cast<std::size_t>(n)] = first[static_cast<std::size_t>(open[i])];
                open.push_back(n);
            }
        }
    }
    return -1;
}

// ai_route_point_needs_jump: a point 3 or more above, moving, and steep: retail measures the climb
// with the point's absolute height (kept)
bool needs_jump(const Ai& ai) {
    const AiRoute& route = ai.c.route;
    if (!(ai.c.path_cache & 2)) {
        return false;
    }
    // ground_velocity is per update in retail: the step's doubled
    if (route.from.y - ai.f.position.y < fx(3) ||
        length(ai.f.ground_velocity * fx(2)) < fx_f(0.1)) {
        return false;
    }
    const Fx across =
        length(FxVec2{route.from.x - ai.f.position.x, route.from.z - ai.f.position.z});
    return angle_radians(angle_atan2(route.from.y, across)) > jump_climb;
}

// every 121st update off a door path, a coin picks a mirrored look or not
// decomp: ai_route_unstick 0x5EB00
void ai_route_unstick(Ai& ai) {
    AiController& c = ai.c;
    if (c.route.current >= 0 && route_kind(ai) == 0) {
        return;
    }
    if (c.route_stuck_frames < static_cast<std::int16_t>(ai_cadence << 2)) {
        ++c.route_stuck_frames;
        return;
    }
    c.route_stuck_frames = 0;
    ai_route_choose_ahead(ai, (ai_draw(ai) & 1) != 0);
}

} // namespace

int route_kind(const Ai& ai) {
    return ai.c.route.current >= 0 ? node_at(ai, ai.c.route.current).kind : -1;
}

// decomp: ai_route_choose_ahead 0x5EF41
void ai_route_choose_ahead(Ai& ai, bool mirrored) {
    if (ai.c.route.current >= 0 && route_kind(ai) == 0) {
        return;
    }
    FxVec3 look = ai_look_direction(ai);
    if (mirrored) {
        look.x = -look.x;
    }
    const int mask = motion_jumping(ai) || motion_falling(ai) ? mask_air : mask_ground;
    pick_route_point(ai, ai.f.position, look, mask, mirrored, true);
}

// decomp: ai_route_advance 0x5A229
int ai_route_advance(Ai& ai, int prefer, int avoid) {
    AiRoute& route = ai.c.route;
    if (route.current < 0 || !point_reached(ai)) {
        return 0;
    }
    const NavNode& node = node_at(ai, route.node);
    const int kind = node.kind;
    ++route.progress_steps;
    if (route.progress_steps < progress_steps_to_end(kind)) {
        route.from = bezier(node, 3 * route.point_index, progress_of(route.progress_steps, kind));
        return 1;
    }
    if (route.point_index < route.point_count - 1) {
        ++route.point_index;
        route.progress_steps = 0;
        route.from = node.points[static_cast<std::size_t>(3 * route.point_index)];
        return 1;
    }
    const int links = static_cast<int>(node.links.size());
    if (links > 0) {
        int link = 0;
        // retail's prefer loop tests this node's own kind, not a link's: a match takes link 0
        const int toward_door = prefer == 2 ? link_toward_door(ai, route.current) : -1;
        if (toward_door >= 0) {
            link = toward_door;
        } else if (!(prefer && (prefer & (1 << kind)))) {
            link = ai_draw(ai) % links;
            for (int i = 0; i < links; ++i) {
                if (avoid & (1 << node_at(ai, node.links[static_cast<std::size_t>(link)]).kind)) {
                    link = (link + 1) % links;
                } else {
                    break;
                }
            }
        }
        enter_node(ai, node.links[static_cast<std::size_t>(link)]);
    }
    return 2;
}

int ai_route_step(Ai& ai, int prefer, int avoid) {
    AiController& c = ai.c;
    const int kind = route_kind(ai);
    int step = 0;
    if (c.route_wait > ai_cadence || kind != 2) {
        step = ai_route_advance(ai, prefer, avoid);
        if (step) {
            c.route_stuck_frames = 0;
            if (kind == 7 && step == 2) {
                motion_set_animation(ai.f, *ai.world.battle, slot_idle, state_idle, motion_whole,
                                     Fx{});
            }
        }
    }
    return step;
}

// decomp: ai_route_steer 0x5A55C
void ai_route_steer(Ai& ai) {
    AiController& c = ai.c;
    Fighter& f = ai.f;
    AiRoute& route = c.route;
    release(ai, control_jump_held | control_jump_pressed);
    bool backing = false;
    // the port runs the unstick in state 6 too (the wandering fix, docs/bugs.md)
    ai_route_unstick(ai);
    if (fighter_durability(ai.world, ai.slot) < c.last_durability && route_kind(ai) == 7) {
        pick_route_point(ai, f.position, {}, mask_air, false, false);
    }
    if (!motion_jumping(ai) && !motion_falling(ai) && needs_jump(ai)) {
        c.jump_start_y = f.position.y;
        press(ai, control_jump_held | control_jump_pressed);
        c.axis_forward = fx(1);
        c.axis_leg_turn = Fx{};
    } else if (motion_falling(ai) && f.motion_mode == motion_lead_out) {
        c.axis_forward = fx(1);
        c.axis_leg_turn = Fx{};
        press(ai, control_jump_held);
    } else if (motion_jumping(ai) || motion_falling(ai)) {
        c.jump_start_y = f.position.y;
        press(ai, control_jump_held);
        ai_steer_to_point(ai, route.from);
        if (c.frames_since_backup < 10) {
            c.frames_since_backup = 0;
        }
    } else {
        ai_steer_to_point(ai, route.from);
        if (c.wants_jump && c.frames_since_backup > ai_cadence) {
            if (c.frames_since_jump >= ai_cadence * 3) {
                press(ai, control_jump_pressed | control_jump_held);
                c.frames_since_jump = 0;
            } else {
                // both arms of retail's level & 1 test back up
                c.axis_forward = fx(-1);
                backing = true;
                c.frames_since_backup = 0;
            }
        } else if (c.frames_since_backup < ai_cadence / 2) {
            c.axis_forward = fx(-1);
            if (c.frames_since_backup == ai_cadence / 2 - 1 && route.current >= 0 &&
                route_kind(ai) != 0) {
                ai_route_choose_ahead(ai, (ai_draw(ai) & 1) != 0);
            }
            backing = true;
        } else if (c.frames_since_backup < ai_cadence) {
            ai_steer_to_point(ai, route.from);
            backing = true;
            c.axis_forward = Fx{};
        } else if (c.frames_since_backup == ai_cadence) {
            c.axis_forward = Fx{};
            if (ai_roll(ai, 1, 4)) {
                if (c.age_frames & 1) {
                    press(ai, control_step_left);
                    release(ai, control_step_right);
                } else {
                    release(ai, control_step_left);
                    press(ai, control_step_right);
                }
            }
        } else {
            release(ai, control_dash);
            if (needs_jump(ai)) {
                press(ai, control_jump_pressed | control_jump_held);
            } else {
                release(ai, control_jump_pressed | control_jump_held);
            }
            c.flags &= ~1;
        }
    }
    release(ai, control_dash);
    switch (route_kind(ai)) {
    case 3:
        if (fx_abs(c.axis_forward) >= fx_f(0.7) && !backing) {
            c.axis_forward = walk_stick;
        }
        break;
    case 2:
        // stop and go: 31 updates standing, 89 moving
        if (!backing) {
            if (c.node_kind != 2) {
                c.route_wait = ai_cadence;
            }
            c.axis_forward = c.route_wait > ai_cadence ? walk_stick : Fx{};
            c.route_wait = static_cast<std::int16_t>((c.route_wait + (ai_cadence << 2) - 1) %
                                                     (ai_cadence << 2));
        }
        break;
    case 5:
        if (fighter_role(ai.world, ai.slot) >= 2) {
            press(ai, control_dash);
        }
        if (fx_abs(c.axis_forward) < fx_f(0.7) && !backing) {
            c.axis_forward = run_stick;
        }
        break;
    case 7:
        // a direct write past the physics, as retail
        f.position += (route.from - f.position) * kind7_pull;
        break;
    case 0:
        c.axis_forward = walk_stick;
        break;
    default:
        break;
    }
    if (c.target < 0) {
        press(ai, control_recentre);
    } else {
        release(ai, control_recentre);
    }
}
