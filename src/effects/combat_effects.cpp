// What the fight shows (decomp src/od/u_0657e0.c impact_effect_spawn, src/od/u_097660.c
// impact_profile_dispatch and shot_model_draw's trail, src/od/u_099770.c weapon_muzzle_effects
// and the flash models, src/hand/attachment_lights.c effect_gun_smoke, effect_get_item,
// effect_finish_started, the wreck's burnouts; docs/port/effects.md "Shots, muzzle flashes,
// casings, impacts and explosions"), from world.combat.events, the shot pool and the stage's
// Light and GetItem events.
#include "effects/effects.hpp"
#include "render/mesh_flags.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

// a frame whose second row is up and whose translation is at (the impact's matrix_top)
Mat4 surface_frame(const Vec3f& at, Vec3f up) {
    if (vec_length(up) < 1e-6f) {
        up = {0, 1, 0};
    }
    up = vec_normalize(up);
    const Vec3f helper = up.y * up.y < 0.9f ? Vec3f{0, 1, 0} : Vec3f{0, 0, 1};
    const Vec3f z = vec_normalize(vec_cross(helper, up));
    const Vec3f x = vec_normalize(vec_cross(up, z));
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = up.x, m.m[1][1] = up.y, m.m[1][2] = up.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = at.x, m.m[3][1] = at.y, m.m[3][2] = at.z;
    return m;
}

// a muzzle's matrix from its forward (the attachment matrix's third row) with y kept up
Mat4 muzzle_frame(const Vec3f& at, Vec3f forward) {
    if (vec_length(forward) < 1e-6f) {
        forward = {0, 0, 1};
    }
    const Vec3f z = vec_normalize(forward);
    const Vec3f helper = z.y * z.y < 0.9f ? Vec3f{0, 1, 0} : Vec3f{1, 0, 0};
    const Vec3f x = vec_normalize(vec_cross(helper, z));
    const Vec3f y = vec_cross(z, x);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = at.x, m.m[3][1] = at.y, m.m[3][2] = at.z;
    return m;
}

Mat4 no_move(Mat4 m) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0;
    return m;
}

// a barrel of the weapon (its current one by default) on the fighter's live pose
// (weapon_muzzle_matrix with no flash and no barrel step), original frame; false when it has none
// or the stage lies before it
bool live_muzzle(const World& world, const Fighter& f, int mount, Vec3f* at, Vec3f* forward,
                 int barrel = -1) {
    Weapon w = f.combat.weapons[mount];
    if (barrel >= 0) {
        w.muzzle = barrel;
    }
    FxMat4 m;
    if (!w.record || !weapon_muzzle_at(world, f, w, &m)) {
        return false;
    }
    *at = original_of(m.w);
    *forward = original_of(m.z);
    return true;
}

// impact_profile_dispatch's look, then impact_effect_spawn
// decomp: impact_profile_dispatch 0x98E5A, impact_effect_spawn 0x65EA8
void impact(Effects& fx, const CombatEvent& event) {
    const Vec3f at = original_of(event.position);
    const Mat4 top = surface_frame(at, original_of(event.dir));
    // a missile's hit: an orange light of 12
    if (event.flags & event_missile) {
        effects_light(fx, at, 0xFFFF8000, 12);
    }
    const int row = event.effect;
    const ImpactRow none;
    const ImpactRow& look = row >= 0 && row < static_cast<int>(fx.data.impacts.size())
                                ? fx.data.impacts[static_cast<std::size_t>(row)]
                                : none;
    const bool mark = look.mark && !(event.flags & event_alt);
    const int surface = event.surface;
    if (mark && !(surface & surface_unmarked)) {
        scorch_mark_add(fx, 0, 0.25f, top);
    }
    // special surfaces: the shockwave decal, dropped half a unit
    if (surface & surface_edge_wall) {
        Mat4 decal = top;
        decal.m[3][0] -= top.m[1][0] * 0.5f;
        decal.m[3][1] -= top.m[1][1] * 0.5f;
        decal.m[3][2] -= top.m[1][2] * 0.5f;
        contact_decal_spawn(fx, decal);
    }
    if (look.set != -1 && row != -1) {
        // impact kind 2 (a blast) lifts the set 1.5 out along the surface; only its last emitter
        // is turned
        const Vec3f pos =
            event.value == 2 ? vec_add(at, vec_scale({top.m[1][0], top.m[1][1], top.m[1][2]}, 1.5f))
                             : at;
        effects_play(fx, look.set, pos, pos, look.scale);
        effect_set_matrix_spawned(fx.pool, no_move(top));
    }
}

