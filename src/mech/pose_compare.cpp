// --pose-compare: the world's fixed-point pose (pose_walk.cpp) against the float walk
// (mech_pose.cpp) for one build over its idle, walk, run and jump clips, at three places and
// headings on and off the origin, still and with the upper body turned, pitched and leaning. Not
// a test (nothing passes or fails): it prints the largest node error in units, where it was, and
// what the data holds of the cases where retail's walk goes wrong (docs/bugs.md).
#include "mech/mech_parts.hpp"

#include "battle/battle_data.hpp"
#include "mech/fighter_pose.hpp"

#include "mech/pose_math.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace {

struct Worst {
    float position = 0, rotation = 0;
    char where[160] = "";
};

float radians_of(Angle a) {
    return static_cast<float>(angle_signed(a)) * (6.28318530718f / 4294967296.0f);
}

void compare_matrix(const FxMat4& fixed, const Mat4& reference, Worst& worst, const char* clip,
                    float frame, const char* what, int part, int index) {
    const FxVec3* rows[4] = {&fixed.x, &fixed.y, &fixed.z, &fixed.w};
    float rotation = 0;
    for (int r = 0; r < 3; ++r) {
        const float d[3] = {fx_float(rows[r]->x) - reference.m[r][0],
                            fx_float(rows[r]->y) - reference.m[r][1],
                            fx_float(rows[r]->z) - reference.m[r][2]};
        for (float e : d) {
            rotation = std::fmax(rotation, std::fabs(e));
        }
    }
    const float dx = fx_float(fixed.w.x) - reference.m[3][0];
    const float dy = fx_float(fixed.w.y) - reference.m[3][1];
    const float dz = fx_float(fixed.w.z) - reference.m[3][2];
    const float position = std::sqrt(dx * dx + dy * dy + dz * dz);
    worst.rotation = std::fmax(worst.rotation, rotation);
    if (position > worst.position) {
        worst.position = position;
        std::snprintf(worst.where, sizeof worst.where, "%s frame %.2f %s %d.%d", clip, frame, what,
                      part, index);
    }
}

// the last key of a clip's curves, ticks
int clip_length(const FxClip* clip) {
    int last = 0;
    for (const FxTrack& track : *clip) {
        for (int k = 0; k < 3; ++k) {
            for (const FxCurve* curve : {&track.rotate[k], &track.translate[k]}) {
                if (!curve->times.empty() && curve->times.back() > last) {
                    last = curve->times.back();
                }
            }
        }
    }
    return last;
}

// what the data holds of retail's walk going wrong: IK tips that turn as well as move (retail
// reads their rotation as the target), tips with no translation (retail's target is garbage), and
// arm tracks whose flags differ from the aim clip's (retail reads the aim clip by the wrong flags)
void data_checks(const BattleData& data, const FighterRig& rig) {
    int tip_turns = 0, tip_still = 0, chains = 0, arm_mismatch = 0;
    std::vector<int> open{rig.leg_graph};
    while (!open.empty()) {
        const int index = open.back();
        open.pop_back();
        if (index < 0 || index >= static_cast<int>(data.rig_nodes.size())) {
            continue;
        }
        const RigNode& node = data.rig_nodes[index];
        open.insert(open.end(), node.children.begin(), node.children.end());
        if (node.type != 2 || node.children.empty() || node.children[0] < 0) {
            continue;
        }
        const RigNode& mid = data.rig_nodes[node.children[0]];
        if (mid.children.empty() || mid.children[0] < 0) {
            continue;
        }
        const RigNode* tip = &data.rig_nodes[mid.children[0]];
        if (tip->type != 4 && !tip->children.empty() && tip->children[0] >= 0) {
            tip = &data.rig_nodes[tip->children[0]];
        }
        for (int slot = 0; slot < motion_slot_count; ++slot) {
            const FxTrack* track = fx_track(fx_motion_slot(rig.leg_motion, slot), tip->track);
            if (!fx_motion_slot(rig.leg_motion, slot)) {
                continue;
            }
            ++chains;
            if (!track || !(track->flags & track_translate)) {
                ++tip_still;
            } else if (track->flags & track_rotate) {
                ++tip_turns;
            }
        }
    }
    for (int arm = 0; arm < 2; ++arm) {
        const FxClip* aim = fx_motion_slot(rig.arm_motion[arm], arm == 0 ? 21 : 22);
        std::vector<int> nodes{rig.arm_graph[arm]};
        while (!nodes.empty() && aim) {
            const int index = nodes.back();
            nodes.pop_back();
            if (index < 0 || index >= static_cast<int>(data.rig_nodes.size())) {
                continue;
            }
            const RigNode& node = data.rig_nodes[index];
            nodes.insert(nodes.end(), node.children.begin(), node.children.end());
            const FxTrack* aimed = fx_track(aim, node.track);
            for (int slot = 0; slot < motion_slot_count && node.track >= 0; ++slot) {
                const FxTrack* track = fx_track(fx_motion_slot(rig.arm_motion[arm], slot), node.track);
                if (track && aimed &&
                    (track->flags & track_channels) != (aimed->flags & track_channels)) {
                    ++arm_mismatch;
                }
            }
        }
    }
    std::printf("pose-compare data: %d chain-clips, %d tips turning, %d tips without translation; "
                "%d arm tracks with flags unlike the aim clip's\n",
                chains, tip_turns, tip_still, arm_mismatch);
}

