// Free play's lessons (decomp src/hand/u_088880.c scene_tutorial_18_enter ..
// scene_tutorial_2e_exit, tutorial_say, tutorial_freeze_others,
// weapons_refill_empty; docs/port/battle.md "Who fights where"). Each lesson is a chain of
// scene states: the controls locked, a line of text, then one thing to do with only the buttons
// it needs, a line when it's done, and on once the line has ended. Lesson 0 (0x18..0x22, or
// 0x2D/0x2E under the Standard control type) moves, turns, runs, jumps, steps and twists; lesson
// 1 (0x23..0x29) shoots down 3, 3, 3, 3 and 10 of the drones with each weapon in turn; lesson 2
// (0x2A..0x2C) hides with the camouflage from the second fighter. The last state sets phase 6,
// which ends free play.
//
// The text is the battle's scene text: the world asks for it and holds the lesson until
// presentation says it has ended (Inputs::scene_text_done); a table entry with no script asks
// nothing (BattleData::scene_text_scripts). Retail's scene camera switch to view 1 (0x18, 0x23,
// 0x2A and 0x2C's enters) is kept as a cut to the chase view (FighterCamera::view).
//
// At 60 Hz the counters sum per-step velocities and turns, which halve, so the distances and
// turns compare as retail's; the dodge counts are clip changes out of a dodge, one a step at
// most as one an update (docs/sixty.md).
#include "ai/ai_links.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_flow.hpp"
#include "mech/fighter_rates.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdint>

namespace {

// tutorial_say's tables of script pointers by scene state and column: D_00243080, and D_00243138
// under the Standard control type
constexpr int say_table_preferred = 0x243080;
constexpr int say_table_standard = 0x243138;
constexpr std::int32_t quarter_pi_raw = 1 << 29; // pi/4 as a binary angle

FighterScene& scene_of(World& world, const Fighter& f) {
    return world.battle_state.slots[f.slot].scene;
}

// the line for this state's column. An empty
// table entry starts nothing (a line still running goes on), as retail
// decomp: tutorial_say 0x91F0F, scene_text_start 0x91EA9
void say(World& world, const Fighter& f, int column) {
    BattleState& b = world.battle_state;
    const int table = f.control_type & 1 ? say_table_standard : say_table_preferred;
    const int address = table + scene_of(world, f).state * 8 + column * 4;
    const std::vector<int>& scripts = world.battle->scene_text_scripts;
    if (!scripts.empty() && !std::binary_search(scripts.begin(), scripts.end(), address)) {
        return;
    }
    b.scene_text = address;
    b.scene_text_serial += 1;
    b.scene_text_alive = true;
}

bool text_alive(const World& world) {
    return world.battle_state.scene_text_alive;
}

// every AI frozen (its controls cleared) or let go
// decomp: tutorial_freeze_others 0x91F74
void freeze_others(World& world, bool on) {
    for (Fighter& other : world.fighters) {
        if (!other.present || other.controller != -1) {
            continue;
        }
        world.battle_state.slots[other.slot].ai_frozen = !on;
        if (!on) {
            other.control = ControlRecord{};
            world.ai[static_cast<std::size_t>(other.slot)].record = ControlRecord{};
        }
    }
}

// an empty weapon full again, with its sound
// decomp: weapons_refill_empty 0x92064
void refill_empty(World& world, Fighter& f) {
    for (Weapon& w : f.combat.weapons) {
        if (w.resource <= Fx{}) {
            w.resource = fx(w.resource_max);
            stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                        se_item_pickup);
        }
    }
}

// the common enter: locked; once the last line has ended, the controls the step needs
void enter_open(World& world, Fighter& f, std::uint32_t allow, const bool open[4],
                bool reset_killer = true) {
    FighterScene& s = scene_of(world, f);
    control_lock(f);
    if (text_alive(world)) {
        return;
    }
    for (int k = 0; k < 4; ++k) {
        if (open[k]) {
            f.axis_zero[k] = false;
        }
    }
    f.control_allow = allow;
    s.step = 1;
    if (reset_killer) {
        s.killer = 0;
    }
    s.progress[0] = s.progress[1] = Fx{};
}

