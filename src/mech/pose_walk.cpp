// actor_pose_build in Q16.16: the SV articulation graph walked as sv_pose_walk
// walks it, with the IK chains of sv_ik_chain, the arms' raise toward the reticle
// (sv_pose_weapon_node), the pose blend and the captures of sv_collision_spheres_select.
// The float walk in mech_pose.cpp is the same code in floats (the free camera's fighter
// and pose_compare.cpp's reference). Every matrix is rebuilt each step from the curves and the
// fighter's angles; only the blend reads the last step's palette, as retail does.
#include "mech/fighter_pose.hpp"

#include "battle/battle_data.hpp"
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"
#include "mech/pose_math.hpp"

namespace {

// the pitch the upper body follows, held within 0.4 rad (sv_pose_walk)
constexpr Angle pitch_hold = angle_f_radians(0.4);
// the arms' 0x40/0x80 nodes pitch toward the reticle within 0.8 rad (pose_node_aim_at)
constexpr Angle arm_pitch_hold = angle_f_radians(0.8);
// a grounded foot's target pulled 0.3 up off the stage (sv_ik_chain)
constexpr Fx foot_lift = fx_f(0.3);
// the arms' aim clips: motion_clip_table_c + 0x54 (arm A) and motion_clip_table_b + 0x58 (arm B)
constexpr int arm_aim_slot[2] = {21, 22};

struct Walk {
    const PoseInput& in;
    FighterPoseState& pose;
    const std::vector<RigNode>& nodes;
    FxMat4 world;
    int part = 0;
    // D_009A6E30, D_009A6E38: the root track's x and z taken out of the pose, put back on the
    // IK targets
    Fx root_offset_x, root_offset_z;
};

void sample(const RigNode& node, const FxTrack* track, Fx frame, FxVec3& t, Angle r[3]) {
    if (track && track->flags & track_rotate) {
        for (int k = 0; k < 3; ++k) {
            r[k] = angle_from_radians(fx_curve_value(track->rotate[k], frame));
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            r[k] = node.rotate[k];
        }
    }
    if (track && track->flags & track_translate) {
        t = {fx_curve_value(track->translate[0], frame), fx_curve_value(track->translate[1], frame),
             fx_curve_value(track->translate[2], frame)};
    } else {
        t = node.translate;
    }
}

bool valid_matrix(const RigNode& node) {
    return node.matrix >= 0 && node.matrix < pose_matrices;
}

// decomp: sv_matrix_store_pass 0xAC448
void store(Walk& w, const RigNode& node, const FxMat4& m) {
    if (!valid_matrix(node)) {
        return;
    }
    w.pose.palette[w.part][node.matrix] = m;
    w.pose.stored[w.part] |= 1u << node.matrix;
    w.pose.stores[w.part] += 1;
}

// sv_collision_spheres_select's blend: toward the last step's matrix of the node, moved by how
// far the first node moved in x and z, by the blend weight
FxMat4 blended(Walk& w, const RigNode& node, const FxMat4& top) {
    if (w.in.blend <= Fx{} || !(w.pose.stored[w.part] & (1u << node.matrix))) {
        return top;
    }
    FxMat4 last = w.pose.palette[w.part][node.matrix];
    if (w.pose.stores[0] == 0) {
        w.pose.blend_offset_x = top.w.x - last.w.x;
        w.pose.blend_offset_z = top.w.z - last.w.z;
    }
    last.w.x += w.pose.blend_offset_x;
    last.w.z += w.pose.blend_offset_z;
    return mat4_blend(top, last, w.in.blend);
}

// decomp: actor_node_capture_matrices 0xACAB6, attachment_bind_matrices 0xACC91
void capture(Walk& w, const RigNode& node, const FxMat4& m) {
    const std::uint32_t flags = node.flags;
    const int slot = flags & node_aim_b   ? mount_aim_b
                     : flags & node_aim_a ? mount_aim_a
                     : flags & node_body  ? mount_body
                     : flags & node_arm_a ? mount_arm_a
                     : flags & node_arm_b ? mount_arm_b
                                          : -1;
    if (slot >= 0) {
        w.pose.mount[slot] = m;
        w.pose.mounted[slot] = true;
    }
    if (slot == mount_body) {
        w.pose.head_position = m.w;
    }
    if (flags & node_flag8) {
        w.pose.mount[mount_flag8] = m;
        w.pose.mounted[mount_flag8] = true;
    }
    for (int k = 0; k < 14; ++k) {
        if (flags & (node_attachment_0 << k)) {
            w.pose.attachments[k].part = w.part;
            w.pose.attachments[k].matrix = node.matrix;
        }
    }
}

// a footstep node's point under its parent, attachment 14 on.
// (Its footstep events, the 0x3800 key crossings, are presentation's: view/footsteps.cpp walks
// the graph in this order to number the feet the same)
// decomp: motion_select_descriptor 0xAD6B7
void foot(Walk& w, const RigNode& node, const FxMat4& parent) {
    const int slot = 14 + w.pose.foot_count;
    w.pose.foot_count += 1;
    if (slot >= attachment_slots) {
        return;
    }
    FighterAttachment& at = w.pose.attachments[slot];
    at.previous = at.position;
    at.position = transform_coord(node.translate, parent);
    at.kind = 4;
}

// an arm node posed from the aim clip's first frame under its
// parent, its mounts turned toward the reticle (pose_node_aim_at), and the walk's matrix
// blended toward that by the raise
// decomp: sv_pose_weapon_node 0xACD24
FxMat4 arm_raised(Walk& w, const RigNode& node, const FxMat4& parent, const FxMat4& walked,
                  int arm) {
    const PoseInput& in = w.in;
    const FxClip* aim_clip = fx_motion_slot(in.rig->arm_motion[arm], arm_aim_slot[arm]);
    FxVec3 t;
    Angle r[3];
    // retail tests the playing clip's track flags and reads the aim clip's curves by them; the
    // port goes by the aim clip's own flags (docs/bugs.md)
    sample(node, fx_track(aim_clip, node.track), Fx{}, t, r);
    if (node.flags & node_aims && in.rig->body_class == 1) {
        r[0] += angle_quarter;
    }
    FxMat4 aimed = mat4_mul(mat4_turns(t, r[0], r[1], r[2]), parent);
    if (node.flags & node_aimed) {
        const FxVec3 to_point = in.aim_point[arm] - aimed.w;
        if (node.flags & node_aims) {
            // z from the reticle toward the node, up the fighter's (0, 1, 0): mirrored, z toward it
            aimed = mat4_from(mat3_from_dir_up_mirrored(-to_point, {Fx{}, fx(1), Fx{}}), aimed.w);
        } else {
            // a pitch toward it in the fighter's frame: -atan2(y, z) of node - reticle, within
            // 0.8; mirrored, atan2(y, -z)
            const FxVec3 local = transform(-to_point, mat3_transpose(mat3_rotation_y(in.heading)));
            const Angle pitch = angle_clamp(angle_atan2(local.y, -local.z), arm_pitch_hold);
            aimed = mat4_mul(mat4_from(mat3_rotation_x(pitch), {}), aimed);
        }
    }
    return mat4_blend(walked, aimed, in.arm_raise[arm]);
}

void walk_node(Walk& w, int index, const FxClip* clip, int arm, const FxMat4& parent);

void walk_children(Walk& w, const RigNode& node, std::size_t from, const FxClip* clip,
                   const FxMat4& frame) {
    for (std::size_t c = from; c < node.children.size(); ++c) {
        walk_node(w, node.children[c], clip, -1, frame);
    }
}

// decomp: sv_ik_chain 0xAA8CB
void walk_chain(Walk& w, const RigNode& node, const FxClip* clip, const FxMat4& parent) {
    const std::vector<RigNode>& nodes = w.nodes;
    if (node.children.empty() || node.children[0] < 0) {
        return;
    }
    const RigNode& mid = nodes[node.children[0]];
    if (mid.children.empty() || mid.children[0] < 0) {
        return;
    }
    const RigNode& end = nodes[mid.children[0]];
    const bool short_chain = end.type == 4;
    const RigNode* tip = &end;
    if (!short_chain) {
        if (end.children.empty() || end.children[0] < 0) {
            return;
        }
        tip = &nodes[end.children[0]];
    }

    // the chain root
    FxVec3 t;
    Angle r[3];
    sample(node, node.track >= 0 ? fx_track(clip, node.track) : nullptr, w.in.frame, t, r);
    if (node.track >= 0 && node.flags == node_root) {
        w.root_offset_z = t.z; // as sampled, sv_pose_walk negates it (no chain root has flags 2)
        t.z = Fx{};
    }
    const FxMat4 top = mat4_mul(mat4_turns(t, r[0], r[1], r[2]), parent);

    // the lengths are the authored link translations
    const Fx length_a = length(end.translate);
    const Fx length_b = length(tip->translate);

    // the target: the tip track's translation with the root's travel put back, in the world.
    // Retail reads the track with no rotation output, so a tip track that also turns would give
    // its rotation curves; the port reads the translation (none in the data: pose_compare.cpp).
    // With no translation retail's target is uninitialized; the port holds the chain straight
    FxVec3 target = transform_coord({length_a + length_b, Fx{}, Fx{}}, top);
    const FxTrack* tip_track = fx_track(clip, tip->track);
    if (tip_track && tip_track->flags & track_translate) {
        FxVec3 at{fx_curve_value(tip_track->translate[0], w.in.frame),
                  fx_curve_value(tip_track->translate[1], w.in.frame),
                  fx_curve_value(tip_track->translate[2], w.in.frame)};
        at.x += w.root_offset_x;
        at.z += w.root_offset_z;
        target = transform_coord(at, w.world);
    }
    // a grounded fighter's leg target pulled onto the stage along the segment from the hip (the
    // chain root's place; retail mirrors the hip's z there, docs/bugs.md), 0.3 above the hit
    if (!short_chain && w.in.grounded && w.in.data && w.in.data->stage.loaded) {
        const FxVec3 hip = transform_coord(node.translate, parent);
        FxVec3 hit;
        if (collision_segment_hit(w.in.data->stage, hip, target, &hit, false)) {
            target = hit + FxVec3{Fx{}, foot_lift, Fx{}};
        }
    }

    FxMat4 joint, end_matrix, tip_matrix;
    if (short_chain) {
        ik_solve_two(target, top, length_a, joint, end_matrix);
    } else {
        ik_solve_three(target, top, length_a, length_b, end.twist_positive, joint, end_matrix,
                       tip_matrix);
    }

    // the root's other children, each link stored and its other children under it
    walk_children(w, node, 1, clip, top);
    store(w, mid, joint);
    walk_children(w, mid, 1, clip, joint);
    store(w, end, end_matrix);
    walk_children(w, end, short_chain ? 0 : 1, clip, end_matrix);
    if (!short_chain) {
        store(w, *tip, tip_matrix);
        walk_children(w, *tip, 1, clip, tip_matrix);
    }
}

// sv_pose_walk; arm is the raised arm's graph being walked (0 A, 1 B), -1 none
// decomp: sv_pose_walk 0xAA419
void walk_node(Walk& w, int index, const FxClip* clip, int arm, const FxMat4& parent) {
    if (!clip || index < 0 || index >= static_cast<int>(w.nodes.size())) {
        return;
    }
    const RigNode& node = w.nodes[index];
    const PoseInput& in = w.in;
    if (node.flags & node_foot) {
        foot(w, node, parent);
        return;
    }
    if (node.type == 2) {
        walk_chain(w, node, clip, parent);
        return;
    }
    FxVec3 t;
    Angle r[3];
    if (node.track >= 0) {
        sample(node, fx_track(clip, node.track), in.frame, t, r);
        if (node.flags == node_root) {
            // the root track's sideways and forward travel come out of the pose (the legs' IK
            // targets get it back), sideways only outside walking and running
            const bool walking = state_in(in.motion_state, {state_walk, state_walk_back,
                                                             state_run, state_run_reverse});
            w.root_offset_x = walking ? Fx{} : -t.x;
            if (!walking) {
                t.x = Fx{};
            }
            w.root_offset_z = -t.z;
            t.z = Fx{};
        }
        if (node.flags & node_body) {
            const int leg = in.rig->leg_class;
            r[1] += in.aim_yaw;
            const bool wheel_leg = leg == 2 || leg == 3 || leg == 4 || leg == 8 || leg == 9;
            if (in.motion_state != state_dead && (in.motion_state != state_run || !wheel_leg)) {
                r[2] += angle_from_radians(in.turn_rate);
            }
        }
    } else if (node.flags & node_body_set) {
        // sv_pose_flagged_node_sample: the body set's slot by flag, its track 0, at
        // that slot's phase
        const std::uint32_t f = node.flags;
        const int slot = f & node_body_set_0   ? 0
                         : f & node_body_set_1 ? 1
                         : f & node_body_set_3 ? 3
                         : f & node_body_set_4 ? 4
                         : f & node_body_set_5 ? 5
                                               : 6;
        const Fx frame = in.node_frame[slot == 0 ? 0 : slot == 1 ? 1 : 2];
        sample(node, fx_track(fx_motion_slot(in.rig->body_motion, slot), 0), frame, t, r);
    } else {
        t = node.translate;
        for (int k = 0; k < 3; ++k) {
            r[k] = node.rotate[k];
        }
    }
    if (node.flags & node_wheel) {
        r[0] -= in.wheel_angle;
    } else if (node.flags & node_pitch) {
        r[0] += angle_clamp(in.pitch, pitch_hold);
    } else if (node.flags & node_aims && in.rig->body_class == 1) {
        r[0] += angle_quarter; // -pi/2 in the original's frame
    }
    FxMat4 m = mat4_mul(mat4_turns(t, r[0], r[1], r[2]), parent);
    if (arm >= 0 && node.track >= 0) {
        m = arm_raised(w, node, parent, m, arm);
    }
    if (valid_matrix(node)) {
        m = blended(w, node, m);
        capture(w, node, m);
        store(w, node, m);
    }
    for (std::size_t c = 0; c < node.children.size(); ++c) {
        int child = node.children[c];
        const FxClip* child_clip = clip;
        int child_arm = arm;
        if (node.type == 7) {
            // the weapon switch: arm A's graph, then arm B's, each with its own clip, raised when
            // its weapon is
            const int which = w.part == 1 ? 1 : 0;
            w.part = which + 1;
            child = in.rig->arm_graph[which];
            child_clip = fx_motion_slot(in.rig->arm_motion[which], in.slot);
            child_arm = in.arm_raise[which] > Fx{} ? which : -1;
        }
        walk_node(w, child, child_clip, child_arm, m);
    }
    if (node.type == 7) {
        w.part = 0;
    }
}

} // namespace

// decomp: actor_pose_build 0xAA2AF
void pose_walk(const PoseInput& in, FighterPoseState& pose) {
    pose.foot_count = 0;
    for (int part = 0; part < pose_parts; ++part) {
        pose.stores[part] = 0;
    }
    if (!in.rig || !in.data || in.rig->leg_graph < 0) {
        return;
    }
    // actor_pose_build: translate by the position, then turn by the heading
    Walk w{in, pose, in.data->rig_nodes, mat4_from(mat3_rotation_y(in.heading), in.position)};
    walk_node(w, in.rig->leg_graph, fx_motion_slot(in.rig->leg_motion, in.slot), -1, w.world);
}
