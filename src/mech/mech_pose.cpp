#include "mech/mech_pose.hpp"

namespace {

constexpr float half_pi = 1.5707964f;

struct Walk {
    const MechPoseInput& in;
    MechPose& pose;
    const std::vector<SvGraphNode>& nodes;
    MatrixStack stack;
    int pose_part = 0;
    float root_offset_x =
        0; // D_009A6E30: the root track's x taken out of the pose, put on the IK targets
    float root_offset_z = 0; // D_009A6E38
};

const MotionTrack* track_of(const MotionClip* clip, int track) {
    return clip && track >= 0 && track < static_cast<int>(clip->size()) ? &(*clip)[track] : nullptr;
}

// a track's rotation and translation at the frame, the node's
// static values where the track has none (both already in the port frame)
// decomp: sv_articulation_sample 0xAE640
void sample(const SvGraphNode& node, const MotionTrack* track, float frame, Vec3f& translate,
            Vec3f& rotate) {
    if (track && track->flags & track_rotate) {
        rotate = {motion_curve_value(track->rotate[0], frame),
                  motion_curve_value(track->rotate[1], frame),
                  motion_curve_value(track->rotate[2], frame)};
    } else {
        rotate = {node.rotate[0], node.rotate[1], node.rotate[2]};
    }
    if (track && track->flags & track_translate) {
        translate = {motion_curve_value(track->translate[0], frame),
                     motion_curve_value(track->translate[1], frame),
                     motion_curve_value(track->translate[2], frame)};
    } else {
        translate = {node.translate[0], node.translate[1], node.translate[2]};
    }
}

// sv_collision_spheres_select without the pose blend (0 at rest) and the spheres, then
// actor_node_capture_matrices and sv_matrix_store_pass
// decomp: sv_collision_spheres_select 0xAC6D7
void store(Walk& walk, const SvGraphNode& node, const Mat4& matrix) {
    if (node.matrix < 0 || node.matrix >= 32) {
        return;
    }
    walk.pose.matrices[walk.pose_part][node.matrix] = matrix;
    walk.pose.stored[walk.pose_part][node.matrix] = true;
}

void capture(Walk& walk, const SvGraphNode& node, const Mat4& matrix) {
    const std::uint32_t flags = node.flags;
    const int slot = flags & node_aim_b   ? 1
                     : flags & node_aim_a ? 0
                     : flags & node_body  ? 4
                     : flags & node_arm_a ? 2
                     : flags & node_arm_b ? 3
                                          : -1;
    if (slot >= 0) {
        walk.pose.mount[slot] = matrix;
        walk.pose.mounted[slot] = true;
    }
}

void walk_node(Walk& walk, int index, const MotionClip* clip, int mode);

// decomp: sv_ik_chain 0xAA8CB
void walk_chain(Walk& walk, const SvGraphNode& node, const MotionClip* clip) {
    const std::vector<SvGraphNode>& nodes = walk.nodes;
    if (node.children.empty() || node.children[0] < 0) {
        return;
    }
    const SvGraphNode& mid = nodes[node.children[0]];
    if (mid.children.empty() || mid.children[0] < 0) {
        return;
    }
    const SvGraphNode& end = nodes[mid.children[0]];
    const bool short_chain = end.type == 4;
    const SvGraphNode* tip = &end;
    if (!short_chain) {
        if (end.children.empty() || end.children[0] < 0) {
            return;
        }
        tip = &nodes[end.children[0]];
    }

    // the chain root's own transform
    stack_push(walk.stack);
    Vec3f translate, rotate;
    sample(node, track_of(clip, node.track), walk.in.frame, translate, rotate);
    if (node.track >= 0 && node.flags == node_root) {
        walk.root_offset_z = translate.z; // as read: stored without the walk's negation
        translate.z = 0;
    }
    stack_translate(walk.stack, translate.x, translate.y, translate.z);
    stack_rotate_z(walk.stack, rotate.z);
    stack_rotate_y(walk.stack, rotate.y);
    stack_rotate_x(walk.stack, rotate.x);
    const Mat4 top = stack_top(walk.stack);

    // the target: the tip track's translation, the root motion put back, in world space
    Vec3f target{};
    const MotionTrack* tip_track = track_of(clip, tip->track);
    if (tip_track && tip_track->flags & track_translate) {
        if (tip_track->flags & track_rotate) {
            ++walk.pose.ik_tip_rotations;
        }
        target = {motion_curve_value(tip_track->translate[0], walk.in.frame),
                  motion_curve_value(tip_track->translate[1], walk.in.frame),
                  motion_curve_value(tip_track->translate[2], walk.in.frame)};
        target.x += walk.root_offset_x;
        target.z += walk.root_offset_z;
        target = transform_coord(target, walk.in.world);
    }
    // (retail then pulls a grounded fighter's target up onto the stage along a segment from the
    // chain root; there is no stage collision here yet)

    // the links' lengths are their authored translations
    const Vec3f end_offset{end.translate[0], end.translate[1], end.translate[2]};
    const Vec3f tip_offset{tip->translate[0], tip->translate[1], tip->translate[2]};
    Mat4 joint, end_matrix, tip_matrix;
    if (short_chain) {
        ik_solve_two(target, top, vec_length(end_offset), joint, end_matrix);
    } else {
        ik_solve_three(target, top, vec_length(end_offset), vec_length(tip_offset), end.rotate[2],
                       joint, end_matrix, tip_matrix);
    }

    // the root's other children under the root, each link's other children under that link
    auto walk_children = [&](const SvGraphNode& parent, std::size_t from, const Mat4& frame,
                             int mode) {
        stack_push(walk.stack);
        stack_set(walk.stack, frame);
        for (std::size_t c = from; c < parent.children.size(); ++c) {
            if (parent.children[c] >= 0) {
                walk_node(walk, parent.children[c], clip, mode);
            }
        }
        stack_pop(walk.stack);
    };
    walk_children(node, 1, top, 0);
    store(walk, mid, joint);
    walk_children(mid, 1, joint, 0);
    store(walk, end, end_matrix);
    walk_children(end, short_chain ? 0 : 1, end_matrix, 0);
    if (!short_chain) {
        store(walk, *tip, tip_matrix);
        walk_children(*tip, 1, tip_matrix, 0);
    }
    stack_pop(walk.stack);
}

// decomp: sv_pose_walk 0xAA419
void walk_node(Walk& walk, int index, const MotionClip* clip, int mode) {
    if (!clip || index < 0) {
        return;
    }
    const SvGraphNode& node = walk.nodes[index];
    if (node.flags & node_foot) {
        return; // motion_select_descriptor: footstep markers, no matrix
    }
    if (node.type == 2) {
        walk_chain(walk, node, clip);
        return;
    }
    const MechPoseInput& in = walk.in;
    stack_push(walk.stack);
    Vec3f translate{}, rotate{};
    if (node.track >= 0) {
        sample(node, track_of(clip, node.track), in.frame, translate, rotate);
        if (node.flags == node_root) {
            // the root track's sideways and forward travel come out of the pose (the legs' IK
            // targets get it back), sideways only outside walking and running
            const bool walking = in.motion_state == 0 || in.motion_state == 1 ||
                                 in.motion_state == 2 || in.motion_state == 3;
            walk.root_offset_x = walking ? 0.0f : -translate.x;
            if (!walking) {
                translate.x = 0;
            }
            walk.root_offset_z = -translate.z;
            translate.z = 0;
        }
        if (node.flags & node_body) {
            const int leg = in.leg_class;
            rotate.y -= in.aim_yaw; // +aim_yaw in the original's frame
            const bool wheel_leg = leg == 2 || leg == 3 || leg == 4 || leg == 8 || leg == 9;
            if (in.motion_state != 10 && (in.motion_state != 2 || !wheel_leg)) {
                rotate.z += in.turn_rate;
            }
        }
    } else if (node.flags & node_body_set) {
        // sv_pose_flagged_node_sample: the body set's slot by flag, its first track
        const int slot = node.flags & node_body_set_0   ? 0
                         : node.flags & node_body_set_1 ? 1
                         : node.flags & node_body_set_3 ? 3
                         : node.flags & node_body_set_4 ? 4
                         : node.flags & node_body_set_5 ? 5
                                                        : 6;
        const MotionClip* body = in.body_clips[slot];
        sample(node, track_of(body, 0), 0.0f, translate, rotate);
    } else {
        translate = {node.translate[0], node.translate[1], node.translate[2]};
        rotate = {node.rotate[0], node.rotate[1], node.rotate[2]};
    }
    if (node.flags & node_wheel) {
        rotate.x -= in.wheel_angle;
    } else if (node.flags & node_pitch) {
        const float pitch = in.pitch < -0.4f ? -0.4f : in.pitch > 0.4f ? 0.4f : in.pitch;
        rotate.x -= pitch;
    } else if (node.flags & node_aims && in.body_class == 1) {
        rotate.x += half_pi; // -pi/2 in the original's frame
    }
    stack_translate(walk.stack, translate.x, translate.y, translate.z);
    if (rotate.z != 0.0f) {
        stack_rotate_z(walk.stack, rotate.z);
    }
    if (rotate.y != 0.0f) {
        stack_rotate_y(walk.stack, rotate.y);
    }
    if (rotate.x != 0.0f) {
        stack_rotate_x(walk.stack, rotate.x);
    }
    if (node.matrix >= 0) {
        capture(walk, node, stack_top(walk.stack));
        store(walk, node, stack_top(walk.stack));
    }
    if (mode == 1) {
        mode = 2;
    }
    for (std::size_t c = 0; c < node.children.size(); ++c) {
        int child = node.children[c];
        const MotionClip* child_clip = clip;
        if (node.type == 7) {
            // the weapon switch: arm A's graph, then arm B's, each with its own clip
            if (walk.pose_part == 1) {
                walk.pose_part = 2;
                child = in.arm_b_root;
                child_clip = in.clip_b;
            } else {
                walk.pose_part = 1;
                child = in.arm_a_root;
                child_clip = in.clip_a;
            }
        }
        if (child >= 0) {
            walk_node(walk, child, child_clip, mode);
        }
    }
    if (node.type == 7) {
        walk.pose_part = 0;
    }
    stack_pop(walk.stack);
}

} // namespace

void build_mech_pose(const MechPoseInput& input, MechPose& pose) {
    pose = MechPose{};
    if (!input.tables || input.graph_root < 0) {
        return;
    }
    Walk walk{input, pose, input.tables->nodes, {}, 0, 0, 0};
    stack_set(walk.stack, input.world);
    walk_node(walk, input.graph_root, input.clip, 0);
}
