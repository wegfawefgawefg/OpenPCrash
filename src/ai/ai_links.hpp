#pragma once
// What the AI and the HUD senses read from systems that aren't in the world yet (damage,
// weapons and shots, camouflage, the supply craft's crates, the lock-on HUD). Each is a small
// function here with the retail field it stands for; ai_links.cpp answers them with what an
// undamaged, unarmed world has until those systems merge, so wiring them is one file.
#include "fx/vec.hpp"

#include <array>
#include <cstdint>

struct World;
struct Fighter;

// Actor.durability, durability_max: damage isn't in the world, every fighter is whole
int fighter_durability(const World& world, int slot);
int fighter_durability_max(const World& world, int slot);
// Actor.alive and running (fighter_slot_running): present and durability over 0
bool fighter_alive(const World& world, int slot);
// Actor.aim_point (+0x454C): actor_settle_damage sets it to damage.center, the position
// plus a radius up, at the end of every step
FxVec3 fighter_aim_point(const Fighter& f);
// DamageState.pending[attacker] > 0: retail has the hits since the last actor_settle_damage, one
// update; the port settles every step, so this must answer for both steps since the AI last ran
// (docs/sixty.md "AI")
bool fighter_hit_pending(const World& world, int slot, int attacker);

// the weapon on a mount 0..3 (battle_slot_weapon): its resource (ammo, the laser's gauge) and the
// range weapon_reach_update works out each update (Weapon.reach_min, reach_max)
Fx weapon_resource(const World& world, int slot, int mount);
void weapon_reach(const World& world, int slot, int mount, Fx* reach_min, Fx* reach_max);
// Actor.triggers[mount].pressed, the arm-raise latch (actor_triggers_update); the AI
// clears all four every update (ai_sense_update)
bool trigger_pressed(const World& world, int slot, int mount);
void triggers_clear_pressed(World& world, int slot);

// Actor.camo_state (camo_update, mech/fighter_camo.hpp): 11 without the option; camo_max
// in ticks
int camo_state(const World& world, int slot);
int camo_max(const World& world, int slot);
// camo_is_state_1_to_4: fading in or hidden
bool camo_hides(const World& world, int slot);

// The shot pass's threats against one fighter (Battle.threats[slot], ShotThreats at Battle
// +0xEE008, rebuilt by battle_shots_update): every live shot of another fighter, in
// shot-pool order, teammates' included. A beam (behaviour 5) at any range; otherwise only within
// 5 units (a squared length against 25): a blade (6) in melee, a behaviour-1 shot whose target is
// this fighter in locked_on_me, anything else in nearby (docs/port/weapons.md)
struct ThreatShot {
    int shot = -1;                 // the pool index, for shot_mark_dodge_rolled
    FxVec3 pos, prev_pos;          // Shot.pos, prev_pos
    FxVec3 velocity;               // Shot.velocity, per tick
    int owner = -1;                // the firing weapon's owner slot (beams)
    std::uint16_t dodge_rolled = 0; // Shot.dodge_rolled, a bit a slot
};
struct ShotThreats {
    std::array<ThreatShot, 9> locked_on_me{};
    int locked_on_me_count = 0;
    std::array<ThreatShot, 29> nearby{};
    int nearby_count = 0;
    std::array<ThreatShot, 4> melee{};
    int melee_count = 0;
    std::array<ThreatShot, 4> beams{};
    int beam_count = 0;
};
// the threats as the last shot pass left them; false when there's no list (retail's null)
bool world_shot_threats(const World& world, int slot, ShotThreats* out);
// ai_dodge_shots sets a blade's bit for each AI that considered it
void shot_mark_dodge_rolled(World& world, int shot, int slot);

// The supply craft's crates (pickups_draw): pickup_find_nearest within range,
// -1 for none; a crate's position, state (+0x14) and kind (+0x18)
int ai_pickup_nearest(const World& world, FxVec3 position, int slot, Fx range);
FxVec3 pickup_position(const World& world, int pickup);
int pickup_state(const World& world, int pickup);
int pickup_kind(const World& world, int pickup);

// FighterHud.reticle_a (reticle_box_size from the chip's reticle kind and size): the half box
// sense_in_reticle tests the reticle's target against. Zero until the lock HUD is in the world
void reticle_box(const World& world, int slot, Fx* half_x, Fx* half_y);

// the tournament rule tier ai_camo_reaction_chance reads (tournament_rule_tier of the
// area's tournament), -1 without a tournament
int tournament_rule_tier_now(const World& world);

// the battle's roster entry of a slot: retail's rumble context of the selected area
// (rumble_context_by_selector), world.rumble[day_slot_of_selector]
struct RosterEntry;
const RosterEntry& battle_roster_entry(const World& world, int slot);
RosterEntry& battle_roster_entry(World& world, int slot);
