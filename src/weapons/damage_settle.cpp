// The victim's side of damage, at the end of its step (decomp src/hand/mech_part_assets.c
// actor_settle_damage; src/hand/u_088880.c actor_weapon_break_apply,
// actor_stagger_start; docs/port/weapons.md "Damage").
#include "mech/fighter_rates.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// the impulse x Battle.impulse_decay 0.8 an update; R13: sqrt(0.8) a step (sixty.md "Knockback")
constexpr Fx impulse_decay_60 = fx_f(0.8944271909999159);
// a human at view 0 outside VS gets 15 updates of screen static and shake (the stage counts them)
constexpr int hit_static_updates = 15;
// the kill streak window: max(180, 900 - 120 x streak) ticks, -= ticks (auto)
constexpr int streak_window_most = 900;
constexpr int streak_window_step = 120;
constexpr int streak_window_least = 180;
// the stagger clips (0x11 forward .. 0x14), +0xC in the air
constexpr int stagger_clip = 0x11;
constexpr int air_clip_offset = 0xC;
constexpr int death_clip = 0xC;

// the clip by where the impulse pushes in the fighter's frame
// decomp: actor_stagger_start 0x8DC51
void stagger_start(Fighter& f, const BattleData& data) {
    // world to body; the port's local z is the original's negated
    const FxVec3 local = turned_by(f.impulse, -f.heading);
    int clip;
    if (fx_abs(local.x) < fx_abs(local.z)) {
        clip = local.z < Fx{} ? stagger_clip + 1 : stagger_clip; // 0x12 pushed back
    } else {
        clip = local.x > Fx{} ? stagger_clip + 3 : stagger_clip + 2;
    }
    if (!f.grounded) {
        clip += air_clip_offset;
    }
    motion_set_animation(f, data, clip, state_stagger, motion_lead_in, Fx{});
}

// a coin of two draws picks arm or back on the hit's side
// decomp: actor_weapon_break_apply 0x8D606
void weapon_break_apply(World& world, Fighter& f) {
    FighterCombat& c = f.combat;
    if (!c.damage.weapon_break) {
        return;
    }
    const int first = rand_table_next(world.random);
    const int second = rand_table_next(world.random);
    const bool back = first - second >= 0;
    const int mount = (c.damage.break_side == 0 ? 1 : 0) + (back ? 2 : 0);
    Weapon& w = c.weapons[mount];
    if (!w.present || c.weapon_broken[mount]) {
        battle_break_barks(f, back); // the roll is spent; the bark is asked all the same
        return;
    }
    c.weapon_broken[mount] = true;
    w.fitted = false;
    w.resource = Fx{};
    battle_break_barks(f, back);
    // retail pauses the task with its beam or blade still in the pool, frozen and live; the
    // port removes it (docs/bugs.md)
    if (w.beam_shot >= 0) {
        shot_clear(world.combat.shots[static_cast<std::size_t>(w.beam_shot)]);
        w.beam_shot = -1;
        if (c.melee_endpoint >= 0) {
            c.melee_endpoint = -1;
        }
    }
    combat_event(world, event_weapon_break, f.slot, mount, f.position);
}

