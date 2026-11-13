#include "view/footsteps.hpp"

#include "battle/battle_data.hpp"
#include "mech/fighter_rates.hpp"
#include "world/world.hpp"

#include <vector>

namespace {

// the skid's footstep: every third update while skidding (age_frames % 3); R4, every sixth step
constexpr int skid_step_every = 6;
// the weapon switch node (sv_pose_walk walks the arms' graphs in its place)
constexpr int node_weapon_switch = 7;
constexpr int node_chain = 2;
constexpr int node_short_chain_end = 4;

// the keys of a footstep stream before a frame (motion_key_stream's hint, kept to the stream's
// own keys: retail's loop has no end test)
int keys_before(const std::vector<std::uint16_t>& times, Fx frame) {
    int n = 0;
    while (n < static_cast<int>(times.size()) && fx(times[static_cast<std::size_t>(n)]) < frame) {
        ++n;
    }
    return n;
}

// a stream's hint moved this pose. The cursors start again on a changed motion or a phase that
// didn't go forward; after a wrap the stream first catches up to a tick before the phase
bool stream_stepped(const std::vector<std::uint16_t>& times, Fx prev, Fx phase, bool changed) {
    if (changed || phase <= prev) {
        if (phase < prev) {
            return keys_before(times, phase) > keys_before(times, phase - fx(1));
        }
        return keys_before(times, phase) > 0;
    }
    return keys_before(times, phase) > keys_before(times, prev);
}

struct FootWalk {
    const std::vector<RigNode>& nodes;
    const FighterRig& rig;
    const Fighter& now;
    bool changed = false; // motion_changed
    bool skid = false;    // a skid's footstep update
    int foot = 0;         // Actor.foot_count
    std::uint32_t steps = 0;
};

void walk_node(FootWalk& w, int index, const FxClip* clip);

void walk_children(FootWalk& w, const RigNode& node, std::size_t from, const FxClip* clip) {
    for (std::size_t c = from; c < node.children.size(); ++c) {
        walk_node(w, node.children[c], clip);
    }
}

// motion_select_descriptor: the next foot, stepped when one of its track's three marker streams
// crossed a key (a track without the 0x3800 group gives none, not even a skid's)
void foot(FootWalk& w, const RigNode& node, const FxClip* clip) {
    const int foot = w.foot++;
    bool stepped = false;
    if (node.track >= 0) {
        const FxTrack* track = fx_track(clip, node.track);
        if (!track || !(track->flags & track_steps)) {
            return;
        }
        for (const auto& times : track->markers) {
            stepped = stream_stepped(times, w.now.motion_phase_prev, w.now.motion_phase,
                                     w.changed) ||
                      stepped;
        }
    }
    if ((stepped || w.skid) && foot < 32) {
        w.steps |= 1u << foot;
    }
}

// sv_ik_chain's order (pose_walk.cpp's walk_chain): the root's other children, then the
// middle link's, the end's and the tip's
// decomp: sv_ik_chain 0xAA8CB
void walk_chain(FootWalk& w, const RigNode& node, const FxClip* clip) {
    if (node.children.empty() || node.children[0] < 0) {
        return;
    }
    const RigNode& mid = w.nodes[static_cast<std::size_t>(node.children[0])];
    if (mid.children.empty() || mid.children[0] < 0) {
        return;
    }
    const RigNode& end = w.nodes[static_cast<std::size_t>(mid.children[0])];
    const bool short_chain = end.type == node_short_chain_end;
    const RigNode* tip = &end;
    if (!short_chain) {
        if (end.children.empty() || end.children[0] < 0) {
            return;
        }
        tip = &w.nodes[static_cast<std::size_t>(end.children[0])];
    }
    walk_children(w, node, 1, clip);
    walk_children(w, mid, 1, clip);
    walk_children(w, end, short_chain ? 0 : 1, clip);
    if (!short_chain) {
        walk_children(w, *tip, 1, clip);
    }
}

// sv_pose_walk's order (pose_walk.cpp's walk_node), so foot k is the walk's attachment
// 14 + k
// decomp: sv_pose_walk 0xAA419
void walk_node(FootWalk& w, int index, const FxClip* clip) {
    if (!clip || index < 0 || index >= static_cast<int>(w.nodes.size())) {
        return;
    }
    const RigNode& node = w.nodes[static_cast<std::size_t>(index)];
    if (node.flags & node_foot) {
        foot(w, node, clip);
        return;
    }
    if (node.type == node_chain) {
        walk_chain(w, node, clip);
        return;
    }
    // the weapon switch: arm A's graph, then arm B's, each with its own clip
    int part = 0;
    for (int child : node.children) {
        const FxClip* child_clip = clip;
        if (node.type == node_weapon_switch) {
            const int which = part == 1 ? 1 : 0;
            part = which + 1;
            child = w.rig.arm_graph[which];
            child_clip = fx_motion_slot(w.rig.arm_motion[which], w.now.motion_slot_a);
        }
        walk_node(w, child, child_clip);
    }
}

} // namespace

std::uint32_t fighter_footsteps(const World& world, const Fighter& before, const Fighter& now) {
    if (!world.battle || !now.present || !now.grounded || now.motion_state == state_dead ||
        now.motion_phase == now.motion_phase_prev) {
        return 0;
    }
    const BattleData& data = *world.battle;
    const FighterRig* rig = rig_of_build(data, now.build);
    const FxClip* clip = rig ? fx_motion_slot(rig->leg_motion, now.motion_slot_a) : nullptr;
    if (!clip || rig->leg_graph < 0) {
        return 0;
    }
    FootWalk w{data.rig_nodes, *rig, now};
    // motion_changed: motion_set_animation or actor_land this step
    w.changed = now.motion_slot_a != before.motion_slot_a ||
                now.motion_state != before.motion_state || now.landed;
    w.skid = now.skidding && now.age_frames % skid_step_every == 0;
    walk_node(w, rig->leg_graph, clip);
    return w.steps;
}
