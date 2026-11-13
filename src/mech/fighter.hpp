#pragma once
// A fighter in the world: the part of the decomp's Actor (include/actor.h) that movement, the
// camera and the input read and write, in Q16.16 in the port's frame. Field names are the
// decomp's.
//
// The frame: positions and the local vectors the original turns by the heading have z negated
// (docs/assets.md), and every angle about y or x is negated with it, so where the original does
// `heading += a` the port does `heading -= a`, and `v * RotationY(heading)` reads the same with
// both sides mirrored. Forward is +z turned by the heading: (sin h, 0, cos h).
#include "battle/battle_data.hpp"
#include "fx/angle.hpp"
#include "fx/vec.hpp"
#include "mech/barks.hpp"
#include "mech/fighter_camo.hpp"
#include "mech/fighter_pose.hpp"
#include "weapons/combat.hpp"
#include "world/control_record.hpp"
#include "world/control_route.hpp"
#include "world/pad_record.hpp"

#include <cstdint>
#include <initializer_list>

struct StageWorld;

// actor_cameras[slot] (Actor.camera), the parts the chase view and the aim use
struct FighterCamera {
    // 0 cockpit, 1 chase, 2 side, 3 side high, 4 front, 5 the stage's rails; view_next the one a
    // near/far switch blends to over view_switch_frames (40 steps, retail's 20 updates)
    int view = 1;
    int view_next = 1;
    int view_switch_frames = 0;
    int rail_repick_clock = 0; // view 5's re-pick, 0..59 (retail 0..29 updates)
    int rail_point = 0;
    Angle view_yaw;   // follows heading + aim_yaw
    Angle view_pitch; // follows pitch
    Angle pitch;      // the stick's, port sense (the original's negated: positive looks down)
    Fx pitch_stick;   // smoothed |pitch axis|, widens the pitch limit
    int recenter_frames = 0;
    Angle recenter_pitch;
    bool auto_turning = false;
    Angle auto_turn_delta;
    // beginner mode's quick turn (Actor +0x2744 the lock, +0x2748 the turn left, port sense)
    int quick_turn_lock = 0; // steps
    Angle quick_turn_left;
    FxVec3 head_offset; // head_position - position (the body node, from the pose)
    FxVec3 eye;
    FxVec3 target;
    // the hit shake in the look-at (camera_view0_cockpit's cockpit_shake_x / y, camera_eye_update's
    // camera_shake_up): how far it moved target this step (0 when none reached it) and the up
    // the fighter's view matrix takes, (0, 1, 0) or the shake's tilt (camera_views.cpp)
    FxVec3 shake_shift;
    FxVec3 up{Fx{}, fx(1), Fx{}};
};

struct Fighter {
    bool present = false;
    int slot = 0;
    int controller = 0; // the pad player, -1 an AI
    int control_type = 0;
    ControlBindings bindings;
    StickFlick flick;
    ControlRecord control;           // this step's record (Actor.control_axis, control_buttons)
    // free play (menu state 0x2A) only, actor_input_restrict: axes held at zero and the
    // buttons masked, set by the lessons' scenes (Actor +0x2F70..+0x2F74); tutorial_clamp is the
    // lesson's one-way rule on an axis for this step, applied before the motion states
    bool restrict_input = false;
    // anything held on the fighter's own pad this step (func_000DF1C0: buttons or a stick's
    // directions; the PC's mouse aim too), which the idle bark's clock reads. An AI has no pad:
    // retail reads PADREC[-1] there, taken as nothing held (docs/bugs.md)
    bool pad_held = false;
    bool axis_zero[4] = {};
    std::uint32_t control_allow = 0;
    int tutorial_clamp = 0;

    // the build (Scoobee) and what the performance pass made of it, once at spawn
    int build = 0;
    int leg_class = 0;
    int body_class = 0;
    int leg_kind = 0; // leg_kind_group
    Fx radius;
    Fx weight;
    Fx walk_rate, run_rate; // units per tick, or clip ticks per tick with root motion
    bool walk_root_motion = false, run_root_motion = false;
    Fx walk_scale; // sv_geometry_response_table's walk column: walk_rate times it is units a tick
    Fx jump_height;
    Fx jump_speed_30, jump_thrust_30; // per update, as retail computes them (sixty.md "Jump")
    Fx jump_speed;                    // per step: half of retail's
    Angle turn_step;                  // turn_deg as an angle a tick at full stick
    Angle air_turn_step;
    Fx dodge_speed_ground, dodge_speed_air; // units per tick
    Fx lunge_speed_max;                     // per step
    Fx leg_landing, leg_knockback_resist;   // percent
    Fx slide_decay;                         // per step: sqrt of the leg's per-update factor
    Fx skid_start, skid_stop;               // sixty.md R13: start scale and stop threshold
    // attachment 13's height above the position (the upper contact sphere, the chase camera),
    // from the last step's pose
    Fx head_height;

