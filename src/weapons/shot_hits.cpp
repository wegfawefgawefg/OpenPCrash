// Shots against fighters and the damage roll (decomp src/hand/weapon_laser.c shot_hit_fighters,
// impact_volume_scan, damage_submit; src/od/u_097660.c
// damage_impact_result; src/od/u_052b10.c tri_sphere_hit;
// docs/port/weapons.md "Hitting fighters", "Damage").
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// Corpses are targets in retail: a dead fighter is hit, stops shots, is pushed and makes the
// rolls until its slot retires (only damage_submit ignores it). Kept as retail as read, not
// confirmed against the running original (docs/bugs.md); false lets shots pass through wrecks
constexpr bool corpses_take_hits = true;
// tri_sphere_hit measures the centre's height above the beam's swept triangle along the
// triangle's unnormalized cross product, so the test reaches radius x |cross| on one side.
// Kept as retail (it is which sweeps land; docs/bugs.md). true tests the radius
constexpr bool sweep_unit_normal = false;

// the rear test: travelling within 45 degrees of the way the victim faces is x3
constexpr Fx rear_cosine = fx_f(0.7071);
// knockback is added to the impulse, which moves the fighter every update and decays by 0.8:
// 5x the hit. At 60 Hz with sqrt(0.8) a step that is 4.74x; the hit x 1 / (1 + sqrt 0.8)
// (0.527864, not 0.5) makes every pair match (sixty.md R13, "Knockback")
constexpr Fx knockback_60 = fx_f(0.5278640450004206);
// the Repulsion Shield's immunity, 30 ticks set in the shot pass: R12 B, 30 at 60 Hz
constexpr Fx shield_immune_ticks = fx(30);
// a blade lands within 1.2 x the victim's radius of its centre
constexpr Fx blade_reach = fx_f(1.2);

// the slot filter both loops share: present and running, not the shooter, not intangible (and
// alive, if corpses are left out). No teams in the world yet (roster pilot[4])
bool can_hit(const Fighter& victim, const Fighter& shooter) {
    return victim.present && victim.slot != shooter.slot && victim.intangible_frames <= Fx{} &&
           (corpses_take_hits || victim.combat.alive);
}

// the amount that lands (0 for none), the rolls, the stagger and the
// knockback. Draws: shield, then crit, then on a crit with positive odds and no break pending,
// the break
// decomp: damage_impact_result 0x98861
int damage_impact_result(World& world, Fighter& victim, const Weapon& w, const Fighter& shooter,
                         int base, FxVec3 dir, bool directional) {
    DamageState& d = victim.combat.damage;
    if (d.immune_frames > Fx{}) {
        return 0;
    }
    int amount = base;
    if (directional && dir != FxVec3{}) {
        const Fx c = fx_div_near(dot(dir, d.facing), fx_mul_near(length(dir), length(d.facing)));
        if (c > rear_cosine) {
            amount = base * 3;
            fighter_bark(victim, bark_rear_hit);
        }
    }
    if (w.record->behavior == 5) {
        // amount -= (int)(amount x laser_reduction), in 64 bits (a tripled crit passes 32768)
        amount -= static_cast<int>(
            (static_cast<std::int64_t>(amount) * victim.combat.laser_reduction.raw) >> fx_shift);
    }
    if (amount <= 0) {
        return 0;
    }
    if (rand_fx(world.random, fx(1)) < victim.combat.shield_chance) {
        d.immune_frames = shield_immune_ticks;
        fighter_bark(victim, bark_shield);
        return 0;
    }
    if (rand_fx(world.random, fx(100)) < shooter.combat.crit_chance) {
        fighter_bark(victim, bark_crit);
        amount *= 2;
        const Fx odds = w.break_chance - victim.combat.break_resist;
        if (odds > Fx{} && !d.weapon_break) {
            if (odds > rand_fx(world.random, fx(100))) {
                d.weapon_break = true;
                // the side: (up x facing) . dir >= 0 is side 0. The port's frame is mirrored, which
                // turns a cross product over, so the original's sign is minus the port's
                const FxVec3 side = cross({Fx{}, fx(1), Fx{}}, d.facing);
                d.break_side = -dot_q32(side, dir) >= 0 ? 0 : 1;
            }
        }
    }
    d.stagger_peak = fx_max(d.stagger_peak, w.stagger);
    if (dir != FxVec3{}) {
        const Fx keep = fx_div_near(fx(100) - victim.leg_knockback_resist, fx(100));
        const Fx push = fx_mul_near(fx_mul_near(w.knockback, keep), knockback_60);
        victim.impulse += scaled_to(dir, push);
    }
    return amount;
}

