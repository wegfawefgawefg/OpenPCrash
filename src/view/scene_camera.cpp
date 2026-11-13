#include "view/scene_camera.hpp"

#include "ai/ai_links.hpp"
#include "assets/pack_sexp.hpp"
#include "battle/battle_flow.hpp"
#include "cook/sexp_read.hpp"
#include "stage/stage_animate.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace {

constexpr float radians_per_unit = 6.28318530718f / 4294967296.0f;
// the entrance's last 20 ticks blend into the fighter's camera (camera_scene_update)
constexpr float intro_blend_ticks = 20.0f;
// camera_front_height 1.65 + 2 |view_pitch|, camera_front_distance 8 ahead (port frame +z)
constexpr float front_height = 1.65f;
constexpr float front_distance = 8.0f;
// a boss's defeat holds the rail eye within 10 (scene_boss_defeat_update)
constexpr float boss_rail_reach = 10.0f;
// rail_repick_clock wraps at 30 calls, one an update; R4: 60 steps
constexpr int rail_repick_steps = 60;
// the sources a view's camera can come from; a change of source is a cut
constexpr int source_fighter = 0;
constexpr int source_scene = 100;  // + the viewer's scene state
constexpr int source_front = 200;  // + the slot looked at
constexpr int source_boss = 300;   // + the boss's slot
constexpr int source_rail = 400;   // + slot * 64 + rail point
constexpr int source_demo = 1000;  // the demo battle's fixed camera
constexpr int source_replay = 1100; // + the replay's view
// the demo battle holds the rail eye within 12.5 (demo_battle_enter_next)
constexpr float demo_rail_reach = 12.5f;

float radians(Angle a) {
    return static_cast<float>(angle_signed(a)) * radians_per_unit;
}
void to_floats(FxVec3 v, float out[3]) {
    out[0] = fx_float(v.x), out[1] = fx_float(v.y), out[2] = fx_float(v.z);
}

// the curves at the scene's frame, about its focus and heading when
// relative (the port's heading turns local +z to (sin, 0, cos), the same formula as the
// original's RotationY in its mirrored frame). The pose keeps what it was sampled from, so a
// drawn frame between two steps samples the curves again at its own fractional frame
// decomp: scene_camera_sample 0xADF9D
CameraPose scene_sample(const SceneCamera& row, const FighterScene& scene) {
    const float duration = static_cast<float>(row.duration);
    SceneCurveAt at;
    at.row = &row;
    at.frame = std::clamp(duration - fx_float(scene.timer), 0.0f, duration - 1.0f);
    at.heading = radians(scene.heading);
    to_floats(scene.focus, at.focus);
    return scene_curve_pose(at);
}

// views 2..5's target: position + head_offset, raised by the radius
void fighter_target(const Fighter& f, float out[3]) {
    to_floats(f.position + f.camera.head_offset, out);
    out[1] += fx_float(f.radius);
}

// camera_view4_front 0x7E696: 8 ahead of the fighter along its view yaw, up 1.65 + 2 |pitch|
CameraPose front_camera(const Fighter& f) {
    CameraPose pose;
    pose.looks_at = true;
    const float yaw = radians(f.camera.view_yaw);
    const float up = front_height + 2.0f * std::fabs(radians(f.camera.view_pitch));
    float at[3];
    to_floats(f.position, at);
    pose.position[0] = at[0] + std::sin(yaw) * front_distance;
    pose.position[1] = at[1] + up;
    pose.position[2] = at[2] + std::cos(yaw) * front_distance;
    fighter_target(f, pose.target);
    return pose;
}

// camera_replay_pass: the replay's view of a fighter, through the world's view functions (the
// chase smoothed from the replay's own eye history, eye_replay, which every replay view leaves)
CameraPose replay_camera(SceneCameraState& state, const SceneCameraTables& tables,
                         const World& world, int slot, int view) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    FxVec3& history = state.eye_replay[slot];
    if (!state.eye_replay_set[slot]) {
        history = f.camera.eye;
        state.eye_replay_set[slot] = true;
    }
    CameraPose pose;
    if (view == 5) {
        pose = rail_camera(state, tables, world, slot, 1e9f); // the rail point as it is
        const auto fx_of = [](float v) { return fx_bits(std::bit_cast<std::uint32_t>(v)); };
        history = {fx_of(pose.position[0]), fx_of(pose.position[1]), fx_of(pose.position[2])};
        return pose;
    }
    FxVec3 eye, target;
    if (view == 1) {
        Fighter chased = f;
        chased.camera.eye = history;
        fighter_view_of(chased, *world.battle, 1, eye, target);
    } else {
        fighter_view_of(f, *world.battle, view, eye, target);
    }
    history = eye;
    pose.looks_at = true;
    to_floats(eye, pose.position);
    to_floats(target, pose.target);
    return pose;
}

