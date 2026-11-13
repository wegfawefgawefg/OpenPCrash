// The job map's fighters (decomp src/hand/u_043c80.c job_wanderer_start,
// job_wanderer_update, rumble_roster_starts_set; the route walk is
// src/hand/ai_target_track.c ai_route_start_nearest, ai_route_advance,
// ai_route_point_reached; findings "Area select and the job menu"). Each drawn opponent of
// an ordinary job walks the area's day stage routes for 120 to 235 s, then leaves; 20 to 38 s
// later a new draw takes its place, as long as more than one is on the map. Entering the job
// starts every entrant where its walk was heading.
//
// Menu code in float as retail's; it changes the world's rosters (release, draw) and draws from
// the world's generator, as retail's menu does from its own (rand_float_crt: the world's here).
#include "screens/campaign/area_select.hpp"

#include "assets/stage_tables.hpp"
#include "cook/pack_library.hpp"
#include "screens/flow/front_end.hpp"
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <bit>
#include <cmath>
#include <cstdio>

namespace {

// a float into the world's fixed point, exactly (as the cook converts the original's floats)
Fx float_fx(float value) {
    return fx_bits(std::bit_cast<std::uint32_t>(value));
}

float rand_unit(World& world) {
    return static_cast<float>(fx_float(rand_fx(world.random, fx(1))));
}

void point_of(const NavNode& node, int i, float out[3]) {
    const int last = static_cast<int>(node.points.size()) - 1;
    const FxVec3& p = node.points[static_cast<std::size_t>(i < last ? i : last)];
    out[0] = static_cast<float>(fx_float(p.x));
    out[1] = static_cast<float>(fx_float(p.y));
    out[2] = static_cast<float>(fx_float(p.z));
}

// bezier3_point on the segment's four points
// decomp: bezier3_point 0x5AEA8
void bezier(const NavNode& node, int segment, float t, float out[3]) {
    float p[4][3];
    for (int i = 0; i < 4; ++i) {
        point_of(node, segment * 3 + i, p[i]);
    }
    const float s = 1.0f - t;
    const float w[4] = {s * s * s, 3.0f * s * s * t, 3.0f * s * t * t, t * t * t};
    for (int k = 0; k < 3; ++k) {
        out[k] = p[0][k] * w[0] + p[1][k] * w[1] + p[2][k] * w[2] + p[3][k] * w[3];
    }
}

float length3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// the area's day stage: its route graph (ai_nav_resource_select(area * 2)) and entry points
bool ground_load(FrontEnd& fe, Ui& ui, int area) {
    Campaign& c = fe.campaign;
    const int stage = area * 2;
    std::string error;
    if (!c.stages_loaded) {
        c.stages_loaded = load_stage_tables(ui.data.packs->core, c.stages, error);
        if (!c.stages_loaded) {
            std::fprintf(stderr, "job map: %s\n", error.c_str());
        }
    }
    if (c.job_nav_stage != stage) {
        c.job_nav_stage = -1;
        if (!load_nav_graph(c.job_nav, *ui.data.packs, stage, error)) {
            std::fprintf(stderr, "job map: %s\n", error.c_str());
            return false;
        }
        c.job_nav_stage = stage;
    }
    return c.stages_loaded && !c.job_nav.nodes.empty();
}

const std::vector<StageEntryPoint>& entries_of(const FrontEnd& fe, int area) {
    return fe.campaign.stages.stages[area * 2].entry_points;
}

// standing over one (0 to 10.5 above, under 5.97 across)
// decomp: stage_entry_point_find 0x447B9
bool at_entry_point(const FrontEnd& fe, int area, const float p[3]) {
    for (const StageEntryPoint& e : entries_of(fe, area)) {
        const float above = p[1] - e.position[1];
        const float dx = p[0] - e.position[0], dz = p[2] - e.position[2];
        if (above < 10.5f && above > 0.0f && std::sqrt(dx * dx + dz * dz) < 5.97f) {
            return true;
        }
    }
    return false;
}

// the nearest point of a node of a kind in the mask (a fresh
// route at a door takes the door paths), steered at from the node's first segment
// decomp: ai_route_start_nearest 0x59F30
void route_start_nearest(const FrontEnd& fe, int area, JobWanderer& w, int mask) {
    const NavGraph& nav = fe.campaign.job_nav;
    if (!w.placed) {
        w.node = 0;
        if (at_entry_point(fe, area, w.pos)) {
            mask = 1;
        }
    }
    float best = 1000.0f;
    int node = w.node < 0 ? 0 : w.node;
    float from[3] = {};
    for (int n = 0; n < static_cast<int>(nav.nodes.size()); ++n) {
        const NavNode& nn = nav.nodes[static_cast<std::size_t>(n)];
        if (!(mask & (1 << nn.kind))) {
            continue;
        }
        for (int k = 0; k < 3 * nn.segments; ++k) {
            float p[3];
            point_of(nn, k, p);
            const float d[3] = {p[0] - w.pos[0], p[1] - w.pos[1], p[2] - w.pos[2]};
            const float dist = length3(d);
            if (dist < best) {
                best = dist;
                node = n;
                from[0] = p[0], from[1] = p[1], from[2] = p[2];
            }
        }
    }
    w.node = node;
    w.point_count = nav.nodes[static_cast<std::size_t>(node)].segments;
    w.point_index = 0;
    w.progress = 0.0f;
    w.from[0] = from[0], w.from[1] = from[1], w.from[2] = from[2];
    w.placed = true;
}

// ai_route_advance with prefer 0 and avoid 0x83 (door paths, kind 1 and 7 links)
// decomp: ai_route_advance 0x5A229
void route_advance(World& world, const NavGraph& nav, JobWanderer& w) {
    const NavNode& node = nav.nodes[static_cast<std::size_t>(w.node)];
    // ai_route_point_reached: within 6 (2 on kind 7), or across and over 3 above or below
    const float radius = node.kind == 7 ? 2.0f : 6.0f;
    const float d[3] = {w.pos[0] - w.from[0], w.pos[1] - w.from[1], w.pos[2] - w.from[2]};
    const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    if (!(length3(d) < radius || (flat < radius && std::fabs(d[1]) > 3.0f))) {
        return;
    }
    w.progress += node.kind == 7 ? 0.05f : 0.2f;
    if (w.progress < 1.0f) {
        bezier(node, w.point_index, w.progress, w.from);
        return;
    }
    if (w.point_index < w.point_count - 1) {
        ++w.point_index;
        w.progress = 0.0f;
        point_of(node, w.point_index * 3, w.from);
        return;
    }
    const int count = static_cast<int>(node.links.size());
    if (count <= 0) {
        return;
    }
    int link = rand_table_next(world.random) % count;
    for (int i = 0; i < count; ++i) {
        const int next = node.links[static_cast<std::size_t>(link)];
        const int kind = next >= 0 && next < static_cast<int>(nav.nodes.size())
                             ? nav.nodes[static_cast<std::size_t>(next)].kind
                             : 0;
        if (0x83 & (1 << kind)) {
            link = (link + 1) % count;
        } else {
            break;
        }
    }
    // ai_route_enter_node
    const int next = node.links[static_cast<std::size_t>(link)];
    if (next < 0 || next >= static_cast<int>(nav.nodes.size())) {
        return;
    }
    w.node = next;
    w.point_count = nav.nodes[static_cast<std::size_t>(next)].segments;
    w.point_index = 0;
    w.progress = 0.0f;
    point_of(nav.nodes[static_cast<std::size_t>(next)], 0, w.from);
}

// decomp: job_wanderer_kind_set 0x44477
void kind_set(const FrontEnd& fe, JobWanderer& w, const RosterEntry* entry, const Ui& ui) {
    w.speed = 10.0f / static_cast<float>(ui.frame_cadence);
    w.body_class = 7;
    w.leg_kind = 8;
    if (entry) {
        w.body_class = build_body_class_or_7(fe.campaign.roster, entry->build);
        w.leg_kind = build_leg_kind_or_8(fe.campaign.roster, entry->build);
    }
}

// a random node that isn't a door path or kind 1, 6 or 7, its start
// decomp: enpt_random_node 0x5803D
void random_node(World& world, const NavGraph& nav, float out[3]) {
    const int count = static_cast<int>(nav.nodes.size());
    int roll = rand_int(world.random, count);
    for (int i = 0; i < count; ++i) {
        const int kind = nav.nodes[static_cast<std::size_t>(roll)].kind;
        if (kind == 6 || kind == 0 || kind == 1 || kind == 7) {
            roll = (roll + 1) % count;
            continue;
        }
        point_of(nav.nodes[static_cast<std::size_t>(roll)], 0, out);
        return;
    }
    out[0] = out[1] = out[2] = 0.0f;
}

int stay_timer(World& world, const Ui& ui) {
    const int c = ui.frame_cadence;
    return c * 120 + static_cast<int>(static_cast<float>(c * 115) * rand_unit(world));
}

} // namespace

