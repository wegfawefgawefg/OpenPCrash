// A fighter's own effects (actor_effects_update with effect_jump_fire, effect_down_vernier,
// effect_jump_dust, effect_muffler_smoke, effect_dash_end_smoke, effect_foot_landing,
// effect_spark_foot, effect_hover_wait; decomp src/hand/mech_part_assets.c,
// src/hand/attachment_lights.c, docs/port/effects.md "A fighter's effects"), driven from the
// world's fighters each update.
//
// The attachments are the fighter's live pose (FighterPoseState, built in the step by
// actor_attachments_update: sv_leg_attachment_table's point plus its direction under the
// leg graph node flagged 0x40000 << n, and the feet 14..19 the walk's footstep nodes). The motion
// events the original's motion code raises (ActorEffects.motion_events) are read off the
// fighter's state changes between two updates, the feet's landings (0x400 << foot) off the steps'
// footsteps (view/footsteps.hpp, gathered by effects_step).
#include "effects/effects.hpp"
#include "mech/fighter_rates.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

struct Attach {
    bool set = false;
    Vec3f at, before, dir; // original frame
};

} // namespace

// decomp: matrix_from_direction 0x880CA
Mat4 from_direction(Vec3f dir, int kind) {
    const Vec3f up = dir.y * dir.y < 0.9f ? Vec3f{0, 1, 0} : Vec3f{0, 0, 1};
    if (kind == 1) {
        dir.y *= 0.1f;
        dir = vec_normalize(dir);
    }
    const Vec3f z = vec_normalize(vec_cross(up, dir));
    const Vec3f x = vec_normalize(vec_cross(dir, z));
    const Vec3f y = vec_cross(z, x);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    return m;
}

