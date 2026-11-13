// The rest of the pose in the fighter's step (actor_step after sv_contact_route): the
// walk (pose_walk.cpp), the back weapons aimed and spun (actor_back_weapons_place), the
// arms' models set from the walk (actor_arm_weapons_place, model_flagged_nodes_set),
// the wheel spin (actor_wheel_spin_update) and the attachment points
// (actor_attachments_update, run by actor_effects_update after the step). Also the body
// set's phases (actor_phases_update), which the motion step runs.
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"
#include "mech/pose_math.hpp"
#include "weapons/weapons.hpp"

namespace {

// actor_pose_reset: pose_blend set to 1 drops 0.1 an update before each pose (0.9 the
// first). R7: two steps each keep sqrt of the update's share of the last pose, so a pair keeps
// what the update kept (sixty.md "Pose blend")
constexpr Fx blend_weight[10] = {fx_f(0.9486832980505138),  fx_f(0.8944271909999159),
                                 fx_f(0.8366600265340756),  fx_f(0.7745966692414834),
                                 fx_f(0.7071067811865476),  fx_f(0.6324555320336759),
                                 fx_f(0.5477225575051661),  fx_f(0.4472135954999579),
                                 fx_f(0.31622776601683794), Fx{}};

// actor_wheel_spin_update: under 0.01 an update the stick's 0.2 x |turn| is added; R1: 0.005 and
// 0.1 a step. The spin is -distance / pi, held within 3.1677728 an update (R1: half a step)
constexpr Fx wheel_still = fx_f(0.005);
constexpr Fx wheel_stick = fx_f(0.1);
constexpr Fx wheel_inverse_pi = fx_f(1.0 / 3.14159265358979);
constexpr Fx wheel_most = fx_f(3.1677728 / 2.0);

// the phases' limits, ticks (actor_phase_*_update)
constexpr Fx phase_hold = fx(44);
constexpr Fx phase_run_back = fx(30);
constexpr Fx phase_turn = fx(14);
constexpr Fx phase_end = fx(59);

// a weapon model's nodes under its mount (weapon_model_nodes_place: translate, then the z,
// y and x turns, no scale) from the node values the clip and the spin left (FighterPoseState).
// Nodes flagged 0x100 of an arm take the arm's frame (model_flagged_nodes_set writes its Euler
// angles and place, which rebuild exactly that frame)
// decomp: weapon_model_nodes_place 0x79321
int place_model(const std::vector<RigModelNode>& model, const FxVec3* translate,
                const Angle (*euler)[3], const FxMat4& mount, const FxMat4* flagged_frame,
                FxMat4 out[]) {
    const int count = static_cast<int>(model.size()) < weapon_model_nodes
                          ? static_cast<int>(model.size())
                          : weapon_model_nodes;
    for (int i = 0; i < count; ++i) {
        const RigModelNode& node = model[static_cast<std::size_t>(i)];
        FxMat4 local = mat4_turns(translate[i], euler[i][0], euler[i][1], euler[i][2]);
        if (node.flags & model_node_aimed && flagged_frame) {
            local = *flagged_frame;
        }
        const bool has_parent = node.parent >= 0 && node.parent < i;
        out[i] = mat4_mul(local, has_parent ? out[node.parent] : mount);
    }
    return count;
}

// weapon_anim_update on a mount's node values
void clip_update(Fighter& f, const FighterRig& rig, int mount) {
    const int count = static_cast<int>(rig.weapon_model[mount].size()) < weapon_model_nodes
                          ? static_cast<int>(rig.weapon_model[mount].size())
                          : weapon_model_nodes;
    weapon_anim_update(f.combat.weapons[mount], rig.blocks[mount], f.controller != -1,
                       f.pose.weapon_translate[mount], f.pose.weapon_euler[mount], count);
}

// actor_back_weapons_place: each back weapon fitted (or class 2's own pair) hangs from the body
// node; a fitted one plays its clip, is aimed at the reticle point and its flagged nodes pitched
// by how far the aim and the body each lean out of level, plus the weapon's elevation: the
// weapon's launch_pitch. On a model stand (battle_phase 5) neither: the nodes keep their pitch
void back_weapons_place(Fighter& f, const FighterRig& rig, bool stand) {
    FighterPoseState& pose = f.pose;
    for (int i = 0; i < 2; ++i) {
        const int mount = 2 + i;
        pose.weapon_node_count[mount] = 0;
        if ((!rig.weapon_fitted[mount] && rig.body_class != 2) || !pose.mounted[mount_body]) {
            continue;
        }
        const FxMat4& body = pose.mount[mount_body];
        int spin_node = 0;
        if (rig.weapon_fitted[mount] && !stand) {
            Weapon& weapon = f.combat.weapons[mount];
            const FxVec3 aim = (f.combat.mount_free_aim[mount] ? f.combat.reticle_track_world
                                                               : f.reticle_center_world) -
                               body.w;
            clip_update(f, rig, mount);
            pose.back_aim[i] = mat4_from(mat3_from_dir_up_mirrored(aim, body.y), {});
            // spread_a: the aim out of level (negative below); spread_b: the body's z axis out
            // of level, negative when the original's z dips (the port's z row is its negation)
            Angle spread_a = angle_between({aim.x, Fx{}, aim.z}, aim);
            if (aim.y < Fx{}) {
                spread_a = -spread_a;
            }
            const FxVec3 z = body.z;
            Angle spread_b = angle_between({z.x, Fx{}, z.z}, z);
            if (z.y > Fx{}) {
                spread_b = -spread_b;
            }
            pose.back_launch_pitch[i] = spread_a + spread_b + weapon.elevation;
            weapon.launch_pitch = pose.back_launch_pitch[i];
            // model_flagged_nodes_spin: the pitch into the flagged nodes' x turn, where it stays
            // (the port's x turns negated)
            for (std::size_t n = 0; n < rig.weapon_model[mount].size() &&
                                    n < static_cast<std::size_t>(weapon_model_nodes);
                 ++n) {
                if (rig.weapon_model[mount][n].flags & model_node_aimed) {
                    pose.weapon_euler[mount][n][0] = -pose.back_launch_pitch[i];
                    spin_node = static_cast<int>(n);
                }
            }
        }
        if (!rig.weapon_model[mount].empty()) {
            pose.weapon_node_count[mount] =
                place_model(rig.weapon_model[mount], pose.weapon_translate[mount],
                            pose.weapon_euler[mount], body, nullptr, pose.weapon_nodes[mount]);
        }
        pose.back_spin_node[i] = spin_node;
    }
}

// actor_arm_weapons_place: each arm plays its clip (not on a model stand) and its model hangs
// from its mount (flags 0x40, 0x80), its flagged nodes set to the walk's 0x10 or 0x20 node seen
// from that mount
void arm_weapons_place(Fighter& f, const FighterRig& rig, bool stand) {
    FighterPoseState& pose = f.pose;
    for (int i = 0; i < 2; ++i) {
        pose.weapon_node_count[i] = 0;
        const int hang = i == 0 ? mount_arm_a : mount_arm_b;
        const int frame = i == 0 ? mount_aim_a : mount_aim_b;
        if (!rig.weapon_fitted[i] || rig.weapon_model[i].empty()) {
            continue;
        }
        if (!stand) {
            clip_update(f, rig, i);
        }
        if (!pose.mounted[hang]) {
            continue;
        }
        // frame * inverse(hang), with the places subtracted first (both are world matrices)
        const FxMat4& m = pose.mount[hang];
        const FxMat4& a = pose.mounted[frame] ? pose.mount[frame] : m;
        const FxMat3 back = mat3_transpose(mat4_basis(m));
        const FxMat4 relative =
            mat4_from(mat3_mul(mat4_basis(a), back), transform(a.w - m.w, back));
        pose.weapon_node_count[i] = place_model(rig.weapon_model[i], pose.weapon_translate[i],
                                                pose.weapon_euler[i], m, &relative,
                                                pose.weapon_nodes[i]);
    }
}

// the weapon models' rest values into the pose (each fighter's copy of the models at load)
void weapon_values_init(FighterPoseState& pose, const FighterRig& rig) {
    for (int mount = 0; mount < 4; ++mount) {
        for (std::size_t n = 0; n < rig.weapon_model[mount].size() &&
                                n < static_cast<std::size_t>(weapon_model_nodes);
             ++n) {
            const RigModelNode& node = rig.weapon_model[mount][n];
            pose.weapon_translate[mount][n] = node.translate;
            for (int k = 0; k < 3; ++k) {
                pose.weapon_euler[mount][n][k] = node.euler[k];
            }
        }
    }
}

// decomp: actor_wheel_spin_update 0x8893F
void wheel_spin_update(Fighter& f) {
    Fx distance;
    if (f.grounded) {
        distance = length(f.position - f.position_prev);
        if (f.motion_state == state_run_reverse || f.motion_state == state_walk_back) {
            distance = -distance;
        }
        if (distance < wheel_still || f.motion_state == state_turn) {
            distance += fx_abs(fx_mul_near(axis(f, axis_leg_turn), wheel_stick));
        }
    } else {
        distance = fx_raw(static_cast<std::int32_t>(div_round(axis(f, axis_forward).raw, 2)));
    }
    const Fx turn = fx_clamp(-fx_mul_near(distance, wheel_inverse_pi), -wheel_most, wheel_most);
    f.wheel_angle += angle_from_radians(turn);
}

// actor_attachments_update: the leg class's 14 points under their bound nodes; 13 is an offset
// from the position (the upper contact sphere and the chase camera's head)
void attachments_update(Fighter& f, const BattleData& data, const FighterRig& rig) {
    FighterPoseState& pose = f.pose;
    const LegAttachment* rows = data.legs[rig.leg_class < 0 ? 0 : rig.leg_class].attachments;
    for (int i = 0; i < leg_attachment_count; ++i) {
        FighterAttachment& at = pose.attachments[i];
        const bool bound = at.part >= 0 && at.matrix >= 0 && at.matrix < pose_matrices &&
                           (pose.stored[at.part] & (1u << at.matrix));
        if (!bound) {
            at.kind = -1;
            if (i == 13) {
                // retail puts the position plus a radius here, not an offset (docs/bugs.md)
                at.kind = 3;
                at.position = {Fx{}, f.radius, Fx{}};
            }
            continue;
        }
        if (i == 8 && rig.body_class == 1) {
            at.kind = -1;
            continue;
        }
        const FxMat4& m = pose.palette[at.part][at.matrix];
        at.previous = at.position;
        at.kind = rows[i].selector;
        const FxVec3 point = transform_coord(rows[i].a, m);
        if (at.kind == 0 || at.kind == 3) {
            at.position = i == 13 ? point - f.position : point;
            continue;
        }
        const FxVec3 direction = transform(rows[i].b, mat4_basis(m));
        at.position = point + direction;
        at.direction = normalized(direction);
    }
    f.head_height = pose.attachments[13].position.y;
}

} // namespace