    // where it is and how it moves
    FxVec3 position, position_prev;
    FxVec3 velocity, velocity_prev;
    FxVec3 force_dv; // this step's velocity change from forces (sixty.md R10)
    FxVec3 push;     // fighters_separate's (none yet)
    FxVec3 impulse;  // DamageState.impulse, knockback (none yet)
    FxVec3 takeoff_velocity, skid_velocity;
    FxVec3 ground_velocity;      // Actor +0xE0: the velocity after the integrate, before contact
    FxVec3 reticle_center_world; // the reticle's point, 64 ahead along the aim (hud_senses.cpp)
    FxVec3 dodge_impulse;        // body space
    Angle heading, heading_prev;
    Angle aim_yaw, aim_yaw_recenter;
    Fx turn_rate; // radians, the original's sense (the pose's lean)
    Fx run_lean;
    bool grounded = false, airborne = false;
    bool dodge_ready = false, dodge_sliding = false, dive_used = false, jump_cut = false;
    bool at_speed = false, reverse = false, skidding = false, landed = false;
    Fx airborne_time;
    Fx jump_thrust_left_30;   // the budget in retail units (sixty.md "Jump")
    Fx jump_step;             // this step's
    Fx pair_jump_step;        // the step a pair charged, for its second step
    bool pair_thrust = false; // the pair's first step thrust
    std::int64_t liftoff_step = 0;
    int air_control_hold = 0; // steps left of the lift-off update's no-air-control
    Fx intangible_frames;     // ticks; shots pass while it is over 0
    int contact_flags = 0;

    // the motion state machine
    int motion_state = 11;
    int motion_mode = 0;
    int motion_slot_a = 28; // the clip slot playing
    // the slot a clip started this step took over from, -1 when none started (motion_slot_prev
    // +0x138: actor_frame_reset clears it, motion_set_animation writes it)
    int motion_slot_prev = -1;
    Fx motion_phase, motion_phase_prev;
    MotionTiming timing;
    Fx motion_time;

    FighterCamera camera;
    int age_frames = 0;
    // in a battle scene this step (src/battle/battle_flow.hpp): the scene moved the fighter in
    // place of the motion states (actor_motion_or_scene)
    bool scene_motion = false;

    // weapons, damage and the HUD that aims them (src/weapons, docs/port/weapons.md)
    FighterCombat combat;
    Camo camo; // the optical camouflage (mech/fighter_camo.hpp)
    // the crates this step's sphere query collected (pickup_collect_test from sv_contact_route):
    // kind -1 none; pickup_apply's fighter side runs after the step (stage/pickup_apply.cpp)
    int crate_kind[3] = {-1, -1, -1};
    FxVec3 crate_at[3];
    // the pose (mech/fighter_pose.hpp), built at the end of each step. It reads what the triggers
    // and the HUD wrote: combat.triggers[i].raise (the arms' raise), combat.mount_free_aim,
    // combat.reticle_track_world with reticle_center_world above (the aim points) and the back
    // weapons' elevation; it writes the back weapons' launch_pitch
    FighterPoseState pose;
    int pose_blend_steps = -1; // steps since pose_blend was set to 1, -1 none
    int motion_events = 0;     // fx.motion_events: motion_event_* bits
    Fx node_frame_400, node_frame_800, node_frame_2000; // the body set's phases
    Angle wheel_angle;                                  // the original's sense
    // the bark kinds asked for this step, a bit each (actor_bark's requests; the speaker
    // rule, the chance and the pick are presentation's, src/hud/hud_barks.cpp); cleared by
    // step_world
    std::uint64_t barks = 0;
};

// decomp: actor_controls_lock 0x91FF0
inline void control_lock(Fighter& f) {
    f.axis_zero[0] = f.axis_zero[1] = f.axis_zero[2] = f.axis_zero[3] = true;
    f.control_allow = 0;
}
// decomp: actor_controls_unlock 0x9202A
inline void control_unlock(Fighter& f) {
    f.axis_zero[0] = f.axis_zero[1] = f.axis_zero[2] = f.axis_zero[3] = false;
    f.control_allow = 0xffffffff;
}

