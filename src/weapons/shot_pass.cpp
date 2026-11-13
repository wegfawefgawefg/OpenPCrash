// The shot pool and its pass (decomp src/hand/battle_frame.c shot_alloc,
// battle_shots_update; src/od/u_097660.c impact_profile_dispatch, shot_blast_at;
// docs/port/weapons.md "The shot pass", "Lingering blasts and impacts", "The pool").
//
// At 60 Hz (sixty.md R11, "Contact damage"): projectiles move and hit every step. Lingering
// blasts and melee blades run on even steps only, with retail's numbers: a blast's life is the
// original's ticks stepped by 2 on even steps, so it deals its damage on ceil(F/2) passes as
// retail; a blade is tested from where it was on the last even step. A beam is cut against the
// stage and fighters every step (it is drawn every step) but damages, sweeps and catches props on
// even steps only, its sweep from the tip of the last even step.
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// the threat lists keep shots within 5 units (squared length under 25), beams at any range
constexpr Fx threat_range = fx(5);
// supply craft scores
constexpr int craft_shot_score = 1000;
constexpr int craft_beam_score = 100;

bool even_step(const World& world) {
    return world.step % 2 == 0;
}

// the beam's sweep (tip, last tip, muzzle) against the craft
// (radius 3) and each crate (radius 1). The craft is shot down as a projectile's is (a parked one
// isn't a target: supply_craft_hit_test, docs/bugs.md); a crate bursts
// decomp: supply_craft_beam_hit_test 0x6765C
int craft_beam_hit_test(StageWorld& stage, FxVec3 tip, FxVec3 last, FxVec3 muzzle) {
    int bits = 0;
    FxVec3 at;
    const bool flying = stage.craft.state != craft_off && stage.craft.state != craft_parked;
    if (stage.craft.shootable && flying &&
        tri_sphere_hit(tip, last, muzzle, stage.craft.pos, fx(3), &at)) {
        bits |= hit_craft;
        stage.craft.shootable = false;
    }
    for (Crate& crate : stage.crates) {
        if (crate.shootable && tri_sphere_hit(tip, last, muzzle, crate.pos, fx(1), &at)) {
            bits |= hit_crate;
            crate.shootable = false;
            crate.state = crate_burst;
        }
    }
    return bits;
}

// ends a shot by the record's impact (a hit) or impact_alt.
// frame_basis / frame_at: the transform the effect is placed by (the stage's frame on a stage
// hit, else the shot's own)
// up and surface: the stage triangle's normal and contact bits on a stage hit (the effects' frame
// and surface; up zero takes the frame's y, the shot's own up)
// decomp: impact_profile_dispatch 0x98E5A
void impact_dispatch(World& world, Shot& shot, bool alt, const FxMat3& frame_basis, FxVec3 frame_at,
                     bool fighter_hit, FxVec3 up = {}, int surface = 0) {
    const Weapon& w = weapon_at(world, shot.weapon);
    const WeaponRecord& r = *w.record;
    const int kind = alt ? r.impact_alt : r.impact;
    const int slot = ref_slot(shot.weapon);
    const FxVec3 dir = up == FxVec3{} ? frame_basis.y : up;
    const unsigned flags = (alt ? event_alt : 0u) | (!alt && r.behavior == 1 ? event_missile : 0u);
    switch (kind) {
    case 1:
        combat_event(world, event_impact, slot, kind, shot.pos, r.impact_fx, dir, flags, surface);
        shot_clear(shot);
        return;
    case 2:
        // a lingering blast where it hit, its capsule from the shot's position before the move
        // that hit: two steps back at 60 Hz (weapons.md "At 60 Hz" 5)
        shot.state = 3;
        shot.mover = mover_blast;
        shot.pos = shot.hit_pos;
        shot.prev_pos = shot.prev_pos2;
        shot.life = w.impact_frames; // the original's ticks, stepped 2 on even steps
        shot.damage = fx_trunc_div(w.impact_damage, fx(1));
        shot.basis = frame_basis;
        shot.origin = frame_at;
        combat_event(world, event_impact, slot, kind, frame_at, r.impact_fx, dir, flags, surface);
        return;
    case 3:
        shot.state = 3;
        shot.mover = mover_kind3;
        shot.pos = shot.hit_pos;
        shot.life = w.impact_frames;
        return;
    case 4:
        // the melee effect (its set only on a fighter) and sound; the controller clears it
        combat_event(world, event_impact, slot, kind, shot.pos, fighter_hit ? r.impact_fx : -1, dir,
                     flags, surface);
        return;
    case 5:
        combat_event(world, event_impact, slot, kind, shot.pos, r.impact_fx, dir, flags, surface);
        return;
    default:
        shot_clear(shot);
        return;
    }
}