// the common exit: once the line has ended, the next state and its first line
void exit_to(World& world, Fighter& f, int next, bool line = true) {
    FighterScene& s = scene_of(world, f);
    if (text_alive(world)) {
        return;
    }
    s.state = next;
    s.step = 0;
    s.killer = 0;
    if (line) {
        say(world, f, 0);
    }
}

// a step done: locked, the closing line
void done(World& world, Fighter& f) {
    scene_of(world, f).step = 2;
    control_lock(f);
    say(world, f, 1);
}

// the shooting steps 0x24..0x28: every axis, the buttons of one weapon (and the moves)
void shoot_enter(World& world, Fighter& f, std::uint32_t allow, bool kills_first) {
    FighterScene& s = scene_of(world, f);
    control_lock(f);
    if (kills_first) {
        f.combat.kills = 0;
    }
    freeze_others(world, false);
    if (!text_alive(world)) {
        for (bool& z : f.axis_zero) {
            z = false;
        }
        f.control_allow = allow;
        s.step = 1;
        s.killer = f.combat.kills;
    }
    if (!kills_first) {
        f.combat.kills = 0;
    }
}

const bool open_forward[4] = {true, false, false, false};
const bool open_turn[4] = {false, true, false, false};
const bool open_move[4] = {true, true, false, false};
const bool open_twist[4] = {false, false, false, true};
const bool open_none[4] = {false, false, false, false};

} // namespace

void battle_free_play_refill(World& world, Fighter& f) {
    refill_empty(world, f);
}

void battle_scene_text_done(World& world, int serial) {
    BattleState& b = world.battle_state;
    if (b.scene_text_alive && serial == b.scene_text_serial) {
        b.scene_text_alive = false;
    }
}

