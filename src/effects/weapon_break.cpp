// A broken weapon's sparks and arcs (decomp src/hand/mech_part_assets.c
// actor_effects_update's weapon_state switch, src/hand/attachment_lights.c effect_arc_emission,
// effect_arc_emission_between, effect_arc_emission_2, effect_arm_break,
// effect_arm_break_follow; docs/port/effects.md "A fighter's effects", sets 0x26..0x2C).
// The break itself is the world's (weapon_broken, event_weapon_break); the four states it walks
// through are presentation: 1 sparking for 15 updates, 2 the break, 3 the break playing out, 4
// for the rest of the battle an arm's arcs relit after random delays. Retail keeps one [4][4]
// table of arcs whose rows 2 and 3 serve both an arm's hang side and the back mounts, which index
// it past its end (rows 4 and 5); here each mount has its own (docs/bugs.md).
#include "effects/effects.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int break_updates = 15;      // actor_weapon_break_apply's weapon_break_timer
constexpr float arc_stop_kmh = 350.0f; // the elbow arc goes out above this

Mat4 floats_of(const FxMat4& m) {
    Mat4 out;
    const FxVec3 rows[4] = {m.x, m.y, m.z, m.w};
    for (int r = 0; r < 4; ++r) {
        out.m[r][0] = fx_float(rows[r].x);
        out.m[r][1] = fx_float(rows[r].y);
        out.m[r][2] = fx_float(rows[r].z);
    }
    return out;
}

Vec3f row(const Mat4& m, int r) {
    return {m.m[r][0], m.m[r][1], m.m[r][2]};
}

// a live handle, or none once its emitter has gone (the arcs' test)
bool live(Effects& fx, EffectHandle& h) {
    if (h.slot != -1 && !effect_emitter(fx.pool, h)) {
        h = {};
    }
    return h.slot != -1;
}

// effect_arc_emission (set 0x26) and _2 (0x28, the elbow): played at the point, or moved there;
// -1 once the emitter has died
bool arc_at(Effects& fx, EffectHandle& h, int set, const Vec3f& at, const Vec3f& before) {
    if (h.slot != -1) {
        if (!live(fx, h)) {
            return false;
        }
        effect_set_position(fx.pool, h, at);
        return true;
    }
    h = effect_set_play(fx.pool, set, at, before);
    return true;
}

// effect_arc_emission_between: a chain (emitter flag 0x1000) whose matrix rows are its start, its
// direction and two jitter axes. The first kind (0x27) runs from a point near the hang toward
// the aim frame; the second (0x29) leaves the hang in a fresh random direction every update
bool arc_between(Effects& fx, EffectHandle& h, Vec3f& offset, const Mat4& mount,
                 const Mat4& other, int side, bool second) {
    if (h.slot != -1) {
        if (!live(fx, h)) {
            return false;
        }
    } else {
        // the offset rolled once, z then y then x, within 0.1
        offset.z = rand_range(fx.seed, -1.0f, 1.0f) * 0.1f;
        offset.y = rand_range(fx.seed, -1.0f, 1.0f) * 0.1f;
        offset.x = rand_range(fx.seed, -1.0f, 1.0f) * 0.1f;
        h = effect_set_play(fx.pool, second ? 0x29 : 0x27, offset, offset);
    }
    const Vec3f pos = transform_coord(offset, mount);
    Mat4 m = mat_identity();
    Vec3f rows[4];
    if (!second) {
        rows[0] = pos;
        rows[1] = vec_scale(vec_sub(row(other, 3), row(mount, 3)), 0.7f);
        rows[2] = row(mount, 1);
        rows[3] = row(mount, 2);
    } else {
        Vec3f out = row(mount, 0);
        out.x += rand_range(fx.seed, -0.8f, 0.8f);
        out.y += rand_range(fx.seed, -0.8f, 0.8f);
        out.z += rand_range(fx.seed, -0.8f, 0.8f);
        out = vec_scale(vec_normalize(out), rand_range(fx.seed, 0.3f, 0.7f));
        if (side % 2 == 0) {
            out = vec_scale(out, -1.0f);
        }
        const float range = rand_range(fx.seed, 0.7f, 1.0f);
        rows[0] = vec_add(vec_scale(out, range), pos);
        rows[1] = out;
        rows[2] = row(mount, 2);
        rows[3] = row(mount, 1);
    }
    for (int r = 0; r < 4; ++r) {
        m.m[r][0] = rows[r].x;
        m.m[r][1] = rows[r].y;
        m.m[r][2] = rows[r].z;
    }
    effect_set_matrix(fx.pool, h, m);
    return true;
}