// the slot's blast weapon fired at a point, a sphere of 150 a pass for 15
// ticks credited to the shooter. Lands in this pass if it sits after the current shot in the pool
// decomp: shot_blast_at 0x99631
void shot_blast_at(World& world, int slot, FxVec3 at) {
    Fighter& owner = world.fighters[static_cast<std::size_t>(slot)];
    const Weapon& bw = owner.combat.blast_weapon;
    if (!bw.record) {
        return;
    }
    const int index = shot_alloc(world);
    if (index < 0) {
        return;
    }
    Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    shot_launch(world, owner, bw, weapon_ref(slot, blast_mount), shot, mat4_translation(at));
    shot.state = 3;
    shot.mover = mover_blast;
    shot.pos = shot.prev_pos = shot.prev_pos2 = at;
    shot.life = bw.impact_frames;
    shot.damage = fx_trunc_div(bw.impact_damage, fx(1));
    shot.basis = mat3_identity();
    shot.origin = at;
}

// the AI's threat lists for one live shot (rebuilt every step; the AI reads the latest)
void add_threats(World& world, int index) {
    const Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    const int owner = ref_slot(shot.weapon);
    const int behavior = weapon_at(world, shot.weapon).record->behavior;
    for (const Fighter& other : world.fighters) {
        if (!other.present || other.slot == owner) {
            continue;
        }
        ShotThreatLists& t = world.combat.threats[other.slot];
        if (behavior == 5) {
            if (t.beam_count < 4) {
                t.beams[t.beam_count++] = index;
            }
        } else if (shorter_than(shot.pos - other.position, threat_range)) {
            if (behavior == 6) {
                if (t.melee_count < 4) {
                    t.melee[t.melee_count++] = index;
                }
            } else if (behavior == 1 && shot.target == other.slot) {
                if (t.locked_on_me_count < 9) {
                    t.locked_on_me[t.locked_on_me_count++] = index;
                }
            } else if (t.nearby_count < 29) {
                t.nearby[t.nearby_count++] = index;
            }
        }
    }
}

