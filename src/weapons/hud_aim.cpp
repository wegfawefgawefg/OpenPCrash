// The HUD's aim and lock-on (decomp src/hand/u_04a840.c: hud_update's tracking point
// from hud_reticle_track_update, hud_locks_update with hud_lock_candidates_build,
// hud_lock_try_start, weapon_lock_take, hud_lock_release, and
// hud_melee_lock_update; docs/port/weapons.md "Aim", "Lock-on"). The back weapons'
// launch_pitch is the pose's (mech/pose_weapons.cpp, actor_back_weapons_place). The senses,
// the visible list, the reticle's centre and target are battle/hud_senses.cpp's (World.huds), run
// for every slot before this.
//
// Screen points are the original's: the battle projection (52 degrees vertical over 576 x 448,
// near 0.1) on the world's chase camera; the port's mirrored frame mirrors screen x, and every
// test reads |x| or a difference. The tracking point moves every step (its speed is per tick:
// auto); the missile and melee locks run on even steps with the retail timers (weapons.md "At
// 60 Hz" 2).
#include "ai/ai_links.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// a finished lock: a human target says BARK_LOCKED_ON (0x17)
void locked_on_bark(World& world, int target) {
    if (target >= 0 && target < fighter_slots) {
        Fighter& t = world.fighters[static_cast<std::size_t>(target)];
        if (t.controller != -1) {
            fighter_bark(t, bark_locked_on);
        }
    }
}

// D3DXMatrixPerspectiveFovLH(52 degrees, 576 / 448, 0.1, 10000): y and x scales
constexpr Fx screen_y_scale = fx_f(2.0503038415792960);
constexpr Fx screen_x_scale = fx_f(2.0503038415792960 * 448.0 / 576.0);
constexpr Fx near_plane = fx_f(0.1);
// the depth kept as 64 / z so the tracking point moves it linearly between two screen points, as
// the original's NDC z moves (its share of the screen distance, under 0.003, is left out)
constexpr Fx depth_scale = fx(64);
// locks: box B under 125; the melee lock under 25 (20 for an AI); released past 90 degrees
constexpr Fx lock_range = fx(125);
constexpr Fx melee_range_human = fx(25);
constexpr Fx melee_range_ai = fx(20);
constexpr Fx quarter_turn_radians = fx_f(1.5707964);
// the timers count down by ticks an update; they run on even steps with the retail values
constexpr Fx lock_tick = fx(2);

FxMat4 view_matrix(const Fighter& f) {
    return mat4_look_at_lh(f.camera.eye, f.camera.target, f.camera.up);
}

// 64 / the view depth of a point
Fx inverse_depth(const FxMat4& view, FxVec3 p) {
    return fx_div_near(depth_scale, fx_max(transform_coord(p, view).z, near_plane));
}

FxVec3 unproject(const FxMat4& view, Fx x, Fx y, Fx inv_depth) {
    const Fx z = fx_div_near(depth_scale, fx_max(inv_depth, fx_raw(1)));
    const FxVec3 v{fx_mul_div(x, z, screen_x_scale), fx_mul_div(y, z, screen_y_scale), z};
    return transform_coord(v, mat4_rigid_inverse(view));
}

// inside the box around the reticle centre
// decomp: sense_in_reticle 0x52530
bool in_box(const FighterHud& hud, const HudSense& s, const Fx box[2]) {
    return fx_abs(s.screen_x - hud.reticle_center_x) < box[0] &&
           fx_abs(s.screen_y - hud.reticle_center_y) < box[1];
}

bool alive(const World& world, int slot) {
    return world.fighters[static_cast<std::size_t>(slot)].combat.alive;
}

// the tracking point closes on the reticle's target (or back on
// the centre) at ticks x (speed / 999 x 0.08 + 0.02) screen units a tick, snapping when closer
// decomp: hud_reticle_track_update 0x4F620
void reticle_track_update(World& world, Fighter& f) {
    FighterCombat& c = f.combat;
    LockHud& lock = c.hud;
    const FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
    const FxMat4 view = view_matrix(f);
    const Fx centre_inv = inverse_depth(view, f.reticle_center_world);
    Fx goal_x = hud.reticle_center_x, goal_y = hud.reticle_center_y, goal_inv = centre_inv;
    if (hud.reticle_target >= 0) {
        const HudSense& s = hud.sense[static_cast<std::size_t>(hud.reticle_target)];
        goal_x = s.screen_x;
        goal_y = s.screen_y;
        goal_inv = inverse_depth(
            view, world.fighters[static_cast<std::size_t>(hud.reticle_target)].combat.aim_point);
    }
    const Fx dx = goal_x - (hud.reticle_center_x + lock.track_x);
    const Fx dy = goal_y - (hud.reticle_center_y + lock.track_y);
    const Fx dz = goal_inv - (centre_inv + lock.track_inv_depth);
    const Fx root = length(FxVec2{dx, dy});
    const Fx rate = fx_mul_near(fx_ratio(c.reticle_speed, 999), fx_f(0.08)) + fx_f(0.02);
    if (rate > root) {
        lock.track_x = goal_x - hud.reticle_center_x;
        lock.track_y = goal_y - hud.reticle_center_y;
        lock.track_inv_depth = goal_inv - centre_inv;
    } else if (root > Fx{}) {
        lock.track_x += fx_mul_div(dx, rate, root);
        lock.track_y += fx_mul_div(dy, rate, root);
        lock.track_inv_depth += fx_mul_div(dz, rate, root);
    }
    c.reticle_track_world =
        unproject(view, hud.reticle_center_x + lock.track_x, hud.reticle_center_y + lock.track_y,
                  centre_inv + lock.track_inv_depth);
}