// queue it on the victim for its next settle, and the shooter's score
// decomp: damage_submit 0x9D24E
void damage_submit(World& world, Fighter& shooter, Fighter& victim, int ref, int amount,
                   bool broke_before) {
    if (amount <= 0 || !victim.combat.alive || shooter.slot == victim.slot) {
        return;
    }
    DamageState& d = victim.combat.damage;
    d.pending[shooter.slot] += amount;
    d.last_weapon[shooter.slot] = ref;
    d.last_hit_step[shooter.slot] = world.step;
    shooter.combat.score += 1;
    if (!broke_before && d.weapon_break) {
        shooter.combat.score += victim.combat.destruction_value / 10;
    }
    combat_event(world, event_hit_fighter, victim.slot, amount, d.center);
    combat_event(world, event_popup, shooter.slot, 1, d.center, 1); // "HIT", 1 point
}

} // namespace

// the triangle (v0, v1, v2) against a sphere: the centre over the face
// (collision_tri_sphere_push_out, one side only), else any edge within the radius
// decomp: tri_sphere_hit 0x52E30
bool tri_sphere_hit(FxVec3 v0, FxVec3 v1, FxVec3 v2, FxVec3 centre, Fx radius, FxVec3* hit) {
    if (v1 != v2) {
        // the original's normal is (v1 - v0) x (v2 - v0); mirrored frame: the other way round
        const FxVec3 n = cross(v2 - v0, v1 - v0);
        const std::int64_t nn = length_q32(n);
        const std::int64_t above = dot_q32(n, centre - v0);
        // the height in units of |n| (retail) is above / nn; the true height above / sqrt(nn)
        const std::int64_t reach = sweep_unit_normal
                                       ? static_cast<std::int64_t>(fx_sqrt_q32(nn).raw) * radius.raw
                                       : static_cast<std::int64_t>(nn >> fx_shift) * radius.raw;
        if (nn > 0 && above >= 0 && above < reach) {
            const FxTri tri{v0, v1, v2, unit_cross(v2 - v0, v1 - v0)};
            if (frame_inside(tri_frame(tri, centre))) {
                // the point the original reports: the radius from the centre toward v0
                *hit = centre + scaled_to(v0 - centre, radius);
                return true;
            }
        }
    }
    return segment_sphere_entry(v1, v0, centre, radius, hit) ||
           segment_sphere_entry(v2, v0, centre, radius, hit) ||
           segment_sphere_entry(v2, v1, centre, radius, hit);
}