void battle_tutorial_step(World& world, Fighter& f) {
    FighterScene& s = scene_of(world, f);
    const int state = s.state;
    const int step = s.step;
    switch (state) {
    case scene_lesson_hello: // the first line
        if (step == 0) {
            control_lock(f);
            camera_view_cut(f.camera, 1); // the chase view, a cut
            s.step = 1;
        } else if (step == 1) {
            say(world, f, 0);
            s.step = 2;
        } else {
            exit_to(world, f, scene_lesson_forward_back);
        }
        break;
    case scene_lesson_forward_back: // forward and back, the stick held to 0.7
        if (step == 0) {
            enter_open(world, f, 0, open_forward);
        } else if (step == 1) {
            f.tutorial_clamp = 1;
        } else {
            exit_to(world, f, f.control_type & 1 ? scene_lesson_strafe_a : scene_lesson_turn_a);
        }
        break;
    case scene_lesson_turn_a: // a full turn one way
    case scene_lesson_turn_b: // and the other
        if (step == 0) {
            enter_open(world, f, 0, open_turn);
        } else if (step == 1) {
            f.tutorial_clamp = state == scene_lesson_turn_a ? 2 : 3;
        } else {
            exit_to(world, f, state + 1);
        }
        break;
    case scene_lesson_run: // run (dash)
        if (step == 0) {
            enter_open(world, f, control_dash, open_move);
        } else if (step == 2) {
            exit_to(world, f, scene_lesson_jump);
        }
        break;
    case scene_lesson_jump: // jump
        if (step == 0) {
            enter_open(world, f, control_dash | control_jump_pressed, open_move);
        } else if (step == 2) {
            exit_to(world, f, scene_lesson_jump_high);
        }
        break;
    case scene_lesson_jump_high: // jump high (the killer isn't reset here)
        if (step == 0) {
            enter_open(world, f, control_dash | control_jump_held | control_jump_pressed, open_move,
                       false);
        } else if (step == 2) {
            exit_to(world, f, scene_lesson_side_steps);
        }
        break;
    case scene_lesson_side_steps: // a step to each side
        if (step == 0) {
            enter_open(world, f, control_moves, open_move);
        } else if (step == 2) {
            exit_to(world, f, f.control_type & 1 ? scene_lesson_moves_end : scene_lesson_twist);
        }
        break;
    case scene_lesson_twist: // twist each way
        if (step == 0) {
            enter_open(world, f, 0, open_twist);
        } else if (step == 2) {
            exit_to(world, f, scene_lesson_recentre);
        }
        break;
    case scene_lesson_recentre: // recentre
        if (step == 0) {
            enter_open(world, f, control_recentre, open_none);
        } else if (step == 2) {
            exit_to(world, f, scene_lesson_moves_end);
        }
        break;
    case scene_lesson_moves_end: // the last line, then the end
    case scene_lesson_weapons_end:
    case scene_lesson_camo_end:
        if (step == 0) {
            if (state != scene_lesson_moves_end) {
                control_lock(f);
                freeze_others(world, false);
            }
            if (state == scene_lesson_camo_end) {
                camera_view_cut(f.camera, 1);
            }
            s.step = 1;
            s.killer = 0;
        } else if (step == 1) {
            if (state != scene_lesson_moves_end) {
                freeze_others(world, false);
            }
            if (!text_alive(world)) {
                s.step = 2;
                s.killer = 0;
            }
        } else {
            world.battle_phase = 6;
        }
        break;
    case scene_lesson_weapons_hello: // lesson 1's first line
    case scene_lesson_camo_hello:    // lesson 2's
        if (step == 0) {
            control_lock(f);
            camera_view_cut(f.camera, 1);
            s.step = 1;
            s.killer = 0;
            freeze_others(world, false);
        } else if (step == 1) {
            freeze_others(world, false);
            say(world, f, 0);
            s.step = 2;
        } else {
            freeze_others(world, false);
            exit_to(world, f, state + 1, state == scene_lesson_weapons_hello);
        }
        break;
    case scene_lesson_arm_a: // each weapon: 3 drones each, then 10 with all of them
                             // (-0x3F81 .. -0x3C01)
    case scene_lesson_arm_a + 1:
    case scene_lesson_arm_a + 2:
    case scene_lesson_arm_a + 3:
    case scene_lesson_all_weapons: {
        const std::uint32_t allows[5] = {
            lesson_allow | control_fire_arm_a, lesson_allow | control_fire_arm_b,
            lesson_allow | control_fire_back_a, lesson_allow | control_fire_back_b,
            lesson_allow | control_fire_all};
        if (step == 0) {
            shoot_enter(world, f, allows[state - scene_lesson_arm_a], state != scene_lesson_arm_a);
        } else if (step == 2) {
            freeze_others(world, false);
            exit_to(world, f, state + 1);
        }
        break;
    }
    case scene_lesson_camo_hide: // hide from the second fighter, set 30 ahead facing back (the
                                 // port's z is negated)
        if (step == 0) {
            control_lock(f);
            if (world.fighters[1].present) {
                Fighter& other = world.fighters[1];
                other.position = f.position;
                other.position.z += fx(30);
                other.heading = Angle{f.heading.raw + 0x80000000u};
                other.combat.damage.immune_frames = Fx{};
            }
            freeze_others(world, false);
            f.control_allow = control_camouflage;
            s.step = 1;
        } else if (step == 2) {
            freeze_others(world, false);
            exit_to(world, f, scene_lesson_camo_end);
        }
        break;
    case scene_lesson_strafe_a: // the Standard type's twist (its strafe) one way, then the other
    case scene_lesson_strafe_b:
        if (step == 0) {
            enter_open(world, f, 0, open_twist);
        } else if (step == 1) {
            f.tutorial_clamp = state == scene_lesson_strafe_a ? 4 : 5;
        } else {
            exit_to(world, f,
                    state == scene_lesson_strafe_a ? scene_lesson_strafe_b : scene_lesson_turn_a);
        }
        break;
    default:
        break;
    }
}

