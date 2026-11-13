// sv_contact_route and sv_motion_integrate (decomp src/od/u_07c740.c): the move
// with gravity and the half-step correction, then the fighter's sphere against the supply craft
// and its crates (pickup_collect_test), the stage and its props (stage_props_collide_sphere). The
// contact decals (bit 0x2000) aren't ported yet.
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"
#include "stage/stage_world.hpp"

namespace {

// the contact sphere's bounce (0x3DCCCCCD)
constexpr Fx sphere_bounce = fx_f(0.1);
// the knockback impulse's cap, 5 an update; R13/R14: 5 / (1 + sqrt 0.8) (sixty.md "Knockback")
constexpr Fx impulse_cap_60 = fx_f(2.6393202);

FxVec3 halved(FxVec3 v) {
    const auto half = [](Fx a) {
        return fx_raw(static_cast<std::int32_t>(div_round(a.raw, 2)));
    };
    return {half(v.x), half(v.y), half(v.z)};
}

// gravity, the caps, then the move. Retail moves by the velocity after the
// forces; at 60 Hz that drifts by half a step's force each step, so the move is v + dv / 2, dv
// being this step's forces (sixty.md R10, "Integration"); the cap of 5 an update is 2.5 on the
// stored velocity and on the move (R14)
// decomp: sv_motion_integrate 0x7D097
void sv_motion_integrate(Fighter& f) {
    f.velocity.y -= gravity_60;
    f.force_dv.y -= gravity_60;
    f.position_prev = f.position;
    f.velocity_prev = f.velocity;
    f.velocity = clamp_length(f.velocity, velocity_cap_60);
    const FxVec3 move = clamp_length(f.velocity + halved(f.force_dv), velocity_cap_60);
    f.push = clamp_length(f.push, velocity_cap_60);
    f.impulse = clamp_length(f.impulse, impulse_cap_60);
    f.position += f.push + move + f.impulse;
}

} // namespace

void contact_route(Fighter& f, const BattleData& data, StageWorld* pickups) {
    const StageCollision& stage = data.stage;
    sv_motion_integrate(f);
    f.ground_velocity = f.velocity;
    // attachment 13's height above the position, from last step's pose
    const Fx head_height = f.head_height;
    const FxVec3 up_radius{Fx{}, f.radius, Fx{}};

    // the sphere sits on the position, its centre a radius up, swept from where it was
    SphereQuery sphere{f.position + up_radius, f.position_prev + up_radius, f.velocity, f.radius,
                       sphere_bounce};
    int hits = 0;
    // the craft's body and the crates, outside free play: the crates taken go to pickup_apply
    // after the step (stage/pickup_apply.hpp)
    for (int i = 0; i < 3; ++i) {
        f.crate_kind[i] = -1;
    }
    if (pickups && !pickups->setup.free_play) {
        const PickupResult taken = pickup_collect_test(*pickups, sphere.centre, sphere.previous.y,
                                                       f.radius, f.combat.alive);
        hits |= taken.bits;
        for (int i = 0; i < 3; ++i) {
            f.crate_kind[i] = taken.kinds[i];
            f.crate_at[i] = taken.at[i];
        }
    }
    if (!shorter_than(sphere.centre - sphere.previous, f.radius)) {
        hits |= collision_segment_slide(stage, sphere.previous, sphere.centre, sphere);
    }
    // the floor: a grounded fighter reaches down as far as it moves this step (R1 halves the
    // reach with the speed; sixty.md "floor reach")
    const Fx bottom = sphere.centre.y - sphere.radius;
    const Fx floor_reach = f.grounded ? bottom - length(f.velocity) : bottom;
    FxVec3 floor_normal;
    Fx floor_height;
    const int floor_hits = collision_floor_height(stage, sphere.centre, &floor_normal, &floor_height);
    bool on_floor = false;
    if (floor_height >= floor_reach) {
        sphere.centre.y = floor_height + sphere.radius;
        on_floor = true;
        hits |= floor_hits;
    }

    SphereQuery upper{};
    int upper_hits = 0;
    if (f.grounded) {
        hits |= collision_query_sphere(stage, sphere);
        if (!(hits & contact_floor)) {
            // no floor in reach: probe straight down as far as the move
            FxVec3 probe = sphere.centre;
            probe.y = sphere.centre.y - sphere.radius - length(f.velocity);
            FxVec3 point;
            hits |= collision_segment_contact(stage, sphere.centre, probe, &point);
            if (hits & contact_floor) {
                sphere.centre.y = point.y + sphere.radius;
                hits |= collision_query_sphere(stage, sphere);
            }
        }
    } else {
        hits |= collision_query_sphere(stage, sphere);
        if (f.motion_state != state_dead) {
            // a second sphere for the upper body, at attachment 13's height less two radii
            const FxVec3 lift{Fx{}, head_height - f.radius * 2, Fx{}};
            upper = {sphere.centre + lift, sphere.previous + lift, sphere.velocity, f.radius,
                     sphere_bounce};
            upper_hits |= collision_query_sphere(stage, upper);
        }
    }

    // the props: the sphere, and in the air the upper one, unless dead
    if (pickups && (f.grounded || f.motion_state != state_dead)) {
        hits |= stage_props_collide_sphere(*pickups, stage, sphere);
        if (!f.grounded) {
            upper_hits |= stage_props_collide_sphere(*pickups, stage, upper);
        }
    }

    // a move through a wall or into a prop throws the fighter back to where it was
    FxVec3 crossing;
    if (collision_segment_hit(stage, sphere.previous, sphere.centre, &crossing, false)) {
        sphere.centre = sphere.previous;
        hits |= contact_floor;
    }
    if (pickups && stage_props_block(*pickups, sphere.previous, sphere.centre)) {
        sphere.centre = sphere.previous;
        hits |= contact_floor;
    }
    if (upper_hits && collision_segment_hit(stage, upper.previous, upper.centre, &crossing, false)) {
        upper.centre = upper.previous;
        hits |= contact_floor;
    }

    // any floor contact grounds the fighter and stops it: ground speed is rebuilt every step.
    // Surface bit 0x40000 (airborne, gravity undone) is left out: collision_surface_bits gives
    // no type that bit (sixty.md, "Integration")
    if (on_floor || (hits & contact_floor)) {
        f.grounded = true;
        f.velocity = {};
    } else {
        f.grounded = false;
        if (upper_hits) {
            f.velocity = upper.velocity;
        } else {
            sphere.velocity.y = f.velocity.y;
            f.velocity = sphere.velocity;
        }
    }
    if (upper_hits) {
        f.position = upper.centre - FxVec3{Fx{}, head_height - f.radius, Fx{}};
    } else {
        f.position = sphere.centre - up_radius;
    }
    f.contact_flags = hits | upper_hits;
}
