#pragma once
// Weapons, shots and damage in the world (decomp docs/port/weapons.md; include/actor.h Weapon,
// Shot, DamageState, MountTrigger, LockMark, FighterHud, ShotThreats). Every number is the
// original's unless its comment gives the 60 Hz conversion (docs/sixty.md). The port frame has z
// negated against the original's (mech/fighter.hpp), so where the original takes a cross product
// or a yaw the code says which way it turns.
#include "fx/angle.hpp"
#include "fx/mat.hpp"
#include "fx/vec.hpp"
#include "weapons/combat_tables.hpp"
#include "world/fighter_slots.hpp"

#include <array>
#include <cstdint>

inline constexpr int combat_slots = fighter_slots;
inline constexpr int shot_pool_size = 0x400; // Battle.shots
inline constexpr int lock_mark_count = 0x18; // FighterHud.locks
// the blast weapon's mount in a WeaponRef (Battle.blast_weapon[slot], blast_weapon_setup)
inline constexpr int blast_mount = 4;

// an arm's raise when it fires: retail's 5 updates are 10 steps (sixty.md R12 D); the pose blends
// the arm up by raise / arm_raise_full
inline constexpr int arm_raise_full = 10;

// MountTrigger (Actor +0x2FA4 + 12 x mount)
struct MountTrigger {
    int fire = 0;  // 3 the first step held, 2 after, 0 up
    int raise = 0; // arms: 0..10 at 60 Hz (retail 0..5 an update; sixty.md R12 D)
    bool pressed = false;
    bool ranged = false;
};

// Weapon (0x108): the record at its tune (weapon_materialize_tuned_arm) and its state
struct Weapon {
    const WeaponRecord* record = nullptr;
    int id = -1;
    int mount = 0;
    bool fitted = false;
    bool present = false; // a part on the mount (its task runs)
    // stats
    Fx weight;
    Angle elevation;
    int reload_frames = 0; // ticks
    int shot_life = 0;     // ticks
    Fx speed, speed_max, accel;
    Angle pellet_spread;
    Angle turn_rate; // per tick
    int shots = 0;
    int damage = 0;
    Fx blast_radius_end, blast_radius_start;
    Fx impact_frames, impact_damage;
    int resource_max = 0;
    Fx recharge_frames, stagger, knockback, break_chance;
    // state
    int lock_target = -1;
    bool volley_active = false;
    int volley_index = 0;
    int volley_timer = 0;    // ticks
    int beam_shot = -1;      // the laser's beam or the melee blade, a pool index
    int reload_left = 0;     // ticks (2 ceil(R/2) + 1 at 60 Hz); a laser's intensity 0..8
    int muzzle = 0;          // barrel
    Fx resource;             // rounds or charge
    Angle launch_pitch;      // backs: the barrel's pitch this step, up positive (the original's)
    Fx reach_max, reach_min; // the AI's range (weapon_reach_update)
    // the weapon clip (Weapon +0xC4..+0xF8, weapon_clip.cpp): its AnimPlayer's time and the range
    // it plays (start, end), the state (0 rest, 1 opening, 2 firing, 3 open, 4 closing), the
    // state asked for next, and the ticks held open (anim_turn)
    Fx clip_time, clip_start, clip_end;
    int clip_state = 0, clip_next = 0;
    Fx clip_turn;
};

// Shot (0x3A0). `weapon` names its weapon as slot * 5 + mount (mount 4 the blast weapon)
enum ShotMover : std::uint8_t {
    mover_none,
    mover_straight, // projectile_update_straight_next
    mover_lob,      // projectile_update_rotating_next
    mover_guided,   // projectile_update_guided_next
    mover_unguided, // projectile_update_guided_next2
    mover_blast,    // impact_volume_callback
    mover_kind3,    // impact_kind3_update (no record has it)
};