PoseInput pose_input_of(const Fighter& f, const BattleData& data) {
    PoseInput in;
    in.data = &data;
    in.rig = rig_of_build(data, f.build);
    in.slot = f.motion_slot_a;
    in.frame = f.motion_phase;
    in.motion_state = f.motion_state;
    in.position = f.position;
    in.heading = f.heading;
    in.aim_yaw = f.aim_yaw;
    in.pitch = f.camera.pitch;
    in.wheel_angle = f.wheel_angle;
    in.turn_rate = f.turn_rate;
    in.node_frame[0] = f.node_frame_400;
    in.node_frame[1] = f.node_frame_800;
    in.node_frame[2] = f.node_frame_2000;
    in.grounded = f.grounded;
    if (f.pose_blend_steps > 0) {
        const int update = (f.pose_blend_steps + 1) / 2;
        in.blend = update <= 10 ? blend_weight[update - 1] : Fx{};
    }
    // sv_pose_weapon_node: a ranged arm raised raise / 5 (raise counts steps to 10 here, R12),
    // turned toward the point its HUD aimed at last step
    for (int i = 0; i < 2; ++i) {
        const MountTrigger& t = f.combat.triggers[i];
        in.arm_raise[i] = t.ranged && t.raise > 0 ? fx_ratio(t.raise, arm_raise_full) : Fx{};
        in.aim_point[i] =
            f.combat.mount_free_aim[i] ? f.combat.reticle_track_world : f.reticle_center_world;
    }
    return in;
}