// decomp: actor_settle_damage 0x766DE
void settle_damage(World& world, Fighter& f) {
    FighterCombat& c = f.combat;
    DamageState& d = c.damage;
    const BattleData& data = *world.battle;
    // once a VS round is decided nothing settles, so the survivor can't die after it
    if (f.motion_state != state_dead && !vs_round_decided(world)) {
        int total = 0;
        for (int i = 0; i < combat_slots; ++i) {
            if (i == f.slot) {
                d.pending[i] = 0; // damage to yourself is thrown away
            } else {
                total += d.pending[i];
            }
        }
        if (total > 0) {
            combat_event(world, event_popup, f.slot, 0, f.position, 0); // "DAMAGED"
            // retail sets the static and shake for a human at view 0 and sets both to 0 for any
            // other fighter damaged later in the update, cancelling it; the port only sets them
            // (docs/bugs.md)
            if (f.controller >= 0 && f.camera.view == 0) {
                hit_static_frames_set(world.stage, hit_static_updates);
                hit_shake_frames_set(world.stage, hit_static_updates);
                world.stage.hit_seat = f.controller;
            }
            battle_damage_barks(world, f, total);
            if (c.durability > 0 && c.durability - total <= 0) {
                // the killing blow: everyone with damage in it gets the whole kill
                for (int i = 0; i < combat_slots; ++i) {
                    if (d.pending[i] <= 0) {
                        continue;
                    }
                    FighterCombat& killer = world.fighters[static_cast<std::size_t>(i)].combat;
                    const int pay =
                        killer.streak > 0 ? c.destruction_value * 2 : c.destruction_value;
                    combat_event(world, event_popup, i, 2, f.position, pay); // "BINGO!"
                    killer.score += pay;
                    killer.kills += 1;
                    killer.streak += 1;
                    killer.streak_window =
                        fx(std::max(streak_window_least,
                                    streak_window_most - killer.streak * streak_window_step));
                    // a human's kill: records, the beaten byte, the boss scenes; a human's
                    // death: the scene watching the killer (src/battle/battle_flow.hpp)
                    Fighter& by = world.fighters[static_cast<std::size_t>(i)];
                    battle_kill_barks(world, f, by);
                    battle_kill(world, f, by);
                    battle_death(world, f, i,
                                 d.last_weapon[i] != -1 && by.motion_state != state_dead);
                }
            } else {
                for (int& last : d.last_weapon) {
                    last = -1;
                }
            }
            c.durability -= total;
            for (int& pending : d.pending) {
                pending = 0;
            }
            // any damage breaks camouflage
            camo_break(f.camo);
            if (d.stagger_peak > Fx{}) {
                stagger_start(f, data);
                // stagger_end = the largest stagger of the update, ticks, counted from the next
                // update; R12 B: 2 ceil(S / 2) (sixty.md "Dodging, skidding, stagger"), capped at
                // the clip's end
                const int half_up = (d.stagger_peak.raw + 2 * fx_one_raw - 1) / (2 * fx_one_raw);
                c.stagger_end = fx_min(fx(2 * half_up), f.timing.end);
            }
            // the pad's rumble, by the damage (presentation's: input/pad_rumble.hpp)
            if (f.controller != -1 && world.battle_phase == 0) {
                combat_event(world, event_hit_rumble, f.slot, total, f.position);
            }
        }
        if (c.durability <= 0) {
            c.alive = false;
            motion_set_animation(f, data, death_clip, state_dead, motion_lead_in, Fx{});
            c.durability = 0;
            camo_break(f.camo);
            combat_event(world, event_destroyed, f.slot, 0, f.position);
        } else {
            weapon_break_apply(world, f);
        }
    } else {
        for (int mount = 0; mount < 4; ++mount) {
            c.triggers[mount].pressed = false;
            f.control.control_buttons &= ~mount_fire_bits[mount];
        }
    }
    // next step's target: the hit sphere at position + radius, the aim point the same
    d.center = f.position + FxVec3{Fx{}, f.radius, Fx{}};
    for (int& pending : d.pending) {
        pending = 0;
    }
    d.stagger_peak = Fx{};
    d.facing = turned_by({Fx{}, Fx{}, fx(1)}, f.heading);
    f.impulse = scaled_toward_zero(f.impulse, impulse_decay_60);
    c.aim_point = d.center;
    d.weapon_break = false;
    if (d.immune_frames > Fx{}) {
        d.immune_frames -= fx(1);
    }
    if (f.intangible_frames > Fx{}) {
        f.intangible_frames -= fx(1);
    }
    if (c.streak_window > Fx{}) {
        c.streak_window -= fx(1);
        if (c.streak_window <= Fx{}) {
            c.streak = 0;
        }
    }
    // actor_step's tail in states 6 and 5: the ammo crossing a half
    if (world.battle_state.live && world.battle_state.menu_state == 6) {
        battle_ammo_barks(f);
    }
}

} // namespace

void combat_fighter_early(World& world, Fighter& f) {
    FighterCombat& c = f.combat;
    c.melee_tip_known = c.melee_endpoint >= 0;
    if (c.melee_tip_known) {
        c.melee_tip = world.combat.shots[static_cast<std::size_t>(c.melee_endpoint)].pos;
    }
    c.melee_target_known = c.melee_target >= 0;
    if (c.melee_target_known) {
        c.melee_target_point =
            world.fighters[static_cast<std::size_t>(c.melee_target)].combat.aim_point;
    }
}

void combat_fighter_late(World& world, Fighter& f) {
    // actor_step's tail: camo_update, then the settle; actor_sounds_update's cloak sounds after
    camo_update(world, f);
    settle_damage(world, f);
    camo_sound(world, f);
}
