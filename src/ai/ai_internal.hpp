#pragma once
// Shared between the AI's files: one update's context and the functions the states call.
#include "ai/ai.hpp"
#include "ai/ai_data.hpp"
#include "battle/fighter_hud.hpp"
#include "world/world.hpp"

// one AI fighter's update: its controller, fighter and HUD, the battle's tables
struct Ai {
    World& world;
    int slot;
    Fighter& f;
    AiController& c;
    FighterHud& hud;
    const AiData& data;
};
Ai ai_context(World& world, int slot);

// ai_update.cpp: levels, rolls, draws
int ai_level(const Ai& ai);                  // ai_level
int ai_draw(Ai& ai);                         // rand_table_next
bool ai_roll(Ai& ai, int chance, int range); // ai_roll
int ai_weighted_pick(Ai& ai);                // ai_weighted_pick on the search weights
// (int)((r / 32767.0f x jitter + 1) x base) for a jitter of tenths (2 = 0.2), one draw
int ai_jittered(Ai& ai, int base, int jitter_tenths);
// actor_role: 2 for opponents 0, 1, 10, 19, else the roster's role, -1 without a rumble
int fighter_role(const World& world, int slot);
void ai_change_state(Ai& ai, int state); // ai_change_state
int ai_choose_next_state(Ai& ai);        // ai_choose_next_state
// set or clear control_buttons bits
void press(Ai& ai, std::uint32_t bits);
void release(Ai& ai, std::uint32_t bits);
bool motion_jumping(const Ai& ai); // motion state 4
bool motion_falling(const Ai& ai); // motion state 5
// the camera's pitch and heading in the original's sense, radians
Fx camera_pitch_original(const Ai& ai);

// ai_target.cpp
void ai_pick_target(Ai& ai); // ai_pick_target, with the camouflage watch, retaliation, aspect
const HudSense* ai_target_sense(const Ai& ai);

// ai_aim.cpp
// ai_aim_at_target's allow bits: the pitch held as it was; the twist and the legs turned
inline constexpr int aim_hold_pitch = 0x1;
inline constexpr int aim_twist = 0x4;
inline constexpr int aim_legs = 0x8;
void ai_aim_at_target(Ai& ai, int allow);
void ai_aim_at_offset(Ai& ai);               // ai_aim_at_offset
int ai_steer_to_point(Ai& ai, FxVec3 point); // ai_steer_to_point
void ai_evade_buttons(Ai& ai);               // ai_evade_buttons
void ai_dodge_shots(Ai& ai);                 // ai_dodge_shots
// normalize(reticle_center_world - position): where the fighter looks
FxVec3 ai_look_direction(const Ai& ai);

// ai_route.cpp
int ai_route_advance(Ai& ai, int prefer, int avoid); // ai_route_advance
void ai_route_steer(Ai& ai);                         // ai_route_steer
void ai_route_choose_ahead(Ai& ai, bool mirrored);   // ai_route_choose_ahead
// the route advance under the stop-and-go wait, as the follow step and its copies run it
int ai_route_step(Ai& ai, int prefer, int avoid);
int route_kind(const Ai& ai); // the current node's kind, -1 before the first

// ai_weapons.cpp
void ai_fire_pacing_init(Ai& ai);
void ai_weapons_bind(Ai& ai);
void ai_fire_arm(Ai& ai);
void ai_fire_disarm(Ai& ai);
void ai_fire_stop_all(Ai& ai);
void ai_want_fire(Ai& ai);
void ai_weapons_update(Ai& ai);
bool ai_weapon_class6_ready(const Ai& ai);
void ai_weapons_stop_if_empty(Ai& ai); // ai_sense_update's part
void ai_camo_setup(Ai& ai);
void ai_camo_update(Ai& ai);
void ai_stat_bands_apply(Ai& ai);

// ai_states.cpp
void ai_run_handler(Ai& ai);
void ai_special_moves(Ai& ai);
// ai_ranker.cpp
void ai_ranker_setup(Ai& ai);
void ai_ranker_gate(Ai& ai);
// the state-1 timer of retail's address bug (state 1, the ranker routes and hold)
std::int16_t ai_address_bug_timer(int slot);
bool ai_run_ranker_handler(Ai& ai); // false when the handler isn't a ranker's