void job_wanderers_start(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    if (a.area < 0 || a.area >= 3 || !ground_load(fe, ui, a.area)) {
        return;
    }
    World& world = *fe.world;
    RumbleSlot& r = world.rumble[a.area];
    for (int i = 1; i < r.entrants && i < roster_size; ++i) {
        JobWanderer& w = a.wanderers[i];
        w = JobWanderer{};
        kind_set(fe, w, &r.roster[i], ui);
        random_node(world, fe.campaign.job_nav, w.pos);
        w.timer = stay_timer(world, ui);
        route_start_nearest(fe, a.area, w, 0x38);
    }
}

void job_wanderers_update(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    if (a.area < 0 || a.area >= 3 || fe.campaign.job_nav_stage != a.area * 2 ||
        tournament_kind(fe.story_tables, a.area, a.jobs[a.area]) != 0) {
        return;
    }
    World& world = *fe.world;
    RumbleSlot& r = world.rumble[a.area];
    const NavGraph& nav = fe.campaign.job_nav;
    const int c = ui.frame_cadence;
    for (int i = 1; i < r.entrants && i < roster_size; ++i) {
        JobWanderer& w = a.wanderers[i];
        RosterEntry& entry = r.roster[i];
        if (entry.player != -2) {
            // on the map: its time up (and someone else still about), it leaves
            if (--w.timer < 0 && job_map_fighter_count(fe, a) > 1) {
                release_opponent(world, fe.story_tables, fe.campaign.roster, a.area, entry, false,
                                 false);
                entry.player = -2;
                w.timer = c * 20 + static_cast<int>(static_cast<float>(c * 18) * rand_unit(world));
            }
            if (w.node >= 0 && w.node < static_cast<int>(nav.nodes.size())) {
                route_advance(world, nav, w);
            }
            float d[3] = {w.from[0] - w.pos[0], w.from[1] - w.pos[1], w.from[2] - w.pos[2]};
            const float len = length3(d);
            if (len > 0.0f) {
                d[0] /= len, d[1] /= len, d[2] /= len;
            }
            for (int k = 0; k < 3; ++k) {
                w.dir[k] = d[k];
                w.pos[k] += d[k] * w.speed;
            }
            continue;
        }
        // off the map: once its wait is out a new draw walks in from a random gate, or the
        // entry stays empty
        if (--w.timer >= 0) {
            continue;
        }
        RumbleTicket ticket;
        int queue = 0;
        if (choose_opponent(world, a.area, ticket, queue)) {
            materialize_actor(world, fe.campaign.roster, a.area, i, ticket, queue);
            w = JobWanderer{};
            kind_set(fe, w, &entry, ui);
            w.timer = stay_timer(world, ui);
            entry.player = -1;
            const std::vector<StageEntryPoint>& gates = entries_of(fe, a.area);
            const int count = static_cast<int>(gates.size());
            if (count > 0) {
                const int g = rand_int(world.random, count) % count;
                for (int k = 0; k < 3; ++k) {
                    w.pos[k] = gates[static_cast<std::size_t>(g)].position[k];
                }
            }
            route_start_nearest(fe, a.area, w, 1);
        } else {
            entry = RosterEntry{};
            entry.ticket.opponent = -1;
            entry.player = -2;
        }
    }
}

void job_roster_starts_set(FrontEnd& fe, AreaSelectScreen& a) {
    RumbleSlot& r = fe.world->rumble[a.area];
    for (int i = 0; i < r.entrants && i < roster_size; ++i) {
        const JobWanderer& w = a.wanderers[i];
        if (!w.placed) {
            continue; // no walk (the player's entry, or no route graph): a gate as before
        }
        r.roster[i].start = {float_fx(w.from[0]), float_fx(w.from[1]), float_fx(w.from[2])};
        r.roster[i].start_set = true;
    }
}