void battle_tutorial_after(World& world, Fighter& f, const TutorialBefore& before) {
    FighterScene& s = scene_of(world, f);
    if (s.state != before.state || before.step != 1 || s.step != 1) {
        return;
    }
    // the step's move along the fighter's facing (its velocity through the inverse rotation:
    // the port's walk moves by root motion with no velocity, so the step's displacement)
    const FxVec3 moved = f.position - before.position;
    const Fx along = fx_mul_near(moved.x, fx_sin(f.heading)) + fx_mul_near(moved.z, fx_cos(f.heading));
    // the turn this step in the original's sense (the port's headings are negated)
    const std::int32_t turned = static_cast<std::int32_t>(before.heading.raw - f.heading.raw);
    const Fx two_pi = angle_radians(Angle{0x7fffffffu}) * 2;
    switch (s.state) {
    case scene_lesson_forward_back:
        if (f.motion_state == state_walk || f.motion_state == state_walk_back) {
            if (along >= Fx{}) {
                s.progress[0] += along;
            } else {
                s.progress[1] -= along;
            }
        }
        if (s.progress[0] > fx(15) && s.progress[1] > fx(15)) {
            done(world, f);
        }
        break;
    case scene_lesson_turn_a:
    case scene_lesson_turn_b: {
        // 0x1A sums the turn wrapped to 0..2 pi, 0x1B to -2 pi..0, while turning in place
        Fx d = angle_radians(Angle{static_cast<std::uint32_t>(turned)});
        if (turned < 0) {
            d = -angle_radians(Angle{static_cast<std::uint32_t>(-turned)});
        }
        if (s.state == scene_lesson_turn_a && d < Fx{}) {
            d += two_pi;
        } else if (s.state == scene_lesson_turn_b && d > Fx{}) {
            d -= two_pi;
        }
        if (f.motion_state == state_turn) {
            s.progress[1] += d;
        }
        if ((s.state == scene_lesson_turn_a && s.progress[1] >= two_pi) ||
            (s.state == scene_lesson_turn_b && s.progress[1] <= -two_pi)) {
            done(world, f);
        }
        break;
    }
    case scene_lesson_run:
        if (f.motion_state == state_run || f.motion_state == state_run + 1) {
            s.progress[0] += fx_abs(along);
        }
        if (s.progress[0] >= fx(80)) {
            done(world, f);
        }
        break;
    case scene_lesson_jump:
        if (!f.grounded && f.motion_state == state_jump) {
            s.progress[0] = fx(15);
        } else if (s.progress[0] != Fx{}) {
            done(world, f);
        }
        break;
    case scene_lesson_jump_high:
        if (f.position.y > s.progress[1]) {
            s.progress[1] = f.position.y;
        }
        if (!f.grounded) {
            s.progress[0] = fx(15);
        } else if (s.progress[0] != Fx{} && s.progress[1] > fx(8)) {
            done(world, f);
        } else {
            s.progress[0] = Fx{};
        }
        break;
    case scene_lesson_side_steps:
        // scene_tutorial_1f_update: a count each update whose motion_slot_prev is a
        // dodge, the slot a clip started this update took over from (actor_frame_reset clears
        // it), so one a dodge, when it ends: two dodges each way. One clip change a step at
        // 60 Hz as at 30, so the counts are retail's
        if (f.motion_slot_prev == slot_dodge_right || f.motion_slot_prev == slot_dodge_right + 2) {
            s.progress[0] += fx(1);
        } else if (f.motion_slot_prev == slot_dodge_left ||
                   f.motion_slot_prev == slot_dodge_left + 2) {
            s.progress[1] += fx(1);
        }
        if (s.progress[0] >= fx(2) && s.progress[1] >= fx(2)) {
            done(world, f);
        }
        break;
    case scene_lesson_twist: {
        const std::int32_t aim = static_cast<std::int32_t>(f.aim_yaw.raw);
        if (aim >= quarter_pi_raw) {
            s.progress[0] = fx(1);
        } else if (aim <= -quarter_pi_raw) {
            s.progress[1] = fx(1);
        }
        if (s.progress[0] >= fx(1) && s.progress[1] >= fx(1)) {
            done(world, f);
        }
        break;
    }
    case scene_lesson_recentre:
        if (f.aim_yaw.raw == 0) {
            done(world, f);
        }
        break;
    case scene_lesson_arm_a:
    case scene_lesson_arm_a + 1:
    case scene_lesson_arm_a + 2:
    case scene_lesson_arm_a + 3:
    case scene_lesson_all_weapons:
        refill_empty(world, f);
        freeze_others(world, true);
        if (f.combat.kills - s.killer >= (s.state == scene_lesson_all_weapons ? 10 : 3)) {
            done(world, f);
        }
        break;
    case scene_lesson_camo_hide:
        freeze_others(world, false);
        if (f.control.control_buttons & control_camouflage) {
            f.control_allow = 0;
        }
        // hidden (camo_update's state 4, as the last step left it) ends the step
        if (f.camo.state == camo_hidden) {
            s.killer = 0;
            done(world, f);
        }
        break;
    case scene_lesson_strafe_a:
    case scene_lesson_strafe_b:
        s.progress[1] += length(moved);
        if (s.progress[1] >= fx(15)) {
            done(world, f);
        }
        break;
    default:
        break;
    }
}
