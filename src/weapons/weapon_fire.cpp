// The triggers and the weapon controllers (decomp src/hand/mech_part_assets.c
// actor_triggers_update; src/hand/weapon_laser.c weapon_fire_controller,
// weapon_laser_controller, weapon_melee_controller, weapon_reach_update;
// docs/port/weapons.md "Triggers", "The fire controller", "Beams", "Melee blades").
#include "mech/fighter_rates.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <algorithm>

namespace {

// arm raise: 0..5, +1 an update while pressed, fires at 5; R12 D: 0..10 a step, fires at 10
// (sixty.md "Weapons and triggers"; arm_raise_full in combat.hpp)
constexpr int raise_full = arm_raise_full;
// the volley gap: volley_timer = 10 ticks, -= ticks the same update; at 60 Hz 10 and -1 a step
// gives one missile every 10 ticks as retail (R12 B, exact)
constexpr int volley_gap = 10;
// a beam's intensity rises by ticks a firing update to 8
constexpr int beam_intensity_max = 8;
// the melee reach the AI uses (0x41700000)
constexpr Fx melee_reach = fx(15);
// g30 with retail's literal 2 for ticks: the lob reach is a distance and keeps retail's constants
// (sixty.md "Reach")
constexpr Fx lob_reach_gravity = fx_f(9.8 / 900.0 * 2.5);

bool trigger_ready(const Fighter& f, const Weapon& w) {
    return f.combat.triggers[w.mount].fire != 0 && w.fitted && w.resource >= fx(1) &&
           f.combat.alive;
}

// reload: reload_left = R on firing, -= ticks only on updates that don't fire: a shot every
// ceil(R/2) + 1 updates. R12 C: 2 ceil(R/2) + 1 at 60 Hz, -1 on steps that don't fire
// (sixty.md "Reload")
int reload_steps(int reload_frames) {
    return 2 * ((reload_frames + 1) / 2) + 1;
}

// the pellet's turn off the barrel: tilt (r - 0.5) x spread about x, spin r x 2 pi about z, two
// draws in that order. m = Rx(tilt) Rz(spin) m. In the port's mirrored frame the spin turns the
// other way round the barrel; the spread is the same
FxMat3 pellet_turn(World& world, const Weapon& w, const FxMat3& basis) {
    const int tilt_roll = rand_table_next(world.random);
    const Angle tilt = angle_scaled(w.pellet_spread, fx_ratio(tilt_roll, 32767) - fx_f(0.5));
    const int spin_roll = rand_table_next(world.random);
    const Angle spin{
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(spin_roll) << 32) / 32767)};
    return mat3_mul(mat3_rotation_x(tilt), mat3_mul(mat3_rotation_z(spin), basis));
}

// behaviours 0..4
// decomp: weapon_fire_controller 0x9B46D
void fire_controller(World& world, Fighter& f, Weapon& w) {
    const WeaponRecord& r = *w.record;
    if ((trigger_ready(f, w) && w.reload_left <= 0) || (w.volley_active && w.volley_timer <= 0)) {
        bool fired = false;
        weapon_reload_pulse(world, f, w); // the model's shot clip
        // a back mount's missiles or grenades: BARK_FIRED
        if ((w.mount == 2 || w.mount == 3) && (r.behavior == 1 || r.behavior == 3)) {
            fighter_bark(f, bark_fired);
        }
        if (r.behavior == 1 && w.turn_rate != Angle{}) {
            w.volley_active = true;
        }
        for (int i = 0; i < w.shots; ++i) {
            if (w.resource > Fx{}) {
                const int index = shot_alloc(world);
                FxMat4 m;
                if (index >= 0 &&
                    weapon_muzzle_matrix(world, f, w, &m, i == 0 || r.behavior != 2, true)) {
                    fired = true;
                    if (i > 0) {
                        const FxMat3 turned = pellet_turn(world, w, mat4_basis(m));
                        m = mat4_from(turned, m.w);
                    }
                    w.lock_target = r.behavior == 1 ? weapon_lock_take(f, w.mount) : -1;
                    shot_launch(world, f, w, weapon_ref(f.slot, w.mount),
                                world.combat.shots[static_cast<std::size_t>(index)], m);
                    if (r.behavior != 2) {
                        w.resource -= fx(1);
                    }
                }
            }
            // a volley fires one missile a step, whether or not one left
            if (w.volley_active) {
                w.volley_index += 1;
                w.volley_timer = volley_gap;
                if (w.volley_index >= w.shots) {
                    w.volley_active = false;
                    w.volley_index = 0;
                    w.volley_timer = 0;
                }
                break;
            }
        }
        w.lock_target = -1;
        w.reload_left = reload_steps(w.reload_frames);
        // pellets pay one round for the pull (the casing is cosmetic)
        if (fired && r.behavior == 2) {
            w.resource -= fx(1);
        }
    } else if (w.reload_left > 0) {
        w.reload_left -= 1;
    }
    if (w.volley_active) {
        w.volley_timer -= 1;
    }
    weapon_reach_update(w);
}