// the camera a human's own scene shows, or none (source_fighter)
int viewer_camera(const SceneCameraTables& tables, const World& world, int slot,
                  const CameraPose& own, CameraPose& out) {
    if (slot < 0 || slot >= fighter_slots) {
        return source_fighter;
    }
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const FighterScene& scene = world.battle_state.slots[slot].scene;
    if (!f.present || f.controller == -1 || scene.state <= 0 || scene.state >= scene_state_count) {
        return source_fighter;
    }
    if (scene.state == 7) {
        // the death scene after its first step: the killer's front, then the wreck's
        const int k = scene.killer;
        if (scene.step == 0 || k < 0 || k >= fighter_slots ||
            !world.fighters[static_cast<std::size_t>(k)].present) {
            return source_fighter;
        }
        out = front_camera(world.fighters[static_cast<std::size_t>(k)]);
        return source_front + k;
    }
    const SceneCamera& row = tables.rows[scene.state];
    if (!row.present) {
        return source_fighter;
    }
    out = scene_sample(row, scene);
    const float timer = fx_float(scene.timer);
    if (scene.state >= 1 && scene.state <= 4 && timer < intro_blend_ticks) {
        // the entrance's end: the eye and target into the fighter's own (roll and fov not)
        out.scene.mix = timer / intro_blend_ticks;
        std::copy(own.position, own.position + 3, out.scene.own_position);
        std::copy(own.target, own.target + 3, out.scene.own_target);
        out = scene_curve_pose(out.scene);
    }
    return source_scene + scene.state;
}

// the boss whose scene takes the main view (camera_scene_intro_set and camera_scene_rail_set write
// view 0 after every human's camera), -1 none
int boss_camera(SceneCameraState& state, const SceneCameraTables& tables, const World& world,
                CameraPose& out) {
    const BattleState& b = world.battle_state;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const FighterScene& scene = b.slots[slot].scene;
        if (!f.present || b.slots[slot].parked || f.controller != -1 || scene.step < 1 ||
            !roster_gets_boss_scenes(world, battle_roster_entry(world, slot))) {
            continue;
        }
        if (scene.state == 5 && scene.step == 1 && !b.boss_scene_done && tables.rows[5].present) {
            out = scene_sample(tables.rows[5], scene);
            return source_boss + slot;
        }
        // the defeat: from its update on (the original goes back to the human's camera for the
        // update or two between its exit and the battle's end; the port holds the rail)
        if (scene.state >= 0x13 && scene.state <= 0x17) {
            out = rail_camera(state, tables, world, slot, boss_rail_reach);
            return source_rail + slot * 64 + state.rail_point[slot];
        }
    }
    return -1;
}

void show(SceneCameraState::Shown& shown, CameraPose& previous, CameraPose& current,
          const CameraPose& drawn, int source) {
    // a cut unless the source carries on, or an entrance hands over to the fighter's camera
    const bool handover = source == source_fighter && shown.source > source_scene &&
                          shown.source <= source_scene + 4;
    const bool carries = source == shown.source || handover;
    if (source == source_fighter && shown.source == source_fighter) {
        // the fighter's own both steps: take_step_poses's pair as it is
    } else {
        previous = carries ? shown.last : drawn;
        current = drawn;
    }
    shown.last = current;
    shown.source = source;
}

} // namespace

bool replay_cockpit_shown(const SceneCameraState& state) {
    return state.main.source == source_replay;
}

