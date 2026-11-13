#include "mech/mech_parts.hpp"

#include "render/mesh_flags.hpp"
#include "render/mesh_queue.hpp"

#include <cmath>
#include <cstdio>

namespace {

// a model's node tree from a mount, translate then z, y, x turns
// (no scale), one matrix per node
// decomp: weapon_model_nodes_place 0x79321
void place_nodes(const ModelObject& object, int index, MatrixStack& stack, std::vector<Mat4>& out) {
    if (index < 0 || index >= static_cast<int>(object.nodes.size())) {
        return;
    }
    // the tree as child and sibling links, rebuilt from parents
    const ModelNode& node = object.nodes[index];
    int sibling = -1;
    for (int n = index + 1; n < static_cast<int>(object.nodes.size()); ++n) {
        if (object.nodes[n].parent == node.parent) {
            sibling = n;
            break;
        }
    }
    if (sibling >= 0) {
        stack_push(stack);
        place_nodes(object, sibling, stack, out);
        stack_pop(stack);
    }
    stack_translate(stack, node.translate[0], node.translate[1], node.translate[2]);
    stack_rotate_z(stack, node.euler[2]);
    stack_rotate_y(stack, node.euler[1]);
    stack_rotate_x(stack, node.euler[0]);
    out[index] = stack_top(stack);
    for (int n = index + 1; n < static_cast<int>(object.nodes.size()); ++n) {
        if (object.nodes[n].parent == index) {
            stack_push(stack);
            place_nodes(object, n, stack, out);
            stack_pop(stack);
            break;
        }
    }
}

// a part's sections: each model node's sections at (its local matrix, baked into the batch by
// part_batch_build) then its bone
void part_items(const MechParts& mech, MechPart part, const std::vector<Mat4>& bones,
                const std::vector<bool>& has_bone, const float eye[3], std::vector<PassItem>& out) {
    const ModelObject& object = mech.models[part].objects[0];
    for (std::size_t n = 0; n < object.nodes.size(); ++n) {
        const ModelNode& node = object.nodes[n];
        if (node.section_count <= 0 || n >= bones.size() || !has_bone[n]) {
            continue;
        }
        const Mat4 world = mat_multiply(node_local_matrix(node), bones[n]);
        const Vec3f at = mat_position(world);
        for (int s = 0; s < node.section_count; ++s) {
            const int section = node.first_section + s;
            if (section < 0 || section >= static_cast<int>(object.sections.size())) {
                continue;
            }
            PassItem item;
            item.pass = 4;
            item.distance =
                std::sqrt((at.x - eye[0]) * (at.x - eye[0]) + (at.y - eye[1]) * (at.y - eye[1]) +
                          (at.z - eye[2]) * (at.z - eye[2]));
            item.item = section_draw(object.sections[section], mech.gpu[part].object_mesh[0],
                                     mech.paint, world, 4, 0);
            out.push_back(item);
        }
    }
}

} // namespace

void mech_sticker_items(const MechParts& mech, const MechPose& pose, int pass,
                        std::uint32_t flags, std::uint32_t factor, std::vector<PassItem>& out) {
    if (mech.sticker_model.objects.empty() || mech.sticker_gpu.object_mesh.empty() ||
        !pose.mounted[4]) {
        return;
    }
    // model_render_submit at body_matrix (matrix_set_top), the colour as the texture factor
    const std::size_t first = out.size();
    queue_object(mech.sticker_model.objects[0], mech.sticker_gpu.object_mesh[0], 0, pose.mount[4],
                 pass, flags | mesh_factor, mat_identity(), out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].item.paint = mech.sticker_texture;
        out[i].item.factor = factor;
    }
}

MechPoseInput mech_pose_input(const MechParts& mech, int slot, float frame, const Mat4& world) {
    MechPoseInput input;
    input.tables = mech.tables;
    input.graph_root = mech.leg_graph;
    input.arm_a_root = mech.arm_a_graph;
    input.arm_b_root = mech.arm_b_graph;
    input.clip = motion_slot(mech.leg_motion, slot);
    input.clip_a = motion_slot(mech.arm_a_motion, slot);
    input.clip_b = motion_slot(mech.arm_b_motion, slot);
    for (int i = 0; i < 7; ++i) {
        input.body_clips[i] = motion_slot(mech.body_motion, i);
    }
    input.frame = frame;
    input.body_class = mech.body_class;
    input.leg_class = mech.leg_class;
    input.world = world;
    return input;
}

void mech_draw_items(const MechParts& mech, const MechPose& pose, const float eye[3],
                     std::vector<PassItem>& out, bool body) {
    // body and legs: palette entry b for the b-th model node with sections, body first
    int bone = 0;
    for (MechPart part : {part_body, part_legs}) {
        if (!mech.loaded[part] || !body) {
            continue;
        }
        const ModelObject& object = mech.models[part].objects[0];
        std::vector<Mat4> bones(object.nodes.size());
        std::vector<bool> has_bone(object.nodes.size(), false);
        for (std::size_t n = 0; n < object.nodes.size(); ++n) {
            if (object.nodes[n].section_count > 0) {
                if (bone < 32 && pose.stored[0][bone]) {
                    bones[n] = pose.matrices[0][bone];
                    has_bone[n] = true;
                }
                ++bone;
            }
        }
        part_items(mech, part, bones, has_bone, eye, out);
    }
    // arms from their mounts (flags 0x40, 0x80), back weapons from the body matrix
    const struct {
        MechPart part;
        int mount;
    } placed[4] = {{part_arm_a, 2}, {part_arm_b, 3}, {part_back_a, 4}, {part_back_b, 4}};
    for (int weapon = 0; weapon < 4; ++weapon) {
        const auto& [part, mount] = placed[weapon];
        if (!mech.loaded[part]) {
            continue;
        }
        const ModelObject& object = mech.models[part].objects[0];
        // the world's nodes, as the step placed them
        const int count = pose.weapon_node_count[weapon];
        if (count > 0) {
            std::vector<Mat4> bones(object.nodes.size());
            std::vector<bool> placed_node(object.nodes.size(), false);
            for (int n = 0; n < count && n < static_cast<int>(object.nodes.size()); ++n) {
                bones[n] = pose.weapon_nodes[weapon][n];
                placed_node[n] = true;
            }
            part_items(mech, part, bones, placed_node, eye, out);
            continue;
        }
        if (!pose.mounted[mount]) {
            continue;
        }
        std::vector<Mat4> bones(object.nodes.size());
        MatrixStack stack;
        stack_set(stack, pose.mount[mount]);
        if (!object.nodes.empty()) {
            place_nodes(object, 0, stack, bones);
        }
        part_items(mech, part, bones, std::vector<bool>(object.nodes.size(), true), eye, out);
    }
}