// resource_max / recharge_frames an update; R1: half a step
// decomp: weapon_restore_resource 0x97705
void restore_resource(Weapon& w) {
    if (w.record->behavior != 5) {
        return;
    }
    if (w.recharge_frames > Fx{}) {
        w.resource += fx_div_near(fx(w.resource_max), w.recharge_frames * 2);
        w.resource = fx_min(w.resource, fx(w.resource_max));
    } else {
        w.resource = fx(w.resource_max);
    }
}

// behaviour 5. The beam is one shot the controller places; its
// intensity is reload_left
// decomp: weapon_laser_controller 0x9B97E
void laser_controller(World& world, Fighter& f, Weapon& w) {
    const bool held = trigger_ready(f, w);
    const bool fresh = w.beam_shot < 0;
    bool fired = false;
    if (held) {
        // a beam starts only from an exactly full gauge
        int index = w.beam_shot;
        if (index < 0 && w.resource == fx(w.resource_max)) {
            index = shot_alloc(world);
        }
        FxMat4 m;
        if (index >= 0 && weapon_muzzle_matrix(world, f, w, &m, true, true)) {
            fired = true;
            w.beam_shot = index;
            Shot& beam = world.combat.shots[static_cast<std::size_t>(index)];
            beam.weapon = weapon_ref(f.slot, w.mount);
            beam.basis = mat4_basis(m);
            beam.origin = m.w;
            if (w.reload_left < beam_intensity_max) {
                w.reload_left += 1;
            }
            w.resource -= fx(1); // -= ticks: auto (R3)
            beam.state = 1;
        }
    } else if (w.reload_left > 0) {
        w.reload_left -= 1;
    }
    // retail keeps the beam while the intensity is over 0 and lingers 6 ticks after a release;
    // stepping by 1 that would be 7, so a released beam goes at 1 or under (sixty.md "Beams")
    const bool lit = w.beam_shot >= 0 && w.reload_left > (held ? 0 : 1);
    if (lit) {
        Shot& beam = world.combat.shots[static_cast<std::size_t>(w.beam_shot)];
        // the length grows ticks x accel an update while firing (auto), up to speed_max
        if (fired) {
            beam.radius += w.accel;
        }
        beam.radius = fx_min(beam.radius, w.speed_max);
        beam.prev_pos = beam.pos;
        beam.pos = beam.origin + beam.basis.z * beam.radius;
        // a new beam's first sweep: retail's runs from the cleared shot's prev_pos, the world's
        // origin, to the tip; the port starts it at the tip (docs/bugs.md)
        if (fresh) {
            beam.prev_pos2 = beam.pos;
        }
    } else {
        if (w.beam_shot >= 0) {
            shot_clear(world.combat.shots[static_cast<std::size_t>(w.beam_shot)]);
            w.beam_shot = -1;
        }
        w.reload_left = 0;
        restore_resource(w);
    }
    weapon_reach_update(w);
}

// behaviour 6. The blade is a shot at the muzzle while the
// swing (motion state 9) runs
// decomp: weapon_melee_controller 0x9C2B7
void melee_controller(World& world, Fighter& f, Weapon& w) {
    FighterCombat& c = f.combat;
    const BattleData& data = *world.battle;
    if (c.triggers[w.mount].pressed && c.alive) {
        weapon_reload_pulse(world, f, w);
        const int index = w.beam_shot >= 0 ? w.beam_shot : shot_alloc(world);
        if (index >= 0 &&
            !state_in(f.motion_state, {state_melee, state_stagger, state_dodge, state_dead}) &&
            w.fitted && w.resource >= fx(1)) {
            w.beam_shot = index;
            Shot& blade = world.combat.shots[static_cast<std::size_t>(index)];
            blade.state = 1;
            blade.weapon = weapon_ref(f.slot, w.mount);
            c.melee_active = true;
            FxMat4 m;
            if (weapon_muzzle_matrix(world, f, w, &m, true, true)) {
                blade.pos = blade.prev_pos = blade.prev_pos2 = blade.origin = m.w;
                c.melee_endpoint = index;
            }
            const int clip = c.melee_clip[w.mount] + (f.grounded ? 0 : 0xA);
            motion_set_animation(f, data, clip, state_melee, motion_lead_in, Fx{});
            if (c.hud.melee_locked) {
                c.melee_connected = true;
                c.melee_target_stored =
                    c.melee_target >= 0
                        ? world.fighters[static_cast<std::size_t>(c.melee_target)].combat.aim_point
                        : f.position;
            } else {
                c.melee_connected = false;
            }
        }
    }
    if (w.beam_shot >= 0) {
        Shot& blade = world.combat.shots[static_cast<std::size_t>(w.beam_shot)];
        if (blade.state == 1 && f.motion_state == state_melee) {
            // the blade follows the muzzle every step (the lunge measures from it); the shot pass
            // tests it on even steps from where it was two steps back (sixty.md "Contact damage")
            FxMat4 m;
            if (weapon_muzzle_matrix(world, f, w, &m, true, true)) {
                blade.prev_pos = blade.pos;
                blade.pos = m.w;
            }
            blade.origin = blade.pos;
        }
        // retail clears the blade only at the swing's mode 3 or the owner's death, so a swing cut
        // by a stagger leaves a frozen live blade and melee_active set (weapons.md "Retail
        // behaviour" 12); the port clears it whenever the swing is over (docs/bugs.md)
        if (f.motion_mode == motion_lead_out || !c.alive || f.motion_state != state_melee) {
            shot_clear(blade);
            w.beam_shot = -1;
            if (f.motion_state != state_melee) {
                c.melee_active = false;
            }
            if (c.melee_connected) {
                c.melee_connected = false;
                c.hud.warning = LockMark{};
                c.hud.melee_locked = false;
            }
            c.melee_endpoint = -1;
        }
    }
    weapon_reach_update(w);
}

} // namespace