// the record's flash, then the gun smoke by shot model
// decomp: weapon_muzzle_effects 0x99B82
void muzzle(Effects& fx, const World& world, const CombatEvent& event) {
    const int slot = event.slot, mount = event.value;
    if (slot < 0 || slot >= fighter_slots || mount < 0 || mount >= 4) {
        return;
    }
    const Fighter& fighter = world.fighters[static_cast<std::size_t>(slot)];
    const WeaponRecord* record = fighter.combat.weapons[mount].record;
    // weapon_fire_controller throws a casing for a pull that let a shot out (one pull a mount
    // an update at most: the quickest reload is 3 steps)
    if (record && record->behavior <= 4) {
        fx.casing_due[slot][mount] = true;
    }
    if (!record || record->muzzle_fx == 6) {
        return;
    }
    // the flash at the barrel that fired (each shot of a pull its own, as weapon_muzzle_matrix
    // steps the barrel before each flash), on this update's pose (the event's place when the
    // stage now lies before it)
    Vec3f at = original_of(event.position), forward = original_of(event.dir);
    live_muzzle(world, fighter, mount, &at, &forward, event.effect);
    const Mat4 m = muzzle_frame(at, forward);
    const auto flash = [&](int model, bool billboard) {
        Effects::Flash f;
        f.slot = model;
        f.matrix = billboard ? m : mat_multiply(mat_scaling(3, 3, 3), m);
        f.billboard = billboard;
        fx.flashes.push_back(f);
    };
    switch (record->muzzle_fx) {
    case 0: // weapon_flash_model_39e: the flash x 3 and a white light of 10
        flash(0x39E, false);
        effects_light(fx, at, 0xFFFFFF, 10);
        break;
    case 1:
        flash(0x39F, false);
        break;
    case 2: // the flame at the muzzle's attachment point 1 (the muzzle here)
        flash(0x3F3, false);
        break;
    case 5: // weapon_flash_laser: a billboard quad and the absorption set
        flash(0x105, true);
        effects_play(fx, 0x23, at, at);
        break;
    default:
        break;
    }
    // effect_gun_smoke: shot models 0..6 gun smoke, 7..15 cannon smoke; a live emitter of
    // its own is re-armed rather than played again (retail re-arms whatever holds the slot now)
    const int shot_model = record->shot_model;
    if (shot_model < 0 || shot_model >= 16) {
        return;
    }
    FighterEffects& f = fx.fighters[slot];
    if (Emitter* e = effect_emitter(fx.pool, f.gun_smoke[mount])) {
        e->run_flags = (e->run_flags | run_emitting) & ~run_finishing;
        e->spawn_debt = 0;
        e->age = 0;
        e->rate = {};
    } else {
        f.gun_smoke[mount] = effect_set_play(fx.pool, shot_model < 7 ? 0x24 : 0x25, at, at);
        effect_set_matrix(fx.pool, f.gun_smoke[mount], no_move(m));
    }
}

// the wreck (actor_settle_damage at durability 0): three burnouts 2 above it, and when all
// three started a one-shot explosion
void destroyed(Effects& fx, const World& world, const CombatEvent& event) {
    if (event.slot < 0 || event.slot >= fighter_slots) {
        return;
    }
    FighterEffects& f = fx.fighters[event.slot];
    // ActorEffects::stop_all: everything but the burnouts, which play out
    for (EffectHandle& h : f.gun_smoke) {
        effect_stop(fx.pool, h);
        h = {};
    }
    for (BreakArcs& arcs : f.arcs) {
        for (int s = 0; s < 2; ++s) {
            for (int m = 0; m < 4; ++m) {
                effect_stop(fx.pool, arcs.arc[s][m]);
                arcs.arc[s][m] = {};
                arcs.delay[s][m] = -1;
            }
        }
    }
    const Vec3f at = vec_add(
        original_of(world.fighters[static_cast<std::size_t>(event.slot)].position), {0, 2, 0});
    bool all = true;
    for (int i = 0; i < 3; ++i) {
        f.burnout[i] = effect_set_play(fx.pool, 0x34 + i, at, at);
        all = all && f.burnout[i].slot != -1;
    }
    if (all) {
        effects_play(fx, 0xF, at, at);
    }
    f.wrecked = true;
}

// the set by the crate's kind, its rate written into the shared record
// decomp: effect_get_item 0x87E2C
void get_item(Effects& fx, const StageEvent& event) {
    const int kind = event.b;
    const Vec3f at = original_of(event.at);
    int set = 0x31, level = kind < 1 ? 1 : kind;
    if (kind > 6) {
        set = 0x33;
        level = kind - 6;
    } else if (kind > 3) {
        set = 0x32;
        level = kind - 3;
    }
    for (Effects::GetItem& g : fx.get_items) {
        if (g.handle.slot != -1 && effect_active(fx.pool, g.handle)) {
            continue;
        }
        g.handle = effect_set_play(fx.pool, set, at, at);
        g.slot = event.a;
        Emitter* e = effect_emitter(fx.pool, g.handle);
        if (!e) {
            g.handle = {};
            return;
        }
        // retail writes the rate into the shared record, for every later pickup too (docs/bugs.md)
        e->own_rate = static_cast<float>(level) * 2400.0f;
        return;
    }
}

} // namespace