void fighter_pose_step(Fighter& f, const BattleData& data, bool stand) {
    const FighterRig* rig = rig_of_build(data, f.build);
    if (!rig) {
        return;
    }
    // actor_pose_reset's countdown of the blend
    if (f.pose_blend_steps >= 0) {
        f.pose_blend_steps += 1;
        if (f.pose_blend_steps > 20) {
            f.pose_blend_steps = -1;
        }
    }
    pose_walk(pose_input_of(f, data), f.pose);
    back_weapons_place(f, *rig, stand);
    arm_weapons_place(f, *rig, stand);
    wheel_spin_update(f);
    attachments_update(f, data, *rig);
}

void fighter_pose_init(Fighter& f, const BattleData& data) {
    const FighterRig* rig = rig_of_build(data, f.build);
    if (!rig) {
        return;
    }
    // no HUD has run yet: the reticle 64 ahead of the chest, as hud_reticle puts it level
    f.reticle_center_world =
        f.position + FxVec3{Fx{}, f.radius, Fx{}} + turned_by({Fx{}, Fx{}, fx(64)}, f.heading);
    f.combat.reticle_track_world = f.reticle_center_world;
    weapon_values_init(f.pose, *rig);
    pose_walk(pose_input_of(f, data), f.pose);
    back_weapons_place(f, *rig, false);
    arm_weapons_place(f, *rig, false);
    attachments_update(f, data, *rig);
}