// decomp: shot_hit_fighters 0x9C942
int shot_hit_fighters(World& world, int index, bool contact_step) {
    Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    const Weapon& w = weapon_at(world, shot.weapon);
    Fighter& shooter = world.fighters[static_cast<std::size_t>(ref_slot(shot.weapon))];
    const int behavior = w.record->behavior;
    if (shot.state != 1 && shot.state != 3) {
        return -1;
    }
    int nearest = -1;
    for (Fighter& victim : world.fighters) {
        if (!can_hit(victim, shooter)) {
            continue;
        }
        DamageState& d = victim.combat.damage;
        const bool broke_before = d.weapon_break;
        int amount = 0;
        if (behavior == 5) {
            // the beam muzzle to tip; the cut is the crossing nearest the muzzle (retail takes
            // the one nearest the tip, the far side of the victim: fixed, docs/bugs.md)
            FxVec3 cut;
            if (segment_sphere_entry(shot.origin, shot.pos, d.center, d.sphere_radius, &cut)) {
                shot.pos = cut;
                nearest = victim.slot;
            }
        } else if (behavior == 6) {
            // the blade's point within reach while the owner's swing is live (even steps only);
            // the direction is the blade's motion reversed, prev_pos - pos (kept, docs/bugs.md)
            if (contact_step && shooter.combat.melee_active &&
                shorter_than(shot.origin - d.center, fx_mul_near(victim.radius, blade_reach))) {
                amount = damage_impact_result(world, victim, w, shooter, w.damage,
                                              shot.prev_pos2 - shot.pos, true);
                shooter.combat.melee_connected = false;
                if (amount > 0) {
                    shooter.combat.melee_active = false;
                    shot.hit_pos = shot.pos;
                    shot.flags |= 1;
                }
            }
        } else {
            FxVec3 entry;
            const Fx reach = d.sphere_radius + shot.radius;
            if (segment_sphere_entry(shot.prev_pos, shot.pos, d.center, reach, &entry)) {
                if (shot.state == 3) {
                    // a blast: everyone in the capsule, every pass, not directional
                    if (contact_step) {
                        amount = damage_impact_result(world, victim, w, shooter, shot.damage,
                                                      d.center - shot.pos, false);
                        if (amount > 0 && victim.combat.durability > 0) {
                            fighter_bark(victim, bark_blast_taken);
                        }
                    }
                } else {
                    nearest = victim.slot;
                    shot.pos = entry;
                }
            }
        }
        damage_submit(world, shooter, victim, shot.weapon, amount, broke_before);
    }
    if (nearest == -1) {
        return -1;
    }
    Fighter& target = world.fighters[static_cast<std::size_t>(nearest)];
    DamageState& d = target.combat.damage;
    const bool broke_before = d.weapon_break;
    int amount = 0;
    if (behavior == 5) {
        if (contact_step) {
            combat_event(world, event_beam_hit, shooter.slot, target.slot, shot.pos);
            amount = damage_impact_result(world, target, w, shooter, w.damage,
                                          shot.pos - shot.origin, true);
        }
    } else if (behavior != 6) {
        shot.state = 2;
        shot.hit_pos = shot.pos;
        shot.flags |= 1;
        amount = damage_impact_result(world, target, w, shooter, w.damage, shot.pos - shot.prev_pos,
                                      true);
    }
    damage_submit(world, shooter, target, shot.weapon, amount, broke_before);
    return nearest;
}

// decomp: impact_volume_scan 0x9D003
void impact_volume_scan(World& world, int index, int skip_slot) {
    Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    const Weapon& w = weapon_at(world, shot.weapon);
    Fighter& shooter = world.fighters[static_cast<std::size_t>(ref_slot(shot.weapon))];
    for (Fighter& victim : world.fighters) {
        if (victim.slot == skip_slot || !can_hit(victim, shooter)) {
            continue;
        }
        DamageState& d = victim.combat.damage;
        const bool broke_before = d.weapon_break;
        // the triangle the tip swept since the last even step's pass (sixty.md "Contact damage")
        FxVec3 hit;
        if (tri_sphere_hit(shot.pos, shot.prev_pos2, shot.origin, d.center, d.sphere_radius,
                           &hit)) {
            combat_event(world, event_beam_hit, shooter.slot, victim.slot, hit);
            // back toward the shooter: muzzle - hit point (kept, docs/bugs.md)
            const int amount =
                damage_impact_result(world, victim, w, shooter, w.damage, shot.origin - hit, true);
            damage_submit(world, shooter, victim, shot.weapon, amount, broke_before);
        }
    }
}
