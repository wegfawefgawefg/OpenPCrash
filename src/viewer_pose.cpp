// --pose FILE: the stage viewer's posed scene (viewer_pose.hpp), for the site's screenshots.
#include "viewer_pose.hpp"

#include "battle/battle_data.hpp"
#include "cli.hpp"
#include "view/view.hpp"
#include "world/inputs.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

constexpr float pi = 3.14159265f;

Angle angle_of_degrees(float degrees) {
    return Angle{static_cast<std::uint32_t>(
        static_cast<std::int64_t>(std::llround(degrees / 360.0 * 4294967296.0)))};
}

Fx fx_of(float value) {
    return fx_raw(static_cast<std::int32_t>(std::lround(value * 65536.0f)));
}

// a camera point in the world: a fighter's frame (right, up, forward of its heading; the port
// frame mirrors z, so its right is (-cos, sin)) or as is
void point_at(const PosePoint& p, const FighterPose* fighters, float out[3]) {
    if (p.slot < 0 || p.slot >= fighter_slots || !fighters[p.slot].present) {
        out[0] = p.at[0], out[1] = p.at[1], out[2] = p.at[2];
        return;
    }
    const FighterPose& f = fighters[p.slot];
    const float s = std::sin(f.heading), c = std::cos(f.heading);
    out[0] = f.position[0] - c * p.at[0] + s * p.at[2];
    out[1] = f.position[1] + p.at[1];
    out[2] = f.position[2] + s * p.at[0] + c * p.at[2];
}

} // namespace

bool load_pose(Pose& pose, const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "can't open " + path;
        return false;
    }
    std::string text;
    int number = 0;
    int seats = 0;
    while (std::getline(in, text)) {
        ++number;
        const std::size_t hash = text.find('#');
        if (hash != std::string::npos) {
            text.resize(hash);
        }
        std::istringstream line(text);
        std::string word;
        if (!(line >> word)) {
            continue; // blank
        }
        const std::string where = path + ":" + std::to_string(number) + ": ";
        bool ok = true;
        if (word == "stage") {
            ok = static_cast<bool>(line >> pose.stage);
        } else if (word == "day") {
            ok = static_cast<bool>(line >> pose.day);
        } else if (word == "seed") {
            ok = static_cast<bool>(line >> pose.seed);
        } else if (word == "steps") {
            ok = static_cast<bool>(line >> pose.steps);
        } else if (word == "fov") {
            ok = static_cast<bool>(line >> pose.fov);
        } else if (word == "roll") {
            ok = static_cast<bool>(line >> pose.roll);
        } else if (word == "fighter") {
            // the next slot: build, place, heading, who drives it
            PoseFighter f;
            std::string mode = "pad";
            ok = static_cast<bool>(line >> f.build >> f.x >> f.z >> f.heading);
            line >> mode;
            f.height_given = static_cast<bool>(line >> f.y);
            if (mode == "pad") {
                f.seat = seats < max_pad_players ? seats++ : -1;
            } else {
                f.seat = mode == "still" ? -2 : -1;
            }
            if (static_cast<int>(pose.fighters.size()) >= fighter_slots) {
                error = where + "at most 6 fighters";
                return false;
            }
            pose.fighters.push_back(f);
        } else if (word == "durability") {
            int slot = 0, value = 0;
            ok =
                line >> slot >> value && slot >= 0 && slot < static_cast<int>(pose.fighters.size());
            if (ok) {
                pose.fighters[static_cast<std::size_t>(slot)].durability = value;
            }
        } else if (word == "pad") {
            // a script line for that fighter's seat
            int slot = 0;
            ScriptLine row;
            std::string buttons;
            ok = line >> slot >> row.steps >> row.stick[0][0] >> row.stick[0][1] >>
                     row.stick[1][0] >> row.stick[1][1] >> buttons &&
                 slot >= 0 && slot < static_cast<int>(pose.fighters.size()) &&
                 pose.fighters[static_cast<std::size_t>(slot)].seat >= 0;
            if (ok) {
                row.buttons = static_cast<std::uint16_t>(std::stoul(buttons, nullptr, 16));
                const int seat = pose.fighters[static_cast<std::size_t>(slot)].seat;
                pose.pads[seat].lines.push_back(row);
            }
        } else if (word == "eye" || word == "look") {
            PosePoint& p = word == "eye" ? pose.eye : pose.look;
            pose.camera = pose.camera || word == "eye";
            ok = static_cast<bool>(line >> p.slot >> p.at[0] >> p.at[1] >> p.at[2]);
        } else {
            error = where + "unknown line '" + word + "'";
            return false;
        }
        if (!ok) {
            error = where + "bad '" + word + "' line";
            return false;
        }
    }
    if (pose.fighters.empty()) {
        error = path + ": no fighter";
        return false;
    }
    return true;
}