// effect_arm_break: the held pair 0x2B and 0x2C and a one-shot 0x2A, unless the pair is live
void arm_break(Effects& fx, EffectHandle& a, EffectHandle& b, const Vec3f& at) {
    if (a.slot != -1 || b.slot != -1) {
        return;
    }
    a = effect_set_play(fx.pool, 0x2B, at, at);
    b = effect_set_play(fx.pool, 0x2C, at, at);
    effect_set_play(fx.pool, 0x2A, at, at);
}

// effect_arm_break_follow: kept on the point, turned along it (the attachment's direction is a
// copy of its position here, as retail fills it)
void break_follow(Effects& fx, EffectHandle& h, const Vec3f& at) {
    if (!live(fx, h)) {
        return;
    }
    effect_set_position(fx.pool, h, at);
    effect_set_matrix(fx.pool, h, from_direction(at, 0));
}

void finish(Effects& fx, EffectHandle& h) {
    if (h.slot != -1) {
        effect_finish(fx.pool, h);
        h = {};
    }
}

} // namespace

void weapon_break_event(Effects& fx, int slot, int mount) {
    if (slot < 0 || slot >= fighter_slots || mount < 0 || mount >= 4) {
        return;
    }
    FighterEffects& e = fx.fighters[slot];
    e.weapon_state[mount] = 1;
    e.break_timer[mount] = break_updates;
}

