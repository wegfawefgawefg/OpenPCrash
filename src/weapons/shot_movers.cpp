// Shots in flight (decomp src/od/u_097660.c): the launchers projectile_update_straight,
// _rotating and _guided, and the movers _straight_next, _rotating_next, _guided_next and
// _guided_next2, with projectile_accelerate (docs/port/weapons.md "Shots in flight").
// Velocities are per tick and moved `ticks` times an update, so at 60 Hz once a step (sixty.md
// R9); the forces in a step get the half-step correction (R10): `+ dv / 2` for the movers that
// change the velocity first, `- dv / 2` for the guided ones that move first.
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// a shot's radius against fighters
constexpr Fx shot_radius = fx_f(0.3);
// the lob's fall, 9.8 / 900 x 2.5 off the per-tick velocity once an update; at 60 Hz half of it
// a step (0.0136111), and the move takes off half the step's change (0.0068056): R10, sixty.md
// "Lobs"
constexpr Fx lob_fall_60 = fx_f(9.8 / 900.0 * 2.5 / 2.0);
constexpr Fx lob_fall_half_60 = fx_f(9.8 / 900.0 * 2.5 / 4.0);
constexpr FxVec3 up_axis{Fx{}, fx(1), Fx{}};

// projectile_accelerate's new speed: |v| + ticks x accel, at most speed_max (accel per tick:
// auto at 60 Hz)
Fx next_speed(const Shot& shot, const Weapon& w) {
    return fx_min(length(shot.velocity) + w.accel, w.speed_max);
}
// the force part of the speed change, for R10: the acceleration, not a cut to speed_max nor the
// firer's speed dropped on the first rebuild
Fx accel_part(Fx before, Fx after, const Weapon& w) {
    return fx_clamp(after - before, Fx{}, w.accel);
}

void step_back(Shot& shot) {
    shot.prev_pos2 = shot.prev_pos;
    shot.prev_pos = shot.pos;
}

// z turned toward `to` by at most `turn` (matrix_slerp of the frames, read as the shortest arc
// between their z axes; both frames are built up y)
FxVec3 turned_toward(FxVec3 z, FxVec3 to, Angle off, Angle turn) {
    const FxVec3 to_n = normalized(to);
    const FxVec3 across = to_n - z * dot(z, to_n);
    if (across == FxVec3{} || off == Angle{}) {
        return to_n;
    }
    const FxVec3 side = normalized(across);
    return normalized(z * fx_cos(turn) + side * fx_sin(turn));
}

} // namespace

// decomp: impact_radius_at 0x98218
Fx impact_radius_at(Fx life, const Weapon& w) {
    if (w.impact_frames <= Fx{}) {
        return Fx{};
    }
    return w.blast_radius_end -
           fx_mul_div(w.blast_radius_end - w.blast_radius_start, life, w.impact_frames);
}

void shot_launch(World& world, const Fighter& owner, const Weapon& w, int ref, Shot& shot,
                 const FxMat4& m) {
    const int delivery = w.record->delivery;
    shot.weapon = ref;
    shot.pos = shot.prev_pos = shot.prev_pos2 = m.w;
    shot.basis = mat4_basis(m);
    shot.origin = m.w;
    Fx scale = fx(1);
    if (delivery == 0) {
        // a speed roll multiplied by 0.0: the draw alone
        rand_table_next(world.random);
    } else if (delivery == 2) {
        // the spin is for drawing; then the speed x (r + 3) / 3.5
        rand_table_next(world.random);
        const int roll = rand_table_next(world.random);
        scale = fx_div_near(fx_ratio(roll, 32767) + fx(3), fx_f(3.5));
    }
    // + owner->velocity: a per-update velocity into a per-tick one, so twice the firer's
    // per-step velocity at 60 Hz (sixty.md "Inherited velocity")
    shot.velocity = shot.basis.z * fx_mul_near(w.speed, scale) + owner.velocity * fx(2);
    shot.flags = 0;
    // life -= ticks on passes that hit nothing: ceil(L/2) moves. R12 B: 2 ceil(L/2) steps
    shot.life = fx(2 * ((w.shot_life + 1) / 2));
    shot.radius = shot_radius;
    shot.damage = w.damage;
    shot.target = delivery == 2 ? w.lock_target : -1;
    shot.mover = delivery == 0                                 ? mover_straight
                 : delivery == 1                               ? mover_lob
                 : shot.target != -1 && w.turn_rate != Angle{} ? mover_guided
                                                               : mover_unguided;
    shot.state = 1;
}

void shot_move(World& world, Shot& shot) {
    const Weapon& w = weapon_at(world, shot.weapon);
    switch (shot.mover) {
    case mover_straight: {
        // accelerate along the shot's z, then move with the new velocity
        shot.origin = shot.pos;
        const Fx before = length(shot.velocity);
        const Fx speed = next_speed(shot, w);
        shot.velocity = shot.basis.z * speed;
        const Fx dv = accel_part(before, speed, w);
        step_back(shot);
        shot.pos += shot.velocity + shot.basis.z * (dv / 2);
        break;
    }
    case mover_lob: {
        // fall, turn along the velocity, move
        shot.velocity.y -= lob_fall_60;
        shot.basis = mat3_from_dir_up(shot.velocity, up_axis);
        shot.origin = shot.pos;
        step_back(shot);
        shot.pos += shot.velocity - FxVec3{Fx{}, lob_fall_half_60, Fx{}};
        break;
    }
    case mover_guided:
    case mover_unguided: {
        // a target in camouflage states 1..4 drops the lock for good: the mover becomes
        // _guided_next2 after this move, which neither turns nor accelerates
        if (shot.mover == mover_guided && shot.target >= 0 &&
            camo_hides(world.fighters[static_cast<std::size_t>(shot.target)].camo)) {
            step_back(shot);
            shot.pos += shot.velocity;
            shot.origin = shot.pos;
            shot.mover = mover_unguided;
            break;
        }
        // move first, then turn (guided) and accelerate
        const Fx before = length(shot.velocity);
        const Fx dv = accel_part(before, next_speed(shot, w), w);
        step_back(shot);
        shot.pos += shot.velocity - scaled_to(shot.velocity, dv / 2);
        shot.origin = shot.pos;
        if (shot.mover == mover_guided && shot.target >= 0) {
            const Fighter& target = world.fighters[static_cast<std::size_t>(shot.target)];
            const FxVec3 to = target.combat.aim_point - shot.pos;
            const Angle off = angle_between(to, shot.velocity);
            // turn at most ticks x turn_rate (auto), kept only under 90 degrees
            FxVec3 z = normalized(to);
            if (w.turn_rate.raw < off.raw) {
                z = turned_toward(shot.basis.z, to, off, w.turn_rate);
            }
            if (off.raw < angle_quarter.raw && to != FxVec3{}) {
                shot.basis = mat3_from_dir_up(z, up_axis);
            }
        }
        shot.velocity = shot.basis.z * next_speed(shot, w);
        break;
    }
    case mover_blast:
        // impact_volume_callback: the damage and the radius from the life left
        shot.damage = fx_trunc_div(w.impact_damage, fx(1));
        shot.radius = impact_radius_at(shot.life, w);
        break;
    default:
        break;
    }
}