struct Shot {
    FxMat3 basis;    // the transform's rows; z is the direction
    FxVec3 origin;   // its translation: a beam's muzzle, a blade's point
    int weapon = -1; // slot * 5 + mount
    int target = -1;
    FxVec3 pos, prev_pos, hit_pos, velocity; // velocity per tick
    // the position two steps back: a blast's capsule starts there (sixty.md "Inherited
    // velocity" and weapons.md "At 60 Hz" 5), a beam's sweep from the tip of the last even step
    FxVec3 prev_pos2;
    int state = 0; // 0 free, 1 flying, 2 hit, 3 blast
    int flags = 0; // 1 hit a fighter this pass
    Fx life;       // ticks (a blast: the original's, stepped 2 on even steps)
    Fx radius;     // 0.3; a blast's radius; a beam's length
    int damage = 0;
    ShotMover mover = mover_none;
    std::uint16_t dodge_rolled = 0; // a bit for each AI slot that rolled to dodge it
};

// DamageState (Actor +0x278C); the impulse and intangible_frames are the Fighter's own fields
struct DamageState {
    FxVec3 center;
    Fx sphere_radius;
    int pending[combat_slots] = {};
    FxVec3 facing;
    Fx stagger_peak;
    Fx immune_frames;
    bool weapon_break = false;
    int break_side = 0;
    int last_weapon[combat_slots] = {-1, -1, -1, -1, -1, -1};
    // the step each attacker last queued damage: the AI, which runs on even steps, asks for the
    // pending damage of the two steps since its last update (ai_links fighter_hit_pending)
    std::int64_t last_hit_step[combat_slots] = {-100, -100, -100, -100, -100, -100};
};

// LockMark (0x48): the screen wobble (CRT rand) and trail are presentation and not kept
struct LockMark {
    int target = -1;
    Fx timer; // ticks
    int mount = -1;
};

// The FighterHud fields of the tracking point and the lock-on (hud_update). The senses,
// the visible list and the reticle's target are battle/fighter_hud.hpp's (World.huds), which this
// reads; locked_on, locking_me and melee_locked are written there for the AI
struct LockHud {
    Fx track_x, track_y, track_inv_depth; // reticle_track: an offset from the centre; 64 / depth
    Fx box_a[2], box_b[2]; // half extents of the tracking and the lock box, -1..1 units
    LockMark locks[lock_mark_count];
    LockMark warning; // the melee lock
    bool melee_locked = false;
    int mount_lock_target[2] = {-1, -1};
    int mount_locks[2] = {};
    int mount_lock_max[2] = {};
    int lock_candidates[combat_slots] = {};
    int lock_candidate_count = 0;
    bool lock_candidate_used[combat_slots] = {};
    bool locked_on[combat_slots] = {};
};

// ShotThreats (Battle +0xEE008) per fighter as pool indices: what ai_dodge_shots reads next step
// (ai/ai_links.hpp world_shot_threats copies them out)
struct ShotThreatLists {
    int locked_on_me[9];
    int locked_on_me_count = 0;
    int nearby[29];
    int nearby_count = 0;
    int melee[4];
    int melee_count = 0;
    int beams[4];
    int beam_count = 0;
};