void weapon_break_update(Effects& fx, const World& world) {
    // PHANTOMCRASH_DEV_BREAK=slot,mount,update (captures): that mount's break shown then
    if (const char* test = std::getenv("PHANTOMCRASH_DEV_BREAK")) {
        int slot = 0, mount = 0, update = 30;
        std::sscanf(test, "%d,%d,%d", &slot, &mount, &update);
        if (fx.updates == update) {
            weapon_break_event(fx, slot, mount);
        }
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        FighterEffects& e = fx.fighters[slot];
        if (!f.present) {
            continue;
        }
        const float speed_kmh = std::sqrt(fx_float(f.velocity.x) * fx_float(f.velocity.x) +
                                          fx_float(f.velocity.z) * fx_float(f.velocity.z)) *
                                60.0f * 3.6f;
        for (int j = 0; j < 4; ++j) {
            BreakArcs& arcs = e.arcs[j];
            const bool arm = j < 2;
            // the ending's machine comes in with the breaks the last boss's defeat left; retail
            // carries their weapon_state too (ending_parts), long since at 4 by then
            if (world.battle_phase == 3 && f.combat.weapon_broken[j] && e.weapon_state[j] == 0) {
                e.weapon_state[j] = 4;
            }
            // the points: an arm's aim frame and hang matrices (+0x2FDC, +0x2FE4) and their
            // places; a back weapon's spun node (+0x2FEC)
            Mat4 frame = mat_identity(), hang = mat_identity();
            Vec3f at;
            if (arm) {
                const int fm = j == 0 ? mount_aim_a : mount_aim_b;
                const int hm = j == 0 ? mount_arm_a : mount_arm_b;
                if (!f.pose.mounted[fm] || !f.pose.mounted[hm]) {
                    continue;
                }
                frame = port_matrix(floats_of(f.pose.mount[fm]));
                hang = port_matrix(floats_of(f.pose.mount[hm]));
                at = row(frame, 3);
            } else {
                const int node = f.pose.back_spin_node[j - 2];
                if (node >= f.pose.weapon_node_count[j]) {
                    continue;
                }
                at = row(port_matrix(floats_of(f.pose.weapon_nodes[j][node])), 3);
            }
            const Vec3f hang_at = row(hang, 3);
            switch (e.weapon_state[j]) {
            case 1:
                // the gun smoke stopped; an arm sparks (three 0x26 at the frame, a 0x27 arc)
                effect_stop(fx.pool, e.gun_smoke[j]);
                e.gun_smoke[j] = {};
                if (arm) {
                    for (int k = 0; k < 4; ++k) {
                        if (k == 3) {
                            arc_between(fx, arcs.arc[0][3], arcs.offset[0][3], hang, frame, j + 2,
                                        false);
                        } else {
                            arc_at(fx, arcs.arc[0][k], 0x26, at, at);
                        }
                    }
                }
                if (e.break_timer[j]-- <= 0) {
                    e.weapon_state[j] = 2;
                    for (int n = 0; n < 4; ++n) {
                        if (arcs.arc[0][n].slot != -1) {
                            finish(fx, arcs.arc[0][n]);
                            arcs.delay[0][n] = 10;
                        }
                    }
                }
                break;
            case 2:
                arm_break(fx, arcs.arc[0][0], arcs.arc[0][1], at);
                e.weapon_state[j] = 3;
                break;
            case 3:
                break_follow(fx, arcs.arc[0][0], at);
                break_follow(fx, arcs.arc[0][1], at);
                if (arcs.arc[0][0].slot == -1 && arcs.arc[0][1].slot == -1) {
                    e.weapon_state[j] = 4;
                }
                break;
            case 4: {
                if (!arm) {
                    break;
                }
                // the frame side's point moved to the joint's middle; each arc relit when its
                // delay runs out, a new delay rolled when it dies
                const Vec3f mid = vec_scale(vec_add(hang_at, at), 0.5f);
                for (int m = 0; m < 4; ++m) {
                    if (arcs.arc[0][m].slot != -1 || arcs.delay[0][m] == 0) {
                        bool lit = true;
                        if (m == 3) {
                            lit = arc_between(fx, arcs.arc[0][3], arcs.offset[0][3], hang, frame,
                                              j + 2, false);
                        } else if (m == 0) {
                            // the elbow arc, out above 350 km/h. Retail reads the previous arc's
                            // result here (never assigned); this one's own (docs/bugs.md)
                            if (speed_kmh < arc_stop_kmh) {
                                lit = arc_at(fx, arcs.arc[0][0], 0x28, hang_at, hang_at);
                            } else {
                                finish(fx, arcs.arc[0][0]);
                            }
                        } else {
                            lit = arc_at(fx, arcs.arc[0][m], 0x26, mid, mid);
                        }
                        if (!lit) {
                            arcs.delay[0][m] = static_cast<int>(rand_range(
                                                   fx.seed, 0.0f, fx.data.arc_delays[j][m])) +
                                               5;
                        }
                    } else {
                        arcs.delay[0][m] -= 1;
                    }
                    if (arcs.arc[1][m].slot != -1 || arcs.delay[1][m] == 0) {
                        const bool lit = m == 3 ? arc_between(fx, arcs.arc[1][3],
                                                              arcs.offset[1][3], hang, frame,
                                                              j + 2, true)
                                                : arc_at(fx, arcs.arc[1][m], 0x26, hang_at,
                                                         hang_at);
                        if (!lit) {
                            arcs.delay[1][m] =
                                static_cast<int>(
                                    rand_range(fx.seed, 0.0f, fx.data.arc_delays[j + 2][m])) +
                                (m == 3 ? 0 : 5);
                        }
                    } else {
                        arcs.delay[1][m] -= 1;
                    }
                }
                break;
            }
            default:
                break;
            }
        }
        // a dead fighter's arcs finished, never relit
        if (!f.combat.alive) {
            for (BreakArcs& arcs : e.arcs) {
                for (int s = 0; s < 2; ++s) {
                    for (int m = 0; m < 4; ++m) {
                        if (arcs.arc[s][m].slot != -1) {
                            finish(fx, arcs.arc[s][m]);
                            arcs.delay[s][m] = -1;
                        }
                    }
                }
            }
        }
    }
}