// decomp: hud_lock_release 0x4D0BD
void lock_release(LockHud& lock, int mark) {
    LockMark& m = lock.locks[mark];
    m.target = -1;
    m.timer = Fx{};
    if (m.mount < 0 || m.mount > 1) {
        return;
    }
    lock.mount_locks[m.mount] = std::max(0, lock.mount_locks[m.mount] - 1);
    if (mark == lock.mount_lock_target[m.mount]) {
        lock.mount_lock_target[m.mount] = -1;
    }
}

// decomp: hud_lock_candidates_build 0x525AD
void lock_candidates_build(const World& world, const FighterHud& hud, LockHud& lock) {
    lock.lock_candidate_count = 0;
    for (int i = 0; i < hud.visible_count; ++i) {
        const int slot = hud.visible[static_cast<std::size_t>(i)];
        const HudSense& s = hud.sense[static_cast<std::size_t>(slot)];
        if (alive(world, slot) && !camo_hides(world, slot) && !s.occluded && s.on_screen &&
            s.distance < lock_range && in_box(hud, s, lock.box_b)) {
            lock.lock_candidates[lock.lock_candidate_count++] = slot;
        }
    }
    bool all_used = true;
    for (int i = 0; i < lock.lock_candidate_count; ++i) {
        all_used = all_used && lock.lock_candidate_used[lock.lock_candidates[i]];
    }
    if (all_used) {
        for (int i = 0; i < lock.lock_candidate_count; ++i) {
            lock.lock_candidate_used[lock.lock_candidates[i]] = false;
        }
    }
    // one pass that moves a used candidate behind an unused neighbour
    for (int i = 0; i + 1 < lock.lock_candidate_count; ++i) {
        int* c = lock.lock_candidates;
        if (lock.lock_candidate_used[c[i]] && !lock.lock_candidate_used[c[i + 1]]) {
            std::swap(c[i], c[i + 1]);
        }
    }
}

// decomp: hud_lock_try_start 0x5281D
bool lock_try_start(Fighter& f, int jp, int candidate) {
    LockHud& lock = f.combat.hud;
    if (lock.mount_lock_max[jp] <= lock.mount_locks[jp]) {
        return false;
    }
    const Weapon& w = f.combat.weapons[jp + 2];
    if (w.resource <= Fx{} || fx(lock.mount_locks[jp] + 1) > w.resource || w.volley_active) {
        return false;
    }
    for (int i = 0; i < lock_mark_count; ++i) {
        if (lock.locks[i].target == -1) {
            lock.locks[i] = {lock.lock_candidates[candidate], fx(f.combat.lock_frames), jp};
            lock.mount_lock_target[jp] = i;
            break;
        }
    }
    lock.mount_locks[jp] += 1;
    return true;
}

bool lost(const World& world, const HudSense& s, int target, Fx range) {
    return !alive(world, target) || camo_hides(world, target) || s.occluded ||
           s.distance > range || fx_abs(s.yaw) > quarter_turn_radians;
}