// The three-link IK alone against the same arithmetic in doubles, the Holy legs' lengths, the
// reach from bent to straight: how far the knee and the foot land from the exact answer in fixed
// point and in floats. Near a straight leg the knee's bend moves a long way for a small change of
// reach (d bend = d reach / (a sin bend)), so there both lose to the doubles by their inputs'
// rounding
using M3 = std::array<double, 9>;
M3 mul3(const M3& a, const M3& b) {
    M3 out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k) {
                out[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c];
            }
        }
    }
    return out;
}
void ik_double(const double v[3], double a, double b, double knee[3], double foot[3]) {
    const double flat = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    const double reach = std::sqrt(flat * flat + v[2] * v[2]);
    const M3 turn{v[0] / flat, v[1] / flat, 0, -v[1] / flat, v[0] / flat, 0, 0, 0, 1};
    const M3 lift{flat / reach, 0, v[2] / reach, 0, 1, 0, -v[2] / reach, 0, flat / reach};
    M3 joint = mul3(lift, turn);
    double c = (a * a + reach * reach - b * b) / (2 * a * reach);
    double s = std::sqrt(1 - c * c);
    joint = mul3(M3{c, -s, 0, s, c, 0, 0, 0, 1}, joint);
    c = (a * a + b * b - reach * reach) / (2 * a * b);
    s = std::sqrt(1 - c * c);
    const M3 end = mul3(M3{-c, s, 0, -s, -c, 0, 0, 0, 1}, joint);
    for (int k = 0; k < 3; ++k) {
        knee[k] = a * joint[k];
        foot[k] = knee[k] + b * end[k];
    }
}
void ik_precision() {
    const double a = 1.03096938, b = 1.10064769;
    float worst_fixed[2] = {}, worst_float[2] = {}; // bent (reach under 2.1), near straight
    for (int i = 0; i <= 4000; ++i) {
        const double reach = 1.0 + (a + b - 1.0) * i / 4000.0;
        const double v[3] = {0.6 * reach * 0.998, -0.8 * reach * 0.998,
                             reach * std::sqrt(1 - 0.998 * 0.998)};
        double knee[3], foot[3];
        ik_double(v, a, b, knee, foot);
        // fixed
        FxMat4 joint, end, tip;
        const FxVec3 target{fx_raw(static_cast<std::int32_t>(std::lround(v[0] * 65536))),
                            fx_raw(static_cast<std::int32_t>(std::lround(v[1] * 65536))),
                            fx_raw(static_cast<std::int32_t>(std::lround(v[2] * 65536)))};
        ik_solve_three(target, mat4_identity(), fx_raw(static_cast<std::int32_t>(std::lround(a * 65536))),
                       fx_raw(static_cast<std::int32_t>(std::lround(b * 65536))), true, joint,
                       end, tip);
        // floats
        Mat4 fjoint, fend, ftip;
        ik_solve_three(Vec3f{static_cast<float>(v[0]), static_cast<float>(v[1]),
                             static_cast<float>(v[2])},
                       mat_identity(), static_cast<float>(a), static_cast<float>(b), 1.0f, fjoint,
                       fend, ftip);
        const auto error = [](const double want[3], float x, float y, float z) {
            return static_cast<float>(std::sqrt((x - want[0]) * (x - want[0]) +
                                                (y - want[1]) * (y - want[1]) +
                                                (z - want[2]) * (z - want[2])));
        };
        const int band = reach < 2.1 ? 0 : 1;
        worst_fixed[band] = std::fmax(
            worst_fixed[band], std::fmax(error(knee, fx_float(end.w.x), fx_float(end.w.y),
                                               fx_float(end.w.z)),
                                         error(foot, fx_float(tip.w.x), fx_float(tip.w.y),
                                               fx_float(tip.w.z))));
        const Vec3f fk = mat_position(fend), ff = mat_position(ftip);
        worst_float[band] = std::fmax(worst_float[band], std::fmax(error(knee, fk.x, fk.y, fk.z),
                                                                   error(foot, ff.x, ff.y, ff.z)));
    }
    std::printf("pose-compare IK against doubles (Holy legs): reach under 2.1 fixed %.6f float "
                "%.6f units; 2.1 to straight fixed %.6f float %.6f\n",
                worst_fixed[0], worst_float[0], worst_fixed[1], worst_float[1]);
}

} // namespace