// decomp: actor_phases_update 0x8D5BD
void fighter_phases_update(Fighter& f) {
    const int leg = f.leg_class;
    // actor_phase_fall_update (leg classes 0 and 5): the fall pose in while falling,
    // a player's snaps back, an AI's runs on past 14 or back under it
    if (leg == 0 || leg == 5) {
        Fx& phase = f.node_frame_2000;
        if (f.motion_state == state_fall) {
            if (phase < phase_hold) {
                phase += fx(1);
            }
        } else if (phase != Fx{}) {
            if (f.controller != -1) {
                phase = Fx{};
            } else {
                phase += phase > phase_turn ? fx(1) : fx(-1);
            }
        }
        phase = fx_clamp(phase, Fx{}, phase_end);
    }
    // actor_phase_start_stop_update: a run's start or stop runs the clip on to its end,
    // half a tick a tick. (The event lasts one step here, one update in retail: half the push)
    {
        Fx& phase = f.node_frame_800;
        if (f.motion_events & motion_event_runs) {
            phase = fx_min(phase + fx_ratio(1, 2), phase_turn);
        } else if (phase > Fx{}) {
            phase += fx_ratio(1, 2);
            if (phase >= phase_end) {
                phase = Fx{};
            }
        }
        phase = fx_clamp(phase, Fx{}, phase_end);
    }
    // actor_phase_run_update: while running (or leg class 20 always) the run loop 14..44
    // (back 30 at 44: a step here, an update in retail, so a cycle is 31 ticks for 32); after it
    // the clip runs out past 14 or back under it
    {
        Fx& phase = f.node_frame_400;
        if (leg == 20 || f.motion_state == state_run || f.motion_state == state_run_reverse) {
            phase = phase < phase_hold ? phase + fx(1) : phase - phase_run_back;
        } else if (phase != Fx{}) {
            if (phase >= phase_end) {
                phase = Fx{};
            } else {
                phase += phase > phase_turn ? fx(1) : fx(-1);
            }
        }
        phase = fx_clamp(phase, Fx{}, phase_end);
    }
}
