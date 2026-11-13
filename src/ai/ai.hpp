#pragma once
// The computer fighters (decomp docs/port/ai.md; ai_enter and ai_update in src/od/u_0577a0.c,
// the rest in src/hand/ai_target_track.c). One AiController a slot, a plain value in the world.
// ai_update runs for an AI fighter on even world steps only, as retail's 30 Hz code with every
// constant as retail has it (frame_cadence 30, ticks 2); on odd steps the fighter keeps the last
// record with the press-only bits cleared (docs/sixty.md "AI"). It writes the fighter's
// ControlRecord exactly as retail writes Actor.control_axis and control_buttons.
//
// Fields keep the decomp's names. Retail's shorts stay 16-bit (they wrap where retail's do);
// its floats are Q16.16. Angles the AI computes with are radians in the original's sense.
#include "fx/vec.hpp"
#include "world/control_record.hpp"

#include <array>
#include <cstdint>

// retail's battle frame_cadence: the AI counts 30 updates a second
inline constexpr int ai_cadence = 30;

// What runs this update (AiController.handler): a state's entry installs its update; special
// moves and the ranker code install theirs directly. Dispatched by a switch (ai_states.cpp)
enum class AiHandler : std::uint8_t {
    stand_fire,
    stand_fire_update,
    route_follow,
    route_follow_update,
    facing_pause,
    facing_pause_update,
    combat,
    combat_update,
    combat_lost_target,
    combat_search_update,
    neutral_pause,
    neutral_pause_update,
    back_off,
    back_off_update,
    wander,
    wander_update,
    ram_start,
    ram_jump,
    ram_coast,
    fetch_pickup_start,
    fetch_pickup,
    ranker_mode_aim,
    ranker_mode_aim_update,
    ranker_mode_hold,
    ranker_mode_hold_update,
    ranker_route,
    ranker_route_update,
    ranker_attack,
    ranker_attack_update,
    ranker_strafe,
    ranker_strafe_update,
};

// AiWeapon: one bound weapon, in mount order arm A, arm B, back A, back B (empty mounts skipped)
struct AiWeapon {
    bool bound = false; // record != 0
    int mount = 0;      // fire_flag points at fire[mount]
    bool want_fire = false;
    std::int16_t delay = 0;
    std::int16_t burst_delay = 0;
    std::int16_t armed = 0;
    int behavior = 0;  // record +0x1C
    int lock_rule = 0; // record +0x1E
};

// AiFirePacing, from the level at ai_enter (the level bug: 0 for every quest AI)
struct AiFirePacing {
    int firing = 0;
    int max_firing = 0;
    std::int16_t fire_chance = 0;
    std::int16_t refire_delay = 0;
    std::int16_t rearm_delay = 0;
    std::int16_t burst_delay = 0;
};

// AiRoute: the node, the record (segment) being followed and how far along it
struct AiRoute {
    bool placed = false; // route.point != 0: a node has been chosen once
    int node = 0;        // the id byte (+1) of the node, as retail keeps it
    int current = -1;    // the node's index in the graph
    int point_index = 0; // the segment
    int point_count = 0;
    int progress_steps = 0; // progress in steps of 0.2 (0.05 on kind 7): 5 (20) reach 1 in float
    FxVec3 from;            // the point steered at
};

// AiController.move_flags
inline constexpr int move_ram = 0x2;
inline constexpr int move_crate_chase = 0x4;
inline constexpr int move_ranker_aim = 0x8; // the unreachable ranker's modes
inline constexpr int move_ranker_hold = 0x10;
inline constexpr int move_any = 0x1E;

struct AiController {
    int flags = 0; // 1 a dead side-step latch, 2 sees through camouflage this second
    int state = 0;
    int prev_state = 0;
    int mode = 0;
    int search_move = 0;
    AiHandler handler = AiHandler::stand_fire;
    int path_cache = 0;
    int spare_1c = 0;      // the unreachable state 2's draw lands here
    int spec_level = 0;    // spec[0]: the roster level, written after ai_enter
    int profile = 0;       // spec + 4: the selection profile
    int target = -1;       // slot, -1 none
    int target_sense = -1; // the slot of the hud sense, -1 none
    int target_slot = -1;
    std::int16_t target_memory = 0;
    std::int16_t camo_watch_timer = 0;
    Fx wanted_range; // no reader; its draw is what counts
    Fx axis_forward, axis_leg_turn, axis_pitch, axis_twist;
    Fx target_distance, target_distance_flat;
    bool wants_jump = false;
    int target_aspect = 0;
    int durability_band = 0;
    int last_durability = 0;
    Fx view_yaw, view_pitch; // half-angles of the view cone, radians
    std::array<AiWeapon, 4> weapon{};
    std::array<bool, 4> fire{}; // by mount
    AiFirePacing pacing;
    std::int16_t camo_hold = 0;
    std::int16_t camo_cooldown = 0;
    std::int16_t camo_cooldown_base = 0;
    std::int16_t camo_chance = 0;
    Fx camo_fraction;
    bool out_of_ammo = false;
    std::int16_t band_button1_chance = 0;
    std::int16_t band_jump_chance = 0;
    std::int16_t band_dodge_chance = 0;
    Fx jump_start_y;
    int node_kind = 3;
    int age_frames = 0;
    std::int16_t state_timer = 0;
    std::int16_t ranker_timer = 0;
    std::int16_t reeval_timer = 0;
    std::int16_t frames_ticking = 0;
    std::int16_t sub_timer = 0;
    std::int16_t route_wait = 0;
    std::int16_t frames_since_jump = 0;
    std::int16_t frames_since_backup = 0;
    std::int16_t dodge_cooldown = 0;
    std::int16_t route_stuck_frames = 0;
    int frames_since_player_hit = 0;
    int player_last_durability = 0;
    int move_flags = 0; // move_* bits
    int free_weapon_classes = 0;
    int pickup = -1;
    std::int16_t pickup_frames = 0;
    std::int16_t attack_frames = 0;
    std::int16_t attack_jumps = 0;
    std::int16_t class5_fire_frames = 0;
    std::int16_t class5_fire_hold = 0;
    std::int16_t strafe_chance = 0;
    bool enraged = false;
    bool strafe_dir = false;
    bool use_camo = false;
    bool camo_when_shot = false;
    bool camo_held = false;
    AiRoute route;

    // the port's: the record held over odd steps and the even step's contact flags, OR'd with
    // the odd step's where the AI reads them
    ControlRecord record;
    int contact_flags_even = 0;
};

struct World;
// fighter_spawn's AI part: ai_enter (on a zeroed controller, so it sees level 0 unless
// the battle is a demo) then ai_controller_assign with the roster's level and profile
void ai_spawn(World& world, int slot, int level, int profile);
// fighter_task_update's input for an AI: ai_update on even steps, the held record on odd ones
void ai_fighter_input(World& world, int slot);
// after the fighter's step: what the next AI update reads of this one
void ai_after_step(World& world, int slot);
