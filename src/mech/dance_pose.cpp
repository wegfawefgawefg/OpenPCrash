#include "mech/dance_pose.hpp"

#include "mech/mech_parts.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

// the idle motion slot (motion_set_animation(actor, 0x1C, 0xB, ...))
constexpr int idle_slot = 28;
// the root's channels in the table: rx ry rz tx ty tz
constexpr int root_part = 3;

// "legs.6.tx" or "root.ry"
bool parse_channel(const std::string& name, DanceChannel& out) {
    const std::size_t last = name.rfind('.');
    if (last == std::string::npos || last + 3 != name.size()) {
        return false;
    }
    const char kind = name[last + 1], axis = name[last + 2];
    if ((kind != 'r' && kind != 't') || axis < 'x' || axis > 'z') {
        return false;
    }
    out.channel = (kind == 't' ? 3 : 0) + (axis - 'x');
    const std::string head = name.substr(0, last);
    if (head == "root") {
        out.part = root_part;
        return true;
    }
    const std::size_t dot = head.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string part = head.substr(0, dot);
    out.part = part == "legs" ? 0 : part == "arm_a" ? 1 : part == "arm_b" ? 2 : -1;
    out.track = std::atoi(head.c_str() + dot + 1);
    return out.part >= 0 && out.track >= 0;
}

// the graph node a track drives, under root
const SvGraphNode* track_node(const FighterTables& tables, int root, int track) {
    std::vector<int> pending{root};
    while (!pending.empty()) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index >= static_cast<int>(tables.nodes.size())) {
            continue;
        }
        const SvGraphNode& node = tables.nodes[index];
        if (node.track == track && node.type != 7) {
            return &node;
        }
        if (node.type != 7) {
            pending.insert(pending.end(), node.children.begin(), node.children.end());
        }
    }
    return nullptr;
}

// one part's clip: the idle clip, each danced channel's group (rotate or translate) rewritten as
// a per-tick linear curve of its idle value at tick 0 plus the table's offset
void build_part(Dance& dance, int part, const MotionClip* idle, const FighterTables* tables,
                int root, MotionClip& out) {
    out = idle ? *idle : MotionClip{};
    // the idle held at its tick 0, so the loop doesn't run the idle clip out under the dance
    for (MotionTrack& track : out) {
        for (int k = 0; k < 3; ++k) {
            for (MotionCurve* curve : {&track.rotate[k], &track.translate[k]}) {
                if (curve->present && curve->mode != 0) {
                    *curve = MotionCurve{0, {}, {motion_curve_value(*curve, 0.0f)}, true};
                }
            }
        }
    }
    for (std::size_t c = 0; c < dance.channels.size(); ++c) {
        const DanceChannel& ch = dance.channels[c];
        if (ch.part != part) {
            continue;
        }
        if (ch.track >= static_cast<int>(out.size())) {
            out.resize(static_cast<std::size_t>(ch.track) + 1);
        }
        MotionTrack& track = out[static_cast<std::size_t>(ch.track)];
        const bool rotate = ch.channel < 3;
        const std::uint32_t group = rotate ? track_rotate : track_translate;
        MotionCurve* curves = rotate ? track.rotate : track.translate;
        // the group's values at rest: the idle track's, or the node's static ones
        if (!(track.flags & group) || track.null) {
            const SvGraphNode* node = tables ? track_node(*tables, root, ch.track) : nullptr;
            for (int k = 0; k < 3; ++k) {
                const float rest = node ? (rotate ? node->rotate[k] : node->translate[k]) : 0.0f;
                curves[k] = MotionCurve{0, {}, {rest}, true};
            }
            if (track.null) {
                // a null track's other group read 0 before; keep it so
                MotionCurve* other = rotate ? track.translate : track.rotate;
                for (int k = 0; k < 3; ++k) {
                    other[k] = MotionCurve{0, {}, {0.0f}, true};
                }
                track.null = false;
            }
            track.flags |= group;
        }
        MotionCurve& curve = curves[ch.channel % 3];
        const float rest = motion_curve_value(curve, 0.0f);
        MotionCurve danced{curve_linear, {}, {}, true};
        for (int t = 0; t <= dance.ticks; ++t) {
            if (t > 0) {
                danced.times.push_back(static_cast<std::uint16_t>(t));
            }
            danced.values.push_back(rest + dance.values[t * dance.channels.size() + c]);
        }
        curve = danced;
    }
}

} // namespace

bool load_dance(Dance& dance, const std::string& path, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "can't read " + path;
        return false;
    }
    std::string line, word;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream in(line);
        if (line.rfind("ticks", 0) == 0) {
            in >> word >> dance.ticks;
        } else if (line.rfind("channels", 0) == 0) {
            in >> word;
            while (in >> word) {
                DanceChannel ch;
                if (!parse_channel(word, ch)) {
                    error = "bad channel " + word;
                    return false;
                }
                dance.channels.push_back(ch);
            }
        } else {
            float value = 0;
            while (in >> value) {
                dance.values.push_back(value);
            }
        }
    }
    const std::size_t want = static_cast<std::size_t>(dance.ticks + 1) * dance.channels.size();
    if (dance.ticks <= 0 || dance.ticks > 60000 || dance.values.size() != want) {
        error = path + ": want ticks, channels and " + std::to_string(want) + " values, read " +
                std::to_string(dance.values.size());
        return false;
    }
    return true;
}

void dance_build_clips(Dance& dance, const MechParts& mech) {
    build_part(dance, 0, motion_slot(mech.leg_motion, idle_slot), mech.tables, mech.leg_graph,
               dance.legs);
    build_part(dance, 1, motion_slot(mech.arm_a_motion, idle_slot), mech.tables, mech.arm_a_graph,
               dance.arm_a);
    build_part(dance, 2, motion_slot(mech.arm_b_motion, idle_slot), mech.tables, mech.arm_b_graph,
               dance.arm_b);
}

MechPoseInput dance_pose_input(const Dance& dance, const MechParts& mech, float ticks,
                               const Mat4& base) {
    const float frame = std::fmod(std::fmod(ticks, static_cast<float>(dance.ticks)) +
                                      static_cast<float>(dance.ticks),
                                  static_cast<float>(dance.ticks));
    // the root's turns and offset between the two ticks around the frame
    float root[6] = {};
    const int t0 = static_cast<int>(frame);
    const int t1 = t0 + 1 > dance.ticks ? dance.ticks : t0 + 1;
    const float u = frame - static_cast<float>(t0);
    const std::size_t count = dance.channels.size();
    for (std::size_t c = 0; c < count; ++c) {
        const DanceChannel& ch = dance.channels[c];
        if (ch.part == root_part) {
            const float a = dance.values[static_cast<std::size_t>(t0) * count + c];
            const float b = dance.values[static_cast<std::size_t>(t1) * count + c];
            root[ch.channel] += a + (b - a) * u;
        }
    }
    Mat4 local = mat_multiply(mat_rotation_y(root[1]), mat_rotation_x(root[0]));
    local = mat_multiply(local, mat_rotation_z(root[2]));
    local = mat_multiply(local, mat_translation(root[3], root[4], root[5]));
    MechPoseInput input = mech_pose_input(mech, idle_slot, frame, mat_multiply(local, base));
    input.clip = &dance.legs;
    input.clip_a = mech.arm_a_graph >= 0 ? &dance.arm_a : nullptr;
    input.clip_b = mech.arm_b_graph >= 0 ? &dance.arm_b : nullptr;
    return input;
}