// one shot of the pass
void shot_pass_one(World& world, int index) {
    Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    const Weapon& w = weapon_at(world, shot.weapon);
    if (!w.record) {
        shot.state = 0;
        return;
    }
    const int behavior = w.record->behavior;
    const bool beam = behavior == 5;
    const bool blade = behavior == 6;
    const bool blast = shot.state == 3;
    const bool contact = even_step(world);
    if ((blast || blade) && !contact) {
        return;
    }
    const int slot = ref_slot(shot.weapon);
    Fighter& owner = world.fighters[static_cast<std::size_t>(slot)];

    // behaviours 0..3 run their update (the mover, or a blast's radius); beams and blades are
    // moved by their controllers
    if (behavior <= 3) {
        if (shot.mover == mover_none) {
            shot.state = 0;
            return;
        }
        shot_move(world, shot);
    }
    const FxVec3 from = beam ? shot.origin : blade ? shot.prev_pos2 : shot.prev_pos;

    // the stage, the supply craft and crates, the props, in that order, each on the segment the
    // last left
    int kind = -1;
    int prop = -1;
    FxMat3 frame = shot.basis;
    FxVec3 frame_at = shot.origin;
    int stage_surface = 0;
    FxVec3 stage_normal;
    if (!blast) {
        const BattleData& data = *world.battle;
        FxVec3 point, normal;
        stage_surface = collision_segment_frame(data.stage, from, shot.pos, &point, &normal, false);
        if (stage_surface) {
            shot.pos = point;
            stage_normal = normal;
            kind = 0;
            frame = mat3_from_dir_up(normal, {fx(1), Fx{}, Fx{}});
            frame_at = point;
        }
        FxVec3 at;
        const int craft = supply_craft_hit_test(world.stage, from, shot.pos, &at);
        if (craft) {
            kind = (craft & hit_craft) ? 1 : 2;
            shot.pos = at;
        }
        // the nearest prop the segment meets (retail takes the first in list order: fixed,
        // docs/bugs.md)
        PropRayHit best;
        for (int p = 0; p < world.stage.prop_count; ++p) {
            const Prop& candidate = world.stage.props[static_cast<std::size_t>(p)];
            PropRayHit hit;
            if (!(candidate.flags & (prop_gone | prop_open)) &&
                prop_ray_test(candidate, from, shot.pos, false, &hit) &&
                (prop < 0 || hit.distance < best.distance)) {
                best = hit;
                prop = p;
            }
        }
        if (prop >= 0) {
            kind = 3;
            shot.pos = best.point;
        }
    }

    // the fighters on what is left of the segment
    const int fighter = shot_hit_fighters(world, index, contact);
    if (shot.flags & 1) {
        kind = 4;
    }
    // its hit sound's event (a beam's once a pair, as retail's pass once an update)
    if (kind != -1 && (!beam || contact)) {
        combat_event(world, event_shot_hit, slot, shot.weapon % 5, shot.pos);
    }

    switch (kind) {
    case 0:
        shot.hit_pos = shot.pos;
        impact_dispatch(world, shot, false, frame, frame_at, false, stage_normal, stage_surface);
        if (!beam && shot.state != 0) {
            shot.basis = frame;
            shot.origin = frame_at;
        }
        break;
    case 1:
        // the craft shot down, +1000
        fighter_bark(owner, bark_hit_flying);
        if (supply_craft_shot_down(world.stage)) {
            owner.combat.score += craft_shot_score;
        }
        [[fallthrough]];
    case 2:
        shot.hit_pos = shot.pos;
        shot_blast_at(world, slot, shot.pos);
        impact_dispatch(world, shot, true, shot.basis, shot.origin, false);
        break;
    case 3: {
        bool blast_too = false;
        owner.combat.score += stage_prop_shot(world.stage, prop, &blast_too);
        if (blast_too) {
            fighter_bark(owner, bark_hit_flammable);
            shot_blast_at(world, slot, shot.pos);
        }
        impact_dispatch(world, shot, false, shot.basis, shot.origin, false);
        shot.hit_pos = shot.pos;
        break;
    }
    case 4:
        if (shot.state == 3) {
            shot.state = 0;
        } else {
            // the shooter's melee and laser hits: BARK_HIT 0x19, BARK_LASER_HIT 0x1A
            if (blade) {
                fighter_bark(owner, bark_hit);
            } else if (beam) {
                fighter_bark(owner, bark_laser_hit);
            }
            shot.origin = shot.hit_pos;
            impact_dispatch(world, shot, false, shot.basis, shot.origin, true);
        }
        shot.flags &= ~1;
        break;
    default:
        // life runs out on passes that hit nothing (a blast's 2 an even step, the original's)
        if (!beam && !blade) {
            shot.life -= blast ? fx(2) : fx(1);
            if (shot.life <= Fx{}) {
                if (blast) {
                    shot.state = 0;
                    break;
                }
                shot.hit_pos = shot.pos;
                impact_dispatch(world, shot, true, shot.basis, shot.origin, false);
            }
        }
        break;
    }

    if (beam && shot.state != 0) {
        // the cut length; on even steps the sweep since the last even step's tip
        shot.radius = length(shot.pos - shot.origin);
        if (contact) {
            if (shot.pos != shot.prev_pos2) {
                for (int p = 0; p < world.stage.prop_count; ++p) {
                    const Prop& candidate = world.stage.props[static_cast<std::size_t>(p)];
                    PropRayHit hit;
                    if (!(candidate.flags & prop_gone) &&
                        (prop_ray_test(candidate, shot.pos, shot.prev_pos2, false, &hit) ||
                         prop_ray_test(candidate, shot.pos, shot.origin, false, &hit))) {
                        stage_prop_beamed(world.stage, p);
                    }
                }
                // the craft or a crate in the sweep: the craft down for +100 and its bark
                const int craft = craft_beam_hit_test(world.stage, shot.pos, shot.prev_pos2,
                                                      shot.origin);
                if (craft) {
                    if (craft & hit_craft) {
                        fighter_bark(owner, bark_hit_flying);
                        supply_craft_shot_down(world.stage);
                        owner.combat.score += craft_beam_score;
                    }
                    shot.hit_pos = shot.pos;
                    impact_dispatch(world, shot, false, shot.basis, shot.origin, true);
                }
                impact_volume_scan(world, index, fighter);
            }
            shot.prev_pos2 = shot.pos;
        }
    }
    if (blade && shot.state != 0) {
        shot.prev_pos2 = shot.pos;
    }
}

} // namespace

// decomp: shot_alloc 0x46867
int shot_alloc(World& world) {
    CombatWorld& c = world.combat;
    // the first free shot from the cursor on, wrapping; cleared, the cursor moved past it
    for (int k = 0; k < shot_pool_size; ++k) {
        const int index = (c.shot_cursor + k) % shot_pool_size;
        Shot& shot = c.shots[static_cast<std::size_t>(index)];
        if (shot.state == 0) {
            shot_clear(shot);
            c.shot_cursor = index + 1;
            return index;
        }
    }
    return -1;
}

// decomp: shot_clear 0x9825B
void shot_clear(Shot& shot) {
    shot = Shot{};
}

// decomp: battle_shots_update 0x47E59
void battle_shots_update(World& world) {
    for (ShotThreatLists& t : world.combat.threats) {
        t = ShotThreatLists{};
    }
    for (int i = 0; i < shot_pool_size; ++i) {
        if (world.combat.shots[static_cast<std::size_t>(i)].state != 0) {
            shot_pass_one(world, i);
        }
    }
    for (int i = 0; i < shot_pool_size; ++i) {
        if (world.combat.shots[static_cast<std::size_t>(i)].state != 0) {
            add_threats(world, i);
        }
    }
}
