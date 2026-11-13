#pragma once
// The weapon functions step_world calls, in the original's order (docs/port/weapons.md, "One
// battle update, as weapons see it"):
//   fighter phase, slot order: actor_triggers_update inside fighter_step, the pose at its end
//     with actor_back_weapons_place (mech/pose_weapons.cpp: launch_pitch), then
//     actor_settle_damage (combat_fighter_late);
//   the shot pass: battle_shots_update;
//   the weapon phase, slot order: hud_update, then arm A, arm B, back A, back B controllers.
#include "weapons/combat.hpp"

struct World;
struct Fighter;
struct BattleData;
struct BuildRecord;

// combat_spawn.cpp: actor_init's durability, damage_options_apply, the chip, and
// fighter_weapons_spawn with the blast weapon
void combat_spawn(Fighter& f, const BattleData& data, const BuildRecord& build);
// the weapon a shot or ref names: slot * 5 + mount, mount 4 the slot's blast weapon
Weapon& weapon_at(World& world, int ref);
const Weapon& weapon_at(const World& world, int ref);
inline int weapon_ref(int slot, int mount) {
    return slot * 5 + mount;
}
inline int ref_slot(int ref) {
    return ref / 5;
}
// Where a mount's barrel is and points, before weapon_muzzle_matrix's re-aim and stage test:
// weapon_attach_matrix's muzzle point `barrel` under its node of the pose's weapon model
// (the arms raised toward last step's reticle points, the backs pitched by launch_pitch), turned
// pi about y (mech/fighter_pose.hpp); false when the mount has no such barrel or node
bool fighter_muzzle(const World& world, const Fighter& f, int mount, int barrel, FxMat4* out);
// weapon_muzzle_matrix without the barrel step or the flash: the weapon's current barrel
// from the pose, re-aimed and stage-tested as below (the effects follow the muzzle by it)
bool weapon_muzzle_at(const World& world, const Fighter& f, const Weapon& w, FxMat4* out);
// weapon_muzzle_matrix: the muzzle, a back weapon with lock rule 0 or 1 re-aimed at the
// reticle point; false (and no shot) when the stage lies between the chest and the muzzle
bool weapon_muzzle_matrix(World& world, Fighter& f, Weapon& w, FxMat4* out, bool flash,
                          bool step_barrel);
void combat_event(World& world, CombatEventKind kind, int slot, int value, FxVec3 at,
                  int effect = -1, FxVec3 dir = {}, unsigned flags = 0, int surface = 0);

// weapon_fire.cpp: the triggers and the three controllers
void fighter_triggers_update(Fighter& f);
void weapon_controller(World& world, Fighter& f, int mount);
void weapon_reach_update(Weapon& w);

// shot_movers.cpp: the launchers (weapon_delivery_callbacks) and the per-step movers
void shot_launch(World& world, const Fighter& owner, const Weapon& w, int ref, Shot& shot,
                 const FxMat4& m);
void shot_move(World& world, Shot& shot);
// impact_radius_at
Fx impact_radius_at(Fx life, const Weapon& w);

// shot_pass.cpp
int shot_alloc(World& world);
void shot_clear(Shot& shot);
void battle_shots_update(World& world);

// shot_hits.cpp: shot_hit_fighters, impact_volume_scan and the damage roll
int shot_hit_fighters(World& world, int shot_index, bool contact_step);
// tri_sphere_hit: a triangle against a sphere (the face from one side, or an edge)
bool tri_sphere_hit(FxVec3 v0, FxVec3 v1, FxVec3 v2, FxVec3 centre, Fx radius, FxVec3* hit);
void impact_volume_scan(World& world, int shot_index, int skip_slot);

// damage_settle.cpp: before the fighter's step, what its swing reads through the original's
// pointers (the blade's point, the target's aim point); after its step, actor_settle_damage
void combat_fighter_early(World& world, Fighter& f);
void combat_fighter_late(World& world, Fighter& f);

// hud_aim.cpp: hud_update (senses, reticle points and tracking, missile and melee locks)
void hud_update(World& world, Fighter& f);
// weapon_lock_take: the first finished lock of a back mount, released; -1 none
int weapon_lock_take(Fighter& f, int mount);

// weapon_clip.cpp: the weapon model's clip. weapon_anim_init at spawn;
// weapon_reload_pulse (asks for the shot clip) from the fire and melee controllers;
// weapon_anim_update from the pose (mech/pose_weapons.cpp), writing the mount's node values
struct RigWeaponBlock;
void weapon_clip_init(Weapon& w, const RigWeaponBlock& block);
void weapon_anim_request(Weapon& w, const RigWeaponBlock& block, int want);
void weapon_reload_pulse(const World& world, const Fighter& f, Weapon& w);
void weapon_anim_update(Weapon& w, const RigWeaponBlock& block, bool human, FxVec3* translate,
                        Angle (*euler)[3], int count);
