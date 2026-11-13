// The in-engine ending's scenes (decomp src/hand/u_088880.c scene_ending_enter,
// scene_ending_update, scene_ending_exit; src/hand/beam_quad.c scene_root_apply).
// Retail runs them once an update at 30 Hz with the scene timer dropping 1.0 an update
// (one curve frame), not the ticks most scenes drop: here every per-update count moves half a
// step, so a shot plays its curve at one frame an update as retail's does (sixty.md R1).
#include "battle/battle_ending.hpp"

#include "assets/pack_sexp.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_data.hpp"
#include "battle/battle_flow.hpp"
#include "battle/battle_scene_parts.hpp"
#include "battle/sexp_fx.hpp"
#include "mech/fighter_rates.hpp"
#include "stage/stage_world.hpp"
#include "world/world.hpp"

#include <cstdlib>

using namespace scenes;

namespace {

constexpr Fx half = fx_f(0.5); // one update's count, a step's share
// the first shot's sound (se_ending_shot) when its lead-in passes 15
constexpr int lead_in_sound_at = 15;
// the shot's lines and the last shot's beeps, and its signal's 3D sound
constexpr int line_bank = bank_battle, line_cue = se_radio_bark;
constexpr int signal_bank = bank_battle_player, signal_cue = se_low_armour_alarm;
// the second shot's music (music_play 0x39)
constexpr int ending_track = 0x39;
// the last shot's signal from 60 frames left
constexpr int signal_frames = 60;
// D_0019B518: the root's yaw is taken from pi
constexpr Angle half_turn = angle_f_radians(3.14159265358979);

const EndingShot& shot_of(const World& world, const FighterScene& scene) {
    static const EndingShot none;
    const int row = scene.state - scene_ending_first;
    return row >= 0 && row < ending_shot_count && world.battle->ending.loaded
               ? world.battle->ending.shots[row]
               : none;
}

Fx duration_of(const World& world, int state) {
    return fx(world.battle->scene_walks.duration[state]);
}

// scene_root_apply: an absolute root track places the fighter and turns it; out how far it moved
// and how far it turned (radians)
void root_apply(World& world, Fighter& f, const FighterScene& scene, Fx& moved, Fx& turned) {
    moved = Fx{};
    turned = Fx{};
    const SceneWalks::Root& root = world.battle->scene_walks.roots[scene.state];
    if (!root.present) {
        return;
    }
    const Fx duration = duration_of(world, scene.state);
    const Fx frame = fx_clamp(duration - scene.timer, Fx{}, duration - fx(1));
    if (root.turns) {
        // retail: heading = pi + yaw; the port's heading is the original's negated, and the
        // extracted yaw is already negated
        const Angle heading = half_turn + angle_from_radians(fx_curve_value(root.yaw, frame));
        turned = angle_radians(Angle{static_cast<std::uint32_t>(
            std::abs(angle_signed(Angle{heading.raw - f.heading.raw})))});
        f.heading = heading;
    }
    const FxVec3 at{fx_curve_value(root.x, frame), fx_curve_value(root.y, frame),
                    fx_curve_value(root.z, frame)};
    moved = length(at - f.position);
    f.position_prev = f.position;
    f.position = at;
}

// the shot's scene text (scene_text_start), asked of presentation as the lessons' lines are
void say(World& world, int row) {
    BattleState& b = world.battle_state;
    b.scene_text = ending_script_field + row * 0x34;
    b.scene_text_serial += 1;
    b.scene_text_alive = true;
}

} // namespace

bool load_ending_shots(EndingShots& out, const Pack& core, std::string& error) {
    out = EndingShots{};
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/scene_ending.sexp", parsed, error)) {
        error += " (run pc_extract --only scenes)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("shot")) {
            const int row = atom_int(form.node().child_at(1)) - scene_ending_first;
            if (row < 0 || row >= ending_shot_count) {
                continue;
            }
            EndingShot& s = out.shots[row];
            s.length = atom_fx(form.find("length").child_at(1));
            s.fade_in = atom_fx(form.find("fade-in").child_at(1));
            s.fade_out_at = atom_fx(form.find("fade-out-at").child_at(1));
            s.capture = atom_fx(form.find("capture").child_at(1));
            s.route = atom_int(form.find("route").child_at(1));
            s.script = atom_hex(form.find("script").child_at(1)) != 0;
            s.script_at = atom_fx(form.find("script-at").child_at(1));
            const gsexp::Node paper = form.find("paper");
            for (int k = 0; k < 3; ++k) {
                s.paper.point[k] = atom_int(paper.child_at(static_cast<std::size_t>(1 + k * 2)));
                s.paper.flag[k] = atom_int(paper.child_at(static_cast<std::size_t>(2 + k * 2)));
            }
            s.paper.last = atom_int(paper.child_at(7));
        } else if (form.head().is_atom("beeps")) {
            for (std::size_t k = 1; k < form.node().child_count() && out.beep_count < 16; ++k) {
                out.beeps[out.beep_count++] = atom_int(form.node().child_at(k));
            }
        }
    }
    out.loaded = true;
    return true;
}