// actor_bark's request: a Bark kind
// decomp: actor_bark 0x493F0
inline void fighter_bark(Fighter& f, int kind) {
    if (kind >= 0 && kind < bark_kinds) {
        f.barks |= std::uint64_t{1} << kind;
    }
}

// fighter_spawn and actor_init for a build row at an entry point
void fighter_spawn(Fighter& fighter, const BattleData& data, int slot, int controller, int build,
                   const EntryPoint& entry);
// the same for a build given as a record with its tunes (a roster entry's Scoobee); `build` is
// still the row it is posed and drawn as (the rig)
void fighter_spawn_record(Fighter& fighter, const BattleData& data, int slot, int controller,
                          const BuildRecord& row, int build, const EntryPoint& entry);
// fighter_task_update's input and actor_step's movement part, one 60 Hz step
// fighter_task_update's input and actor_step's movement part, one 60 Hz step;
// `blocked` zeroes a human's control record (a VS round decided: sv_control_route); `stand` is
// battle_phase 5 (a model stand: the weapon clips and the backs' aim skipped); `single_view` the
// battle has one view (Battle.view_count), which the cockpit view needs
void fighter_step(Fighter& fighter, const BattleData& data, const PadRecord& pad,
                  const MouseAim& aim, std::int64_t step, bool blocked = false,
                  StageWorld* pickups = nullptr, bool stand = false, bool single_view = true);

// fighter_motion.cpp
void motion_set_animation(Fighter& f, const BattleData& data, int clip, int state, int mode,
                          Fx phase);
void motion_dispatch(Fighter& f, const BattleData& data, std::int64_t step);
void fighter_turn_by_stick(Fighter& f);
// motion_root_delta: the root track's move since the last phase, local
FxVec3 motion_root_delta(const Fighter& f, const BattleData& data);
// fighter_hurt.cpp: the stagger (7), the melee swing (9) and the dead (10) states
void motion_state_stagger(Fighter& f, const BattleData& data);
void motion_state_melee(Fighter& f, const BattleData& data);
void motion_state_dead(Fighter& f, const BattleData& data);
// fighter_air.cpp
void motion_state_jump(Fighter& f, const BattleData& data, std::int64_t step);
void motion_state_fall(Fighter& f, const BattleData& data, std::int64_t step);
void motion_state_dodge(Fighter& f, const BattleData& data, std::int64_t step);
void fighter_slide_step(Fighter& f);
bool fighter_try_dodge(Fighter& f, const BattleData& data);
// fighter_contact.cpp; pickups the stage whose supply craft and crates the sphere meets (null
// none)
void contact_route(Fighter& f, const BattleData& data, StageWorld* pickups);
// battle_camera.cpp
void fighter_camera_input(Fighter& f);
// the follows, the head offset, then the view's eye and target; stage the props view 5's rail
// choice looks past (null none)
void fighter_camera_update(Fighter& f, const BattleData& data, const StageWorld* stage = nullptr);
// camera_views.cpp: the d-pad's view switch (actor_camera_input's first part; the cockpit only in
// one view), the views' eye and target into camera.eye and camera.target (camera_eye_update),
// and a cut to a view (the lessons' scenes, the ending)
void camera_view_input(Fighter& f, bool single_view);
void camera_eye_update(Fighter& f, const BattleData& data, const StageWorld* stage);
void camera_view_cut(FighterCamera& camera, int view);
// view 0's, or views 1..4's, eye and target for a fighter, for presentation's replay camera (the
// chase smoothed from camera.eye); changes nothing
void cockpit_camera_of(const Fighter& f, const BattleData& data, FxVec3& eye, FxVec3& target);
void fighter_view_of(const Fighter& f, const BattleData& data, int view, FxVec3& eye,
                     FxVec3& target);
// camera_view0_cockpit's move of the cockpit target by the hit shake (cockpit_shake_x / y turned
// by the view), none when the shake doesn't reach the fighter; changes nothing
FxVec3 cockpit_shake_of(const Fighter& f, const StageWorld* stage);

// local (body) vector to world: v * RotationY(heading)
FxVec3 turned_by(FxVec3 local, Angle heading);
// this step's control axis
inline Fx axis(const Fighter& f, int index) {
    return f.control.control_axis[index];
}
inline bool state_in(int state, std::initializer_list<int> states) {
    for (int s : states) {
        if (state == s) {
            return true;
        }
    }
    return false;
}