void pose_compare(const MechParts& mech, const BattleData& data, int build) {
    ik_precision();
    const FighterRig* rig = rig_of_build(data, build);
    if (!rig || !mech.tables) {
        std::printf("pose-compare: no rig for build %d\n", build);
        return;
    }
    data_checks(data, *rig);
    // the nodes under the IK chains (part 0): a knee's bend turns fast near a straight leg
    // (d bend = d reach / (a sin bend)), so a raw unit of reach there is thousandths of a radian
    std::uint32_t ik_links = 0;
    std::vector<std::pair<int, bool>> open{{rig->leg_graph, false}};
    while (!open.empty()) {
        const auto [index, under] = open.back();
        open.pop_back();
        if (index < 0 || index >= static_cast<int>(data.rig_nodes.size())) {
            continue;
        }
        const RigNode& node = data.rig_nodes[index];
        if (under && node.matrix >= 0 && node.matrix < pose_matrices) {
            ik_links |= 1u << node.matrix;
        }
        for (int child : node.children) {
            open.push_back({child, under || node.type == 2});
        }
    }
    const struct {
        const char* name;
        int slot, state;
    } clips[] = {{"idle", 28, 11}, {"walk", 0, 0}, {"run", 2, 2}, {"jump", 10, 4}};
    const struct {
        FxVec3 position;
        Angle heading;
    } places[] = {{{}, {}},
                  {{fx_f(111.16), fx_f(0.32), fx_f(152.33)}, angle_f_radians(2.0)},
                  {{fx_f(-250.5), fx_f(20.25), fx_f(80.75)}, angle_f_radians(-2.5)}};
    const struct {
        Angle aim_yaw, pitch;
        Fx turn_rate;
    } bodies[] = {{{}, {}, {}}, {angle_f_radians(0.3), angle_f_radians(-0.25), fx_f(0.2)}};
    Worst overall;
    for (const auto& clip : clips) {
        const FxClip* fx_clip = fx_motion_slot(rig->leg_motion, clip.slot);
        if (!fx_clip) {
            continue;
        }
        Worst worst_at[3];
        Worst rigid; // every node but those under the IK chains, and the mounts
        int samples = 0;
        const int last = clip_length(fx_clip);
        for (int quarter = 0; quarter <= last * 4; ++quarter) {
            const Fx frame = fx_ratio(quarter, 4);
            for (int p = 0; p < 3; ++p) {
                const auto& place = places[p];
                Worst& worst = worst_at[p];
                for (const auto& body : bodies) {
                    PoseInput in;
                    in.data = &data;
                    in.rig = rig;
                    in.slot = clip.slot;
                    in.frame = frame;
                    in.motion_state = clip.state;
                    in.position = place.position;
                    in.heading = place.heading;
                    in.aim_yaw = body.aim_yaw;
                    in.pitch = body.pitch;
                    in.turn_rate = body.turn_rate;
                    FighterPoseState fixed;
                    pose_walk(in, fixed);

                    const Mat4 world = mat_multiply(
                        mat_rotation_y(radians_of(place.heading)),
                        mat_translation(fx_float(place.position.x), fx_float(place.position.y),
                                        fx_float(place.position.z)));
                    MechPoseInput reference_in =
                        mech_pose_input(mech, clip.slot, fx_float(frame), world);
                    reference_in.motion_state = clip.state;
                    // the float walk takes the original's sense for the angles it mirrors
                    reference_in.aim_yaw = -radians_of(body.aim_yaw);
                    reference_in.pitch = -radians_of(body.pitch);
                    reference_in.turn_rate = fx_float(body.turn_rate);
                    MechPose reference;
                    build_mech_pose(reference_in, reference);

                    for (int part = 0; part < pose_parts; ++part) {
                        for (int m = 0; m < pose_matrices; ++m) {
                            if ((fixed.stored[part] & (1u << m)) && reference.stored[part][m]) {
                                compare_matrix(fixed.palette[part][m], reference.matrices[part][m],
                                               worst, clip.name, fx_float(frame), "node", part, m);
                                if (part != 0 || !(ik_links & (1u << m))) {
                                    compare_matrix(fixed.palette[part][m],
                                                   reference.matrices[part][m], rigid, clip.name,
                                                   fx_float(frame), "node", part, m);
                                }
                            } else if ((fixed.stored[part] & (1u << m)) !=
                                       (reference.stored[part][m] ? (1u << m) : 0u)) {
                                std::printf("pose-compare: %s frame %.2f node %d.%d stored by one "
                                            "walk only\n",
                                            clip.name, fx_float(frame), part, m);
                            }
                        }
                    }
                    for (int i = 0; i < 5; ++i) {
                        if (fixed.mounted[i] && reference.mounted[i]) {
                            compare_matrix(fixed.mount[i], reference.mount[i], worst, clip.name,
                                           fx_float(frame), "mount", 0, i);
                            compare_matrix(fixed.mount[i], reference.mount[i], rigid, clip.name,
                                           fx_float(frame), "mount", 0, i);
                        }
                    }
                    ++samples;
                }
            }
        }
        Worst worst;
        for (const Worst& at : worst_at) {
            const float rotation = std::fmax(worst.rotation, at.rotation);
            if (at.position >= worst.position) {
                worst = at;
            }
            worst.rotation = rotation;
        }
        std::printf("pose-compare build %d %-4s: %d poses, largest node error %.5f units (%s), "
                    "largest rotation entry error %.6f; by place (origin, 111 out, 265 out) %.5f "
                    "%.5f %.5f\n",
                    build, clip.name, samples, worst.position, worst.where, worst.rotation,
                    worst_at[0].position, worst_at[1].position, worst_at[2].position);
        std::printf("pose-compare build %d %-4s: off the IK chains %.5f units (%s), rotation "
                    "%.6f\n",
                    build, clip.name, rigid.position, rigid.where, rigid.rotation);
        if (worst.position > overall.position) {
            overall = worst;
        }
        overall.rotation = std::fmax(overall.rotation, worst.rotation);
    }
    std::printf("pose-compare build %d: largest %.5f units (%s), rotation %.6f\n", build,
                overall.position, overall.where, overall.rotation);
}