CameraPose scene_curve_pose(const SceneCurveAt& at) {
    CameraPose pose;
    if (!at.row) {
        return pose;
    }
    float v[8];
    for (int k = 0; k < 8; ++k) {
        v[k] = motion_curve_value(at.row->curves[k], at.frame);
    }
    pose.looks_at = true;
    pose.roll = v[0];
    pose.fov = v[1];
    float eye[3] = {v[2], v[3], v[4]}, target[3] = {v[5], v[6], v[7]};
    if (at.row->relative) {
        const float c = std::cos(at.heading), s = std::sin(at.heading);
        for (float* p : {eye, target}) {
            const float x = p[0], z = p[2];
            p[0] = x * c + z * s + at.focus[0];
            p[1] += at.focus[1];
            p[2] = -x * s + z * c + at.focus[2];
        }
    }
    for (int i = 0; i < 3; ++i) {
        pose.position[i] = eye[i] * at.mix + at.own_position[i] * (1.0f - at.mix);
        pose.target[i] = target[i] * at.mix + at.own_target[i] * (1.0f - at.mix);
    }
    pose.scene = at;
    return pose;
}

bool load_scene_camera_tables(SceneCameraTables& out, const Pack& core, std::string& error) {
    out = SceneCameraTables{};
    if (!load_scene_cameras(out.rows, core, error)) {
        return false;
    }
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/camera_rails.sexp", parsed, error)) {
        error += " (run pc_extract --only scenes)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || !form.head().is_atom("rails")) {
            continue;
        }
        const int mode = atom_int(form.child_at(1));
        if (mode < 0 || mode >= 8) {
            continue;
        }
        const gsexp::FormView rails(form);
        for (gsexp::Node p : rails.find("points").children()) {
            if (p.is_list()) {
                out.rail_points[mode].push_back(
                    {atom_float(p.child_at(0)), atom_float(p.child_at(1)), atom_float(p.child_at(2))});
            }
        }
        int zone = 0;
        for (gsexp::Node z : rails.find("zones").children()) {
            if (z.is_list() && zone < 5) {
                out.rail_zones[mode][zone][0] = atom_int(z.child_at(0));
                out.rail_zones[mode][zone][1] = atom_int(z.child_at(1));
                ++zone;
            }
        }
    }
    out.loaded = true;
    return true;
}

CameraPose rail_camera(SceneCameraState& state, const SceneCameraTables& tables,
                       const World& world, int slot, float reach) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const int mode = state.selector & 7;
    const std::vector<std::array<float, 3>>& points = tables.rail_points[mode];
    float centre[3];
    to_floats(f.combat.damage.center, centre);
    CameraPose pose;
    pose.looks_at = true;
    fighter_target(f, pose.target);
    if (points.empty()) {
        std::copy(pose.target, pose.target + 3, pose.position);
        pose.position[2] -= reach;
        return pose;
    }
    int& clock = state.rail_clock[slot];
    int& point = state.rail_point[slot];
    if (clock == 0) {
        // the three points of the zone's range nearest the hit centre across, tried in order:
        // the first with a clear line to it (the stage and the props), else the
        // nearest. With fewer than three only those are tried (retail repeats the last: bugs.md)
        const int zone = std::clamp(stage_zone_at(mode, centre), 0, 4);
        const int first = tables.rail_zones[mode][zone][0];
        const int end = std::min(tables.rail_zones[mode][zone][1], static_cast<int>(points.size()));
        int picks[3] = {0, 0, 0};
        if (first < end) {
            std::vector<float> distances(points.size(), 1e4f);
            for (int j = first; j < end; ++j) {
                const float dx = points[static_cast<std::size_t>(j)][0] - centre[0];
                const float dz = points[static_cast<std::size_t>(j)][2] - centre[2];
                distances[static_cast<std::size_t>(j)] = std::sqrt(dx * dx + dz * dz);
            }
            for (int& pick : picks) {
                float nearest = 1e4f;
                for (int j = first; j < end; ++j) {
                    nearest = std::min(nearest, distances[static_cast<std::size_t>(j)]);
                }
                for (int j = first; j < end; ++j) {
                    if (distances[static_cast<std::size_t>(j)] == nearest) {
                        distances[static_cast<std::size_t>(j)] = 1e4f;
                        pick = j;
                    }
                }
            }
        }
        point = -1;
        const int tries = first < end ? std::min(3, end - first) : 1;
        for (int k = 0; k < tries; ++k) {
            const int pick = picks[k];
            const std::array<float, 3>& p = points[static_cast<std::size_t>(pick)];
            const auto fx_of = [](float v) { return fx_bits(std::bit_cast<std::uint32_t>(v)); };
            const FxVec3 from{fx_of(p[0]), fx_of(p[1]), fx_of(p[2])};
            FxVec3 at, normal;
            bool blocked = collision_segment_frame(world.battle->stage, from,
                                                   f.combat.damage.center, &at, &normal, false);
            // and every prop still there (prop_ray_test mode 0; an open gate still blocks)
            for (int p = 0; p < world.stage.prop_count && !blocked; ++p) {
                PropRayHit hit;
                blocked = prop_ray_test(world.stage.props[static_cast<std::size_t>(p)], from,
                                        f.combat.damage.center, false, &hit);
            }
            if (!blocked) {
                point = pick;
                break;
            }
        }
        if (point < 0) {
            point = picks[0];
        }
    }
    clock = (clock + 1) % rail_repick_steps;
    const std::array<float, 3>& eye = points[static_cast<std::size_t>(point)];
    float out[3] = {eye[0] - pose.target[0], eye[1] - pose.target[1], eye[2] - pose.target[2]};
    const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    const float scale = length > reach ? reach / length : 1.0f;
    for (int i = 0; i < 3; ++i) {
        pose.position[i] = pose.target[i] + out[i] * scale;
    }
    return pose;
}