// decomp: actor_triggers_update 0x7645E
void fighter_triggers_update(Fighter& f) {
    FighterCombat& c = f.combat;
    const std::uint32_t buttons = f.control.control_buttons;
    for (int mount = 2; mount < 4; ++mount) {
        MountTrigger& t = c.triggers[mount];
        t.fire = (buttons & mount_fire_bits[mount]) ? (t.fire == 0 ? 3 : 2) : 0;
    }
    for (int mount = 0; mount < 2; ++mount) {
        MountTrigger& t = c.triggers[mount];
        if (buttons & mount_fire_bits[mount]) {
            t.pressed = true;
        }
        if (t.pressed) {
            if (t.raise == raise_full) {
                t.fire = t.fire == 0 ? 3 : 2;
                t.pressed = false;
            } else {
                t.raise += 1;
                t.fire = 0;
            }
        } else {
            t.raise -= 1;
            t.fire = 0;
        }
        t.raise = std::clamp(t.raise, 0, raise_full);
    }
}

void weapon_controller(World& world, Fighter& f, int mount) {
    Weapon& w = f.combat.weapons[mount];
    // a mount with no part, or a broken weapon, has its task paused
    if (!w.present || !w.record || f.combat.weapon_broken[mount]) {
        return;
    }
    switch (w.record->behavior) {
    case 5:
        laser_controller(world, f, w);
        break;
    case 6:
        melee_controller(world, f, w);
        break;
    default:
        fire_controller(world, f, w);
        break;
    }
}

// decomp: weapon_reach_update 0x9D3AF
void weapon_reach_update(Weapon& w) {
    if (!w.record) {
        return;
    }
    const WeaponRecord& r = *w.record;
    w.reach_min = Fx{};
    if (r.behavior == 5) {
        w.reach_max = w.speed_max;
        return;
    }
    if (r.behavior == 6) {
        w.reach_max = melee_reach;
        return;
    }
    const Fx life = fx(w.shot_life);
    if (r.delivery == 1 && w.mount >= 2) {
        // weapon_reach_update's lob: per-tick speeds against a per-update gravity, half the real
        // range; kept as retail has it (the AI's lob reach, docs/bugs.md)
        const Fx vx = fx_mul_near(fx_cos(w.launch_pitch), w.speed);
        const Fx sin_pitch = fx_sin(w.launch_pitch);
        Fx range = fx(10);
        if (sin_pitch >= Fx{}) {
            const Fx flight = fx_div_near(vx, lob_reach_gravity);
            range = fx_abs(fx_mul_near(fx_mul_near(sin_pitch, w.speed) * 2, flight));
        }
        w.reach_max = w.blast_radius_end + range + fx(5);
        w.reach_min = fx_max(Fx{}, range - w.blast_radius_end - fx(5));
        return;
    }
    // weapon_reach_straight, continuous ticks
    if (w.accel == Fx{}) {
        w.reach_max = fx_mul_near(w.speed, life);
        return;
    }
    const Fx t = fx_div_near(w.speed_max - w.speed, w.accel);
    const auto ramp = [&](Fx time) {
        return fx_mul_near(w.speed, time) +
               fx_mul_near(fx_mul_near(w.accel, fx_mul_near(time, time)), fx_f(0.5));
    };
    w.reach_max = t <= life ? ramp(t) + fx_mul_near(life - t, w.speed_max) : ramp(life);
}