void pose_options(const Pose& pose, Options& options) {
    options.stage = pose.stage;
    options.day = pose.day;
    options.seed = pose.seed;
    if (!options.capture.empty()) {
        options.frames = pose.steps + 1;
    }
    options.opponents.clear();
    options.seats.clear();
    for (std::size_t i = 0; i < pose.fighters.size(); ++i) {
        const PoseFighter& f = pose.fighters[i];
        if (i == 0) {
            options.build = std::atoi(f.build.c_str()); // slot 0 takes a build row
        } else {
            options.opponents.push_back(f.build);
        }
        options.seats.push_back(f.seat >= 0 ? f.seat : -1);
    }
}

void pose_place(World& world, const Pose& pose) {
    for (std::size_t i = 0; i < pose.fighters.size(); ++i) {
        const PoseFighter& p = pose.fighters[i];
        Fighter& f = world.fighters[i];
        if (!f.present) {
            continue;
        }
        // on the floor under X, Z (or at the height given), as actor_init stands a fighter at
        // its entry point
        FxVec3 at{fx_of(p.x), fx_of(p.y), fx_of(p.z)};
        FxVec3 normal;
        Fx floor;
        if (!p.height_given &&
            collision_floor_height(world.battle->stage, at, &normal, &floor) != 0) {
            at.y = floor;
        }
        f.position = f.position_prev = at;
        f.heading = f.heading_prev = angle_of_degrees(p.heading);
        f.camera.view_yaw = f.heading;
        f.camera.eye = f.position + FxVec3{Fx{}, fx(1), Fx{}};
        f.camera.target = f.camera.eye;
        f.combat.damage.immune_frames = Fx{}; // placed, not through a gate: no entrance immunity
        if (p.seat == -2) {
            f.controller = -2; // run by nobody, as --dummy's
        }
        if (p.durability > 0) {
            f.combat.durability = p.durability;
        }
        std::printf("pose: slot %zu build %s at %.2f %.2f %.2f heading %.1f seat %d\n", i,
                    p.build.c_str(), fx_float(at.x), fx_float(at.y), fx_float(at.z), p.heading,
                    p.seat);
    }
}

void pose_pads(Pose& pose, Inputs& inputs) {
    for (int seat = 0; seat < max_pad_players; ++seat) {
        inputs.pads[static_cast<std::size_t>(seat)] = next_script_pad(pose.pads[seat]);
        inputs.aims[static_cast<std::size_t>(seat)] = MouseAim{};
    }
}

void pose_camera(const Pose& pose, const FighterPose* fighters, CameraPose& camera) {
    point_at(pose.eye, fighters, camera.position);
    point_at(pose.look, fighters, camera.target);
    const float dx = camera.target[0] - camera.position[0];
    const float dy = camera.target[1] - camera.position[1];
    const float dz = camera.target[2] - camera.position[2];
    camera.looks_at = true;
    camera.yaw = std::atan2(dx, dz);
    camera.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    camera.fov = pose.fov;
    camera.roll = pose.roll * pi / 180.0f;
    camera.scene = SceneCurveAt{};
}