void fighter_effects_update(Effects& fx, const World& world) {
    if (!world.battle) {
        return;
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        FighterEffects& e = fx.fighters[slot];
        if (!f.present) {
            // the slot ended: its wreck's burnouts too (retail's stop_all leaves them: bugs.md)
            if (e.seen) {
                for (EffectHandle& h : e.burnout) {
                    effect_stop(fx.pool, h);
                    h = {};
                }
                e.wrecked = false;
            }
            e.seen = false;
            for (bool& seen : e.seen_foot) {
                seen = false;
            }
            e.footsteps = 0;
            continue;
        }
        // this update's attachments 0..13 from the pose (13 is an offset from the position, not a
        // point: none of the emitters read it), and where they were the last update
        Attach a[14];
        bool posed = false;
        for (int n = 0; n < 13; ++n) {
            const FighterAttachment& pa = f.pose.attachments[n];
            if (pa.kind < 0) {
                continue;
            }
            posed = true;
            a[n].set = true;
            a[n].at = original_of(pa.position);
            a[n].before = e.seen ? e.attach_before[n] : a[n].at;
            a[n].dir = pa.kind == 1 || pa.kind == 2 ? original_of(pa.direction) : Vec3f{0, 1, 0};
            e.attach_before[n] = a[n].at;
        }
        const int state = f.motion_state;
        const bool first = !e.seen;
        e.seen = true;
        const int group = f.leg_kind <= 2 ? 0 : f.leg_kind <= 6 ? 1 : 2;
        // ground speed in km/h: units a step, x 60 x 3.6
        const float speed_kmh = std::sqrt(fx_float(f.velocity.x) * fx_float(f.velocity.x) +
                                          fx_float(f.velocity.z) * fx_float(f.velocity.z)) *
                                60.0f * 3.6f;

        // the motion events, from the state's changes since the last update
        unsigned events = 0;
        if (!first) {
            if (state == state_jump && e.motion_state != state_jump) {
                events |= motion_event_jump | motion_event_jump_dust;
            }
            if (f.dive_used && !e.dive_used) {
                events |= motion_event_dive;
            }
            if (state == state_dodge && e.motion_state != state_dodge) {
                // the vernier pair on the side it dodges from
                events |= fx_float(f.dodge_impulse.x) < 0 ? motion_event_vernier_left
                                                          : motion_event_vernier_right;
            }
            const bool running = state == state_run || state == state_run_reverse;
            const bool was_running =
                e.motion_state == state_run || e.motion_state == state_run_reverse;
            if (running && !was_running) {
                events |= motion_event_run_start;
            }
            if (!running && was_running && f.grounded) {
                events |= motion_event_run_stop;
            }
        }
        // the feet that stepped since the last update (0x400 << foot), the only writer of those
        // bits: a landing alone starts none (actor_land leaves the phase where it was)
        events |= e.footsteps * motion_event_foot_0;
        const std::uint32_t footsteps = e.footsteps;
        e.footsteps = 0;
        if (events && std::getenv("PHANTOMCRASH_FX_TRACE")) {
            std::printf("fx step %lld fighter %d: events %x state %d attachments %s feet %d\n",
                        static_cast<long long>(world.step), slot, events, state,
                        posed ? "yes" : "no", f.pose.foot_count);
        }
        e.motion_state = state;
        e.airborne = f.airborne;
        e.dive_used = f.dive_used;

        // the foot sparks' windows (0x80 or 0x200 opens 10 updates, a dodge's verniers 1)
        if (e.spark_window >= 0) {
            e.spark_window -= 1;
        }
        if (events & motion_event_run_stop) {
            e.spark_window = 10;
        }
        if (e.spark_hold >= 0) {
            e.spark_hold -= 1;
        }
        if (events & motion_event_verniers) {
            e.spark_hold = 1;
        }
        if (events & motion_event_jump_dust) {
            for (int& t : e.dust_timer) {
                t = 0;
            }
        }
        // the feet: attachments 14..19, the walk's footstep points (sparks at speed on wheels and
        // skates, landings, jump dust)
        const int feet = f.pose.foot_count < 6 ? f.pose.foot_count : 6;
        for (int foot = 0; foot < feet; ++foot) {
            Attach ft;
            ft.set = true;
            ft.at = original_of(f.pose.attachments[14 + foot].position);
            ft.before = e.seen_foot[foot] ? e.foot_before[foot] : ft.at;
            e.foot_before[foot] = ft.at;
            e.seen_foot[foot] = true;
            if (f.grounded && group == 0 && speed_kmh > 35.0f) {
                const bool held = f.leg_kind == 0 && e.spark_hold >= 0;
                if (held || (f.leg_kind ? (state == state_dodge || e.spark_window >= 0)
                                        : e.spark_window >= 0)) {
                    if (!held || !effect_emitter(fx.pool, e.spark[foot])) {
                        Vec3f out = vec_normalize(vec_sub(ft.at, ft.before));
                        out = vec_normalize(vec_sub({0, 1, 0}, vec_scale(out, held ? 1.5f : 2.5f)));
                        const EffectHandle h =
                            effect_set_play(fx.pool, held ? 0x21 : 0x20, ft.at, ft.before);
                        effect_set_matrix(fx.pool, h, from_direction(out, 2));
                        if (held) {
                            e.spark[foot] = h;
                        }
                    }
                }
            }
            if (footsteps & (1u << foot)) {
                // effect_foot_landing: the set by the state and the leg group
                const int set = state >= 0 && state < 15 ? fx.data.foot_landing[state][group] : -1;
                if (set != -1) {
                    const EffectHandle h = effect_set_play(fx.pool, set, ft.at, ft.before);
                    Vec3f up{0, 1, 0};
                    if (set == 0x20) {
                        up = vec_normalize(
                            vec_sub(up, vec_scale(vec_normalize(vec_sub(ft.at, ft.before)), 2.0f)));
                    }
                    effect_set_matrix(fx.pool, h, from_direction(up, set == 0x20 ? 2 : 0));
                    effect_set_offset(fx.pool, h, vec_add(ft.before, {0, 2.8f, 0}));
                }
            }
            // jump dust for 31 updates, unless the legs hover
            if (e.dust_timer[foot] >= 0) {
                if (group != 2 && e.dust_feet[foot].slot == -1) {
                    e.dust_feet[foot] = effect_set_play(fx.pool, 0x1A, ft.at, ft.before);
                    effect_set_matrix(fx.pool, e.dust_feet[foot], from_direction({0, 1, 0}, 0));
                }
                if (e.dust_timer[foot]++ >= 0x1E) {
                    e.dust_timer[foot] = -1;
                    effect_finish(fx.pool, e.dust_feet[foot]);
                }
            }
            if (e.dust_feet[foot].slot != -1) {
                if (!effect_active(fx.pool, e.dust_feet[foot])) {
                    e.dust_feet[foot] = {};
                } else {
                    effect_set_position(fx.pool, e.dust_feet[foot], ft.at);
                }
            }
        }

        // jump fire on attachments 4 and 5, along their direction
        if (events & motion_event_jump) {
            for (int k = 0; k < 2; ++k) {
                const Attach& j = a[4 + k];
                if (!j.set) {
                    continue;
                }
                effect_stop(fx.pool, e.jump_fire[k], true);
                e.jump_fire[k] = effect_set_play(fx.pool, 0x17, vec_add(j.at, j.dir), j.before);
                effect_set_matrix(fx.pool, e.jump_fire[k], from_direction(j.dir, 0));
            }
        }
        // the dodge's verniers: two 0x18, one along the attachment and one flattened, both kept
        // and followed (retail's caller passes one handle twice and loses the first)
        for (int k = 0; k < 2; ++k) {
            if (!(events & (k == 0 ? motion_event_vernier_right : motion_event_vernier_left)) ||
                !a[4 + k].set) {
                continue;
            }
            const Attach& j = a[4 + k];
            effect_stop(fx.pool, e.jump_fire[k], true);
            effect_stop(fx.pool, e.vernier[k], true);
            const Vec3f pos = vec_add(j.at, j.dir);
            e.vernier[k] = effect_set_play(fx.pool, 0x18, pos, j.before);
            effect_set_matrix(fx.pool, e.vernier[k], from_direction(j.dir, 0));
            e.jump_fire[k] = effect_set_play(fx.pool, 0x18, pos, j.before);
            effect_set_matrix(fx.pool, e.jump_fire[k], from_direction(j.dir, 1));
        }
        // the dive: 0x18 on attachments 6 and 7 after stopping that side's jump fire
        if (events & motion_event_dive) {
            for (int k = 0; k < 2; ++k) {
                const Attach& j = a[6 + k];
                if (!j.set) {
                    continue;
                }
                effect_stop(fx.pool, e.dive[k], true);
                effect_stop(fx.pool, e.jump_fire[k], true);
                e.jump_fire[k] = {};
                e.dive[k] = effect_set_play(fx.pool, 0x18, vec_add(j.at, j.dir), j.before);
                effect_set_matrix(fx.pool, e.dive[k], from_direction(j.dir, 0));
            }
        }
        // the mufflers, 8..12 (8 skipped for body class 1): smoke at a dash's start, then a puff
        // an update for 6..34 updates (EMUFSMOKE02: retail calls the puff with no owner, so it
        // never shows; docs/bugs.md), the end smoke at its end
        for (int k = 0; k < 5; ++k) {
            const Attach& m = a[8 + k];
            if (!m.set || (k == 0 && f.body_class == 1)) {
                continue;
            }
            if (events & motion_event_run_start) {
                // both of the last smokes stopped (retail tests the first handle twice)
                effect_stop(fx.pool, e.muffler[k][0], true);
                effect_stop(fx.pool, e.muffler[k][1], true);
                e.muffle_timer[k] = 0;
                e.muffler[k][0] = effect_set_play(fx.pool, 0x11, m.at, m.before);
                e.muffler[k][1] = effect_set_play(fx.pool, 0x12, m.at, m.before);
                effect_set_matrix(fx.pool, e.muffler[k][0], from_direction(m.dir, 0));
                effect_set_matrix(fx.pool, e.muffler[k][1], from_direction(m.dir, 0));
            }
            if (e.muffle_timer[k] >= 0) {
                e.muffle_timer[k] += 1;
                if (e.muffle_timer[k] > 5 && e.muffle_timer[k] < 0x23) {
                    const EffectHandle puff = effect_set_play(fx.pool, 0x13, m.at, m.before);
                    effect_set_matrix(fx.pool, puff, from_direction(m.dir, 0));
                } else if (e.muffle_timer[k] >= 0x23) {
                    e.muffle_timer[k] = -1;
                }
            }
            if (events & motion_event_run_stop) {
                e.muffle_timer[k] = -1;
                effect_stop(fx.pool, e.muffler[k][2], true);
                e.muffler[k][2] = effect_set_play(fx.pool, 0x19, m.at, m.before);
                effect_set_matrix(fx.pool, e.muffler[k][2], from_direction(m.dir, 0));
            }
            for (EffectHandle& h : e.muffler[k]) {
                if (h.slot != -1 && !effect_active(fx.pool, h)) {
                    h = {};
                }
                effect_set_position(fx.pool, h, m.at);
            }
        }
        // the jets and verniers follow 0.1 out along their attachment
        for (int k = 0; k < 2; ++k) {
            const Attach& j = a[4 + k];
            if (j.set) {
                if (e.jump_fire[k].slot != -1 && !effect_active(fx.pool, e.jump_fire[k])) {
                    e.jump_fire[k] = {};
                }
                effect_set_position(fx.pool, e.jump_fire[k], vec_add(j.at, vec_scale(j.dir, 0.1f)));
                effect_set_position(fx.pool, e.vernier[k], vec_add(j.at, vec_scale(j.dir, 0.1f)));
            }
            const Attach& d = a[6 + k];
            if (d.set) {
                if (e.dive[k].slot != -1 && !effect_active(fx.pool, e.dive[k])) {
                    e.dive[k] = {};
                }
                effect_set_position(fx.pool, e.dive[k], vec_add(d.at, vec_scale(d.dir, 0.1f)));
            }
        }

        // the hover wash (hover legs, after 10 updates, alive): layers at the ground under it,
        // 0.13 up; resumed and moved rather than played again
        const bool hover = group == 2 && f.combat.alive && e.age > 10;
        e.age += 1;
        if (hover && f.grounded) {
            const bool moving = state == state_run || state == state_run_reverse ||
                                f.motion_state == 0 || f.motion_state == 1;
            const int layers = moving ? 1 : 4;
            Vec3f at = original_of(f.position);
            at.y += 0.13f;
            for (int k = 0; k < 4; ++k) {
                EffectHandle& h = e.hover[k];
                if (k >= layers) {
                    effect_finish(fx.pool, h);
                    continue;
                }
                if (Emitter* em = effect_emitter(fx.pool, h)) {
                    effect_resume(fx.pool, h);
                    em->position = at;
                } else {
                    h = effect_set_play(fx.pool, 0x1E, at, at);
                    effect_set_matrix(fx.pool, h, mat_identity());
                }
            }
        } else {
            for (EffectHandle& h : e.hover) {
                effect_finish(fx.pool, h);
            }
        }
    }
}