void scene_cameras_step(SceneCameraState& state, const SceneCameraTables& tables, View& view,
                        const World& after) {
    if (!after.battle || !tables.loaded) {
        return;
    }
    // a demo battle (the result screen's): the rails within 12.5 of the fighter followed, or with
    // none followed yet the stage's fixed demo camera (camera_demo_set; demo_camera_eyes
    // and _targets by day mode, static constructors, port frame); with the fighter gone after one
    // was, the last picture stays
    if (after.battle_phase == 1) {
        const BattleState& b = after.battle_state;
        CameraPose drawn = state.main.last;
        int source = state.main.source;
        if (after.fighters[static_cast<std::size_t>(b.demo_slot)].present) {
            drawn = rail_camera(state, tables, after, b.demo_slot, demo_rail_reach);
            source = source_rail + b.demo_slot * 64 + state.rail_point[b.demo_slot];
        } else if (!b.demo_followed) {
            static constexpr float eyes[4][3] = {
                {52.5f, 10, 128}, {13, 3, 23.4f}, {50, 30, 53.4f}, {-70, 20, 62}};
            static constexpr float targets[4][3] = {
                {60, 14, 100}, {15.5f, 0, 28}, {25, 13, 90}, {-90, 8, 90}};
            const int row = (state.selector & 7) / 2;
            drawn = CameraPose{};
            drawn.looks_at = true;
            std::copy(eyes[row], eyes[row] + 3, drawn.position);
            std::copy(targets[row], targets[row] + 3, drawn.target);
            source = source_demo;
        }
        show(state.main, view.previous.camera, view.current.camera, drawn, source);
        return;
    }
    // the main view: a boss's scene over everything, else its fighter's own scene
    int shown = view.follow >= 0 && view.follow < fighter_slots &&
                        after.fighters[static_cast<std::size_t>(view.follow)].present
                    ? view.follow
                    : seat_slot(after, 0);
    shown = shown < 0 ? 0 : shown;
    CameraPose drawn = view.current.camera;
    int source = boss_camera(state, tables, after, drawn);
    if (source < 0) {
        source = viewer_camera(tables, after, shown, view.current.camera, drawn);
    }
    // a replay's own view of the human outside its scenes (view 1 too: its own chase history)
    if (source == source_fighter && after.battle_phase == 2 && state.replay_view >= 0 &&
        after.fighters[static_cast<std::size_t>(shown)].present) {
        drawn = replay_camera(state, tables, after, shown, state.replay_view);
        source = source_replay + state.replay_view +
                 (state.replay_view == 5 ? 8 * state.rail_point[shown] : 0);
    }
    show(state.main, view.previous.camera, view.current.camera, drawn, source);
    // each seat's own view (split screen: no bosses, doors or deaths in VS, but the entrances)
    for (int seat = 0; seat < max_pad_players; ++seat) {
        const int slot = seat_slot(after, seat);
        const auto i = static_cast<std::size_t>(seat);
        CameraPose seat_drawn = view.current.seat_cameras[i];
        const int seat_source =
            viewer_camera(tables, after, slot, view.current.seat_cameras[i], seat_drawn);
        show(state.seats[seat], view.previous.seat_cameras[i], view.current.seat_cameras[i],
             seat_drawn, seat_source);
    }
}