// even steps
// decomp: hud_locks_update 0x50FC5
void locks_update(World& world, Fighter& f) {
    LockHud& lock = f.combat.hud;
    const FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
    for (bool& on : lock.locked_on) {
        on = false;
    }
    if (lock.mount_lock_target[0] == -1 || lock.mount_lock_target[1] == -1) {
        lock_candidates_build(world, hud, lock);
    } else {
        lock.lock_candidate_count = 0;
    }
    int next = 0;
    for (int jp = 0; jp < 2; ++jp) {
        if (lock.mount_lock_target[jp] == -1 && next < lock.lock_candidate_count &&
            lock_try_start(f, jp, next)) {
            lock.lock_candidate_used[lock.lock_candidates[next]] = true;
            ++next;
        }
    }
    bool armed[2];
    for (int m = 0; m < 2; ++m) {
        const Weapon& w = f.combat.weapons[m + 2];
        armed[m] = w.present && w.fitted && w.resource > Fx{};
    }
    for (int i = 0; i < lock_mark_count; ++i) {
        LockMark& mark = lock.locks[i];
        if (mark.target == -1) {
            continue;
        }
        const HudSense& s = hud.sense[static_cast<std::size_t>(mark.target)];
        if (lost(world, s, mark.target, lock_range) || !armed[mark.mount]) {
            lock_release(lock, i);
            continue;
        }
        if (mark.timer > Fx{}) {
            mark.timer -= lock_tick;
            if (mark.timer <= Fx{}) {
                // finished: the mount may start its next lock (the HUD sound, the target.s bark)
                lock.mount_lock_target[mark.mount] = -1;
                mark.timer = Fx{};
                combat_event(world, event_lock_done, f.slot, mark.target, f.position, mark.mount);
                locked_on_bark(world, mark.target);
            }
        } else {
            lock.locked_on[mark.target] = true;
        }
    }
}

// even steps
// decomp: hud_melee_lock_update 0x51DB7
void melee_lock_update(World& world, Fighter& f) {
    FighterCombat& c = f.combat;
    LockHud& lock = c.hud;
    const FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
    bool melee = false;
    for (int m = 0; m < 2; ++m) {
        const Weapon& w = c.weapons[m];
        melee = melee || (w.present && w.record->behavior == 6 && w.fitted);
    }
    if (!melee) {
        if (lock.warning.target != -1) {
            lock.warning = LockMark{};
            lock.melee_locked = false;
        }
        return;
    }
    const Fx range = f.controller != -1 ? melee_range_human : melee_range_ai;
    LockMark& mark = lock.warning;
    if (mark.target == -1) {
        for (int i = 0; i < hud.visible_count; ++i) {
            const int slot = hud.visible[static_cast<std::size_t>(i)];
            const HudSense& s = hud.sense[static_cast<std::size_t>(slot)];
            if (alive(world, slot) && !camo_hides(world, slot) && !s.occluded && s.on_screen &&
                s.distance < range && in_box(hud, s, lock.box_b)) {
                mark = {slot, fx(c.lock_frames), -1};
                break;
            }
        }
        return;
    }
    if (lost(world, hud.sense[static_cast<std::size_t>(mark.target)], mark.target, range)) {
        lock.warning = LockMark{};
        lock.melee_locked = false;
        return;
    }
    if (mark.timer > Fx{}) {
        mark.timer -= lock_tick;
        if (mark.timer <= Fx{}) {
            mark.timer = Fx{};
            lock.melee_locked = true;
            c.melee_target = mark.target;
            combat_event(world, event_lock_done, f.slot, mark.target, f.position, -1);
            locked_on_bark(world, mark.target);
        }
    } else {
        lock.locked_on[mark.target] = true;
    }
}

// what the AI reads of the lock HUD: locked_on, melee_locked, and locking_me (that slot holds a
// lock mark on this one: lower slots as of this step, higher as of the last, as retail)
void publish(World& world, const Fighter& f) {
    const LockHud& lock = f.combat.hud;
    FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
    for (int i = 0; i < combat_slots; ++i) {
        hud.locked_on[static_cast<std::size_t>(i)] = lock.locked_on[i];
        bool locking = false;
        const Fighter& other = world.fighters[static_cast<std::size_t>(i)];
        if (other.present && other.combat.alive && i != f.slot) {
            for (const LockMark& mark : other.combat.hud.locks) {
                locking = locking || mark.target == f.slot;
            }
        }
        hud.locking_me[static_cast<std::size_t>(i)] = locking;
    }
    hud.melee_locked = lock.melee_locked;
}

} // namespace

// decomp: hud_update 0x4E62E
void hud_update(World& world, Fighter& f) {
    reticle_track_update(world, f);
    if (world.step % 2 == 0) {
        locks_update(world, f);
        melee_lock_update(world, f);
    }
    publish(world, f);
}

// decomp: weapon_lock_take 0x4D19D
int weapon_lock_take(Fighter& f, int mount) {
    if (mount != 2 && mount != 3) {
        return -1;
    }
    LockHud& lock = f.combat.hud;
    for (int i = 0; i < lock_mark_count; ++i) {
        const LockMark& mark = lock.locks[i];
        if (mark.target != -1 && mark.mount == mount - 2 && mark.timer <= Fx{}) {
            const int target = mark.target;
            lock_release(lock, i);
            return target;
        }
    }
    return -1;
}