void combat_effects_event(Effects& fx, const World& world, const CombatEvent& event) {
    switch (event.kind) {
    case event_impact:
        impact(fx, event);
        break;
    case event_muzzle:
        muzzle(fx, world, event);
        break;
    case event_destroyed:
        destroyed(fx, world, event);
        break;
    case event_weapon_break:
        weapon_break_event(fx, event.slot, event.value);
        break;
    default:
        break;
    }
}

void stage_light_event(Effects& fx, const StageEvent& event) {
    if (event.kind == StageEventKind::Light) {
        effects_light(fx, original_of(event.at), static_cast<std::uint32_t>(event.a),
                      fx_float(event.value));
    } else if (event.kind == StageEventKind::GetItem) {
        get_item(fx, event);
    }
}

void combat_effects_update(Effects& fx, const World& world) {
    // the shots with a flame (shot looks 10..15): a trail from their first draw, a point pushed
    // every second update, released when the shot ends (shot_model_draw, impact_profile_dispatch)
    for (int i = 0; i < shot_pool_size; ++i) {
        const Shot& shot = world.combat.shots[static_cast<std::size_t>(i)];
        Effects::ShotTrail& st = fx.shot_trails[static_cast<std::size_t>(i)];
        const WeaponRecord* record =
            shot.state == 1 && shot.weapon >= 0 ? weapon_at(world, shot.weapon).record : nullptr;
        const bool flame = record && record->shot_model >= 10 && record->shot_model <= 15;
        if (!flame || st.weapon != shot.weapon) {
            if (st.trail != -1) {
                trail_release(fx, st.trail);
            }
            st = Effects::ShotTrail{};
            if (!flame) {
                continue;
            }
        }
        const Vec3f at = original_of(shot.pos);
        if (st.trail == -1 && !st.started) {
            st.trail = trail_start(fx, at, 32, i);
            st.started = true;
            st.weapon = shot.weapon;
            st.tick = true;
        }
        if (st.tick) {
            trail_push_default(fx, st.trail, at);
        }
        st.tick = !st.tick;
    }

    // the pickups home in on their collector (effect_finish_started)
    for (Effects::GetItem& g : fx.get_items) {
        if (g.handle.slot == -1) {
            continue;
        }
        Emitter* e = effect_emitter(fx.pool, g.handle);
        if (!e || g.slot < 0 || g.slot >= fighter_slots) {
            g = Effects::GetItem{};
            continue;
        }
        if (e->age) {
            effect_finish(fx.pool, g.handle);
        }
        effect_set_offset(fx.pool, g.handle,
                          original_of(world.fighters[static_cast<std::size_t>(g.slot)].position));
    }

    for (int slot = 0; slot < fighter_slots; ++slot) {
        for (int mount = 0; mount < 4; ++mount) {
            if (fx.casing_due[slot][mount]) {
                fx.casing_due[slot][mount] = false;
                casing_eject(fx, world, slot, mount);
            }
        }
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& fighter = world.fighters[static_cast<std::size_t>(slot)];
        FighterEffects& f = fx.fighters[slot];
        // gun smoke follows the muzzle (effect_follow_matrix): the weapon's current barrel
        // on the live pose, turned along it, unless the stage lies between body and muzzle
        for (int mount = 0; mount < 4; ++mount) {
            Vec3f at, forward;
            if (!fighter.present || !effect_emitter(fx.pool, f.gun_smoke[mount]) ||
                !live_muzzle(world, fighter, mount, &at, &forward)) {
                continue;
            }
            effect_set_position(fx.pool, f.gun_smoke[mount], at);
            effect_set_matrix(fx.pool, f.gun_smoke[mount], from_direction(forward, 0));
        }
        // the burnouts stay at the wreck; while all three live, an orange light of 9 + rand(2)
        if (f.wrecked) {
            const Vec3f at = vec_add(original_of(fighter.position), {0, 2, 0});
            bool all = true;
            for (EffectHandle& h : f.burnout) {
                effect_set_position(fx.pool, h, at);
                all = all && effect_emitter(fx.pool, h);
            }
            if (all) {
                effects_light(fx, at, 0xFFFF8000, 9.0f + rand_float_crt(fx.seed, 2.0f));
            }
        }
    }
}

void combat_effects_queue(Effects& fx, const Mat4& view, const Vec3f& eye,
                          std::vector<PassItem>& items) {
    for (const Effects::Flash& f : fx.flashes) {
        if (f.billboard) {
            const Vec3f at{f.matrix.m[3][0], f.matrix.m[3][1], f.matrix.m[3][2]};
            effects_queue_quad(fx, f.slot, billboard_matrix(at, eye, 0, 0, 0),
                               mesh_additive | mesh_factor, flash_colour(fx, 0xFFFFFFFF), 9, view,
                               items);
        } else {
            // 0x3F3 is the flying shots' flame too, which ORs 0x8000 (additive) into the model
            // for good from the first flame drawn; the flash is always additive here (docs/bugs.md)
            const std::uint32_t flags = f.slot == 0x3F3 ? mesh_additive : 0;
            effects_queue_slot(fx, f.slot, f.matrix, 9, flags, flash_colour(fx, 0xFFFFFFFF), view,
                               items);
        }
    }
}