const EndingPaper* ending_paper(const World& world) {
    if (world.battle_phase != 3 && world.battle_phase != 6) {
        return nullptr;
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const FighterScene& scene = world.battle_state.slots[slot].scene;
        const int row = scene.state - scene_ending_first;
        if (f.present && f.controller != -1 && row >= 0 && row < ending_shot_count &&
            world.battle && world.battle->ending.loaded) {
            return &world.battle->ending.shots[row].paper;
        }
    }
    return nullptr;
}

// decomp: scene_ending_enter 0x8ABD2, scene_ending_update 0x8AF97
void ending_enter(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    const EndingShot& shot = shot_of(world, scene);
    // everyone stands still, the controls cleared (every update of the lead-in)
    for (int slot = 0; slot < fighter_slots; ++slot) {
        Fighter& other = world.fighters[static_cast<std::size_t>(slot)];
        if (other.present) {
            freeze(world, slot);
            motion_set_animation(other, *world.battle, 0, state_idle, motion_whole, Fx{});
        }
    }
    // the player's camera in view 0, the cockpit (its HUD shows the instruments in the last shot)
    camera_view_cut(f.camera, 0); // scene_ending_enter: view = view_next = 0
    // the weapons' rounds and fitted flags the last boss's defeat left
    const EndingCarry& carry = world.story.ending_carry;
    if (carry.carry) {
        for (int m = 0; m < 4; ++m) {
            f.combat.weapons[m].resource = carry.resource[m];
            f.combat.weapons[m].fitted = carry.fitted[m];
        }
    }
    Fx moved, turned;
    root_apply(world, f, scene, moved, turned);
    scene.door_wait += half;
    if (scene.door_wait >= shot.length) {
        // the shot starts: its fade in (presentation, from door_wait), its timer, the idle
        scene.door_wait = shot.fade_in;
        scene.timer = duration_of(world, scene.state);
        scene.step = 1;
        scene.killer = 0;
        motion_set_animation(f, *world.battle, 0, state_idle, motion_whole, Fx{});
        f.pose_blend_steps = -1;
        if (scene.state == scene_ending_first + 1) {
            battle_event(world, BattleEventKind::Music, ending_track, 0);
        }
    }
    if (scene.state == scene_ending_first && scene.door_wait >= fx(lead_in_sound_at) &&
        scene.door_wait < fx(lead_in_sound_at) + half) {
        battle_event(world, BattleEventKind::Sound, bank_sys, se_ending_shot);
    }
    // the craft flies the shot's route, or is gone
    if (shot.route != -1) {
        supply_craft_route_for_ending(world.stage, shot.route);
    } else {
        supply_craft_active_clear(world.stage);
    }
    b.letterbox_state = 1;
}

void ending_update(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    const EndingShot& shot = shot_of(world, scene);
    const int row = scene.state - scene_ending_first;
    scene.timer -= half;
    f.motion_phase_prev = f.motion_phase;
    Fx moved, turned;
    root_apply(world, f, scene, moved, turned);
    // the clip steps by the distance over actor_leg_walk_speed (the leg class's root
    // motion walk column, units a clip tick; two ticks an update without one), at least a quarter
    // frame an update while it turns over 0.05 rad (each halved for a step)
    Fx advance = fx(1);
    if (f.walk_scale > Fx{}) {
        advance = fx_abs(fx_div_near(moved, f.walk_scale));
    }
    if (advance < fx_f(0.125) && turned > fx_f(0.025)) {
        advance = fx_f(0.125);
    }
    f.motion_phase += advance;
    if (f.timing.loop_end > Fx{} && f.motion_phase >= f.timing.loop_end) {
        f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
    }
    // the shot's line at its frame, with bank 1's 0x2D but in the last shot
    const Fx elapsed = duration_of(world, scene.state) - scene.timer;
    if (shot.script && shot.script_at >= elapsed && elapsed + half > shot.script_at) {
        say(world, row);
        if (scene.state != scene_ending_last) {
            battle_event(world, BattleEventKind::Sound, line_bank, line_cue);
        }
    }
    // the last shot: the signal's sound every other update from 60 frames left, and the beeps at
    // their frames (then every other update once the schedule ends)
    if (scene.state == scene_ending_last) {
        const bool odd_update = (world.step / 2) % 2 == 1 && world.step % 2 == 0;
        if (scene.timer <= fx(signal_frames) && odd_update) {
            battle_event(world, BattleEventKind::Sound, signal_bank, signal_cue);
        }
        const EndingShots& table = world.battle->ending;
        const int k = scene.killer;
        if (k >= table.beep_count || table.beeps[k] == -1) {
            if (odd_update) {
                battle_event(world, BattleEventKind::Sound, line_bank, line_cue);
            }
        } else if ((elapsed.raw >> fx_shift) >= table.beeps[k]) {
            battle_event(world, BattleEventKind::Sound, line_bank, line_cue);
            scene.killer += 1;
        }
    }
    if (scene.timer <= fx(2)) {
        scene.timer = fx(2);
        scene.step = 2;
    }
}

void ending_exit(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    scene.timer = fx(1);
    scene.door_wait = Fx{};
    if (scene.state >= scene_ending_last) {
        world.battle_phase = 6; // the front end's ending scene ends
        return;
    }
    scene.state += 1;
    scene.step = 0;
    scene.timer = duration_of(world, scene.state);
}