// What presentation hears of the fight (the effects, sounds and barks are a later job): an
// impact's kind and effect set, a fighter destroyed, a shot leaving
enum CombatEventKind : std::uint8_t {
    event_impact,      // impact_profile_dispatch: kind (impact or impact_alt), effect set
    event_muzzle,      // a shot left (the muzzle flash, the fire sound)
    event_hit_fighter, // damage queued on a fighter: amount in `value`
    event_destroyed,   // a fighter's durability ran out
    event_weapon_break,
    // the HUD's (src/hud): a popup for `slot` (actor_hud_popup_push), `value` the kind
    // (0 hit taken, 1 hit dealt, 2 kill) and `effect` its points
    event_popup,
    // `slot`'s lock mark finished (hud_locks_update, hud_melee_lock_update): `value` the target,
    // `effect` the back mount 0 or 1, -1 the melee lock
    event_lock_done,
    // `slot`'s lock alarm (actor_sounds_update): `value` 1 starts it, 0 stops it
    event_lock_warning,
    // a pickup refilled `slot`'s weapon on mount `value` (weapon_gauge_refill_start)
    event_gauge_refill,
    // a human's weapon on mount `value` began opening (weapon_anim_update: its sound,
    // bank 3 cue 0x15 at the fighter)
    event_weapon_open,
    // a human's settle took damage in a fight (battle phase 0): actor_settle_damage's
    // rumble_start on its pad, `value` the damage settled (presentation's pad rumble)
    event_hit_rumble,
    // a shot's pass met something (battle_shots_update 0x9C942's hit_slot: the stage, the craft
    // or a crate, a prop, or a fighter), at the shot: its hit sound by the weapon (presentation).
    // `slot` the shooter, `value` the mount (4 the blast weapon); a beam's on even steps only,
    // as its once-an-update pass
    event_shot_hit,
    // a beam met a fighter (shot_hit_fighters' nearest, impact_volume_scan's sweep; their
    // sound_start_3d(1, 0x12)), at the hit: `slot` the shooter, `value` the fighter it met
    event_beam_hit,
};
// CombatEvent.flags of an impact
enum CombatEventFlag : unsigned {
    event_alt = 1,     // impact_alt: the life ran out or the craft or a crate was hit (no mark)
    event_missile = 2, // a missile's hit (behaviour 1, not alt): an orange point light of 12
};
// Each carries the step it happened on and the slot it concerns, so under rollback presentation can
// play an event once on first sight and drop it when a re-run of that step makes it again
// (docs/multiplayer.md "Change now", item 10). Presentation reads them after every step.
struct CombatEvent {
    CombatEventKind kind = event_impact;
    std::int64_t step = 0; // World::step it happened on
    int slot = -1;   // the fighter it concerns (the shooter for muzzle and impact)
    int value = 0;   // impact kind, damage, mount
    int effect = -1; // impact_fx row (effect set) for impacts; a muzzle's barrel (Weapon::muzzle)
    FxVec3 position;
    // for the effects (src/effects): an impact's surface up (the original's matrix_top row 1:
    // the stage triangle's normal, else the shot's up), a muzzle's forward (its third row)
    FxVec3 dir;
    unsigned flags = 0; // CombatEventFlag
    int surface = 0;    // an impact's stage surface bits (collision_segment_frame's contact)
};
inline constexpr int combat_event_max = 128;

// Everything the fight owns outside the fighters
struct CombatWorld {
    std::array<Shot, shot_pool_size> shots;
    int shot_cursor = 0;
    ShotThreatLists threats[combat_slots];
    std::array<CombatEvent, combat_event_max> events;
    int event_count = 0; // this step's
};

// The Actor fields weapons read and write, one per fighter (mech/fighter.hpp: Fighter.combat)
struct FighterCombat {
    bool alive = true;
    int durability = 0, durability_max = 0;
    // damage_options_apply: percents, the shield and the laser cut as fractions
    Fx crit_chance, shield_chance, break_resist, laser_reduction;
    // the chip (chip_eval_tuned_fields)
    int reticle_kind_a = 0, reticle_kind_b = 0;
    Fx reticle_size_a, reticle_size_b;
    int reticle_speed = 0;
    int lock_frames = 0;
    MountTrigger triggers[4];
    Weapon weapons[4];
    Weapon blast_weapon;
    bool mount_free_aim[4] = {};
    bool weapon_broken[4] = {}; // Actor.weapon_fitted's broken words
    int melee_clip[2] = {0x17, 0x18};
    DamageState damage;
    LockHud hud;
    FxVec3 aim_point;
    FxVec3 reticle_track_world; // reticle_center_world is the Fighter's (battle/hud_senses.cpp)
    bool melee_active = false, melee_connected = false;
    int melee_endpoint = -1;    // the blade shot whose pos the lunge measures from
    int melee_target = -1;      // melee_target_pos: that fighter's aim_point
    FxVec3 melee_target_stored; // +0x25B0: its y is the climb's aim height
    // what the swing's lunge reads through those pointers, copied in just before the fighter's
    // step (the motion code sees the fighter alone): the blade's point and the target's aim point
    bool melee_tip_known = false, melee_target_known = false;
    FxVec3 melee_tip, melee_target_point;
    Fx stagger_end;
    int defeat_frames = 0;
    int score = 0, kills = 0, streak = 0;
    Fx streak_window;
    int destruction_value = 0; // the roster's; builds alone carry none
    Fx ammo_fraction = fx(1);  // actor_ammo_fraction after the last settle (barks only)
};
