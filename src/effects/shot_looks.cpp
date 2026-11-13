// What a shot looks like (decomp src/od/u_097660.c shot_model_draw and
// impact_volume_draw, src/hand/weapon_laser.c weapon_laser_controller's beam;
// docs/port/effects.md "Shots, muzzle flashes, casings, impacts and explosions"). Retail submits
// them from the shot's update, once an update; the look depends only on the shot's transform,
// life and radius, so it is taken after every world step and drawn between the last two steps,
// as the fighters are. Melee blades draw nothing of their own: the blade is the weapon model, and
// its hits play set 0xC (impact_fx row 12).
#include "effects/effects.hpp"
#include "render/mesh_flags.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.1415927f;
constexpr int flame_slot = 0x3F3; // the missiles' and rockets' flame
constexpr int shell_slot = 0x3EF; // the blast's two counter-turning shells
constexpr int sphere_slot = 0x396;
constexpr int beam_slot = 0x3A5;


// a shot's transform as the original's: the port keeps the original's rows (z the direction)
// with each one's z negated, the translation too
Mat4 original_transform(const FxMat3& basis, const FxVec3& at) {
    Mat4 m;
    const FxVec3 rows[4] = {basis.x, basis.y, basis.z, at};
    for (int r = 0; r < 4; ++r) {
        const Vec3f v = original_of(rows[r]);
        m.m[r][0] = v.x;
        m.m[r][1] = v.y;
        m.m[r][2] = v.z;
    }
    return m;
}

Vec3f row_of(const Mat4& m, int r) {
    return {m.m[r][0], m.m[r][1], m.m[r][2]};
}

// between two steps' transforms: rows and place lerped
Mat4 lerp_matrix(const Mat4& a, const Mat4& b, float t) {
    Mat4 out = b;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r][c] = a.m[r][c] + (b.m[r][c] - a.m[r][c]) * t;
        }
    }
    return out;
}

// x = up x dir, y = dir x x, z = dir, all normalized
// decomp: matrix_from_dir_up 0xD3D59
Mat4 from_dir_up(const Vec3f& up, const Vec3f& dir) {
    const Vec3f x = vec_cross(up, dir);
    const Vec3f y = vec_cross(dir, x);
    const Vec3f rows[3] = {vec_normalize(x), vec_normalize(y), vec_normalize(dir)};
    Mat4 m;
    for (int r = 0; r < 3; ++r) {
        m.m[r][0] = rows[r].x;
        m.m[r][1] = rows[r].y;
        m.m[r][2] = rows[r].z;
    }
    return m;
}

// shot_model_draw: the row's model (or a quad facing the camera) at the shot, and for the
// missiles and rockets the flame behind it, turned by the life
void shot_look_queue(const Effects& fx, const ShotSnap& s, const Mat4& m, float life,
                     const Mat4& view, const Vec3f& eye, std::vector<PassItem>& items) {
    const ShotLook& look = fx.data.shot_looks[static_cast<std::size_t>(s.look)];
    const Vec3f at = row_of(m, 3);
    const bool late = s.look == 0x10 || s.look == 0x11; // pass 9, not turned about y
    Mat4 top;
    if (!look.billboard) {
        top = late ? m : mat_multiply(mat_rotation_y(pi), m);
    } else {
        top = billboard_matrix(at, eye, 0, 0, 0);
    }
    top = mat_multiply(mat_scaling(look.scale, look.scale, look.scale), top);
    const int pass = late ? 9 : 2;
    if (look.model != -1) {
        effects_queue_slot(fx, look.model, top, pass, 0, 0xFFFFFFFF, view, items);
    } else if (look.quad != -1) {
        effects_queue_quad(fx, look.quad, billboard_matrix(row_of(top, 3), eye, 0, 0, 0),
                           mesh_additive | mesh_factor, 0xFFFFFFFF, pass, view, items);
    }
    if (look.flame) {
        // model_slot_flags_or(0x3F3, 0x8000): additive
        Mat4 flame = mat_multiply(mat_translation(0, 0, look.flame_z), top);
        flame = mat_multiply(
            mat_scaling(look.flame_scale[0], look.flame_scale[1], look.flame_scale[2]), flame);
        flame = mat_multiply(mat_rotation_z(life * pi / 16.0f), flame);
        effects_queue_slot(fx, flame_slot, flame, 9, mesh_additive, 0xFFFFFFFF, view, items);
    }
}

// impact_volume_draw: two shells turning against each other and a sphere, fading out over the
// last 20 ticks of the blast's life
void blast_queue(const Effects& fx, const Mat4& b, float age, float radius, const Mat4& view,
                 std::vector<PassItem>& items) {
    const float angle = age * pi / 32.0f;
    const std::uint32_t fade =
        static_cast<std::uint32_t>(age < 20.0f ? static_cast<int>(age / 20.0f * 128.0f) : 0x80)
            << 24 |
        0xFFFFFF;
    const auto shape = [&](int slot, float turn, float x, float y, float z) {
        const Mat4 m = mat_multiply(mat_multiply(mat_rotation_y(turn), mat_scaling(x, y, z)), b);
        effects_queue_slot(fx, slot, m, 9, mesh_factor, fade, view, items);
    };
    shape(shell_slot, angle, radius * 2.0f, radius * 4.0f, radius * 2.0f);
    shape(shell_slot, -angle, radius * 2.0f - 0.2f, radius * 4.0f - 0.2f, radius * 2.0f - 0.2f);
    const float sphere = radius * 2.0f - 0.3f;
    shape(sphere_slot, -angle, sphere, sphere, sphere);
}

// the laser's beam: model 0x3A5 stretched from the muzzle along the beam, turned about it to
// face the eye (retail's view 0 eye in every view; here each view's own), dimmer while the
// intensity is under 8
void beam_queue(const Effects& fx, const Mat4& m2, float length, int intensity, const Mat4& view,
                const Vec3f& eye, std::vector<PassItem>& items) {
    const Vec3f muzzle = row_of(m2, 3);
    Mat4 frame = from_dir_up(vec_sub(eye, muzzle), row_of(m2, 2));
    frame.m[3][0] = muzzle.x;
    frame.m[3][1] = muzzle.y;
    frame.m[3][2] = muzzle.z;
    Mat4 out = mat_multiply(mat_rotation_y(pi / 2), frame);
    out = mat_multiply(mat_rotation_x(pi / 2), out);
    out = mat_multiply(mat_translation(-0.5f * length, 0, 0.5f), out);
    out = mat_multiply(mat_scaling(length, 0.25f, 1), out);
    const std::uint32_t factor =
        intensity < 8 ? static_cast<std::uint32_t>(intensity) * 0x20202020u : 0xFFFFFFFFu;
    effects_queue_slot(fx, beam_slot, out, 9, mesh_factor | mesh_additive, factor, view, items);
}

} // namespace

void shot_looks_step(Effects& fx, const World& world) {
    fx.shot_before.swap(fx.shot_now);
    const int looks = static_cast<int>(fx.data.shot_looks.size());
    for (int i = 0; i < shot_pool_size; ++i) {
        const Shot& shot = world.combat.shots[static_cast<std::size_t>(i)];
        ShotSnap& s = fx.shot_now[static_cast<std::size_t>(i)];
        s = ShotSnap{};
        if (shot.state == 0 || shot.weapon < 0 || !world.battle) {
            continue;
        }
        const Weapon& w = weapon_at(world, shot.weapon);
        if (!w.record) {
            continue;
        }
        s.weapon = shot.weapon;
        s.m = original_transform(shot.basis, shot.origin);
        s.life = fx_float(shot.life);
        s.radius = fx_float(shot.radius);
        const int behavior = w.record->behavior;
        if (shot.state == 3 && shot.mover == mover_blast) {
            s.kind = ShotSnap::blast;
            s.radius = fx_float(impact_radius_at(shot.life, w)); // the radius it draws with
        } else if (behavior == 5) {
            // weapon_laser_controller draws its beam while the intensity is above 0
            if (w.beam_shot == i && w.reload_left > 0) {
                s.kind = ShotSnap::beam;
                s.intensity = w.reload_left;
            }
        } else if (shot.state == 1 && behavior <= 4 && w.record->shot_model < looks) {
            s.kind = ShotSnap::flying;
            s.look = w.record->shot_model;
        }
    }
}

void shot_looks_queue(Effects& fx, float alpha, const Mat4& view, const Vec3f& eye,
                      std::vector<PassItem>& items) {
    for (int i = 0; i < shot_pool_size; ++i) {
        const ShotSnap& s = fx.shot_now[static_cast<std::size_t>(i)];
        if (s.kind == ShotSnap::none) {
            continue;
        }
        // from the step before when it was the same shot doing the same
        const ShotSnap& b = fx.shot_before[static_cast<std::size_t>(i)];
        const bool same = b.kind == s.kind && b.weapon == s.weapon;
        const Mat4 m = same ? lerp_matrix(b.m, s.m, alpha) : s.m;
        const float life = same ? b.life + (s.life - b.life) * alpha : s.life;
        const float radius = same ? b.radius + (s.radius - b.radius) * alpha : s.radius;
        switch (s.kind) {
        case ShotSnap::flying:
            shot_look_queue(fx, s, m, life, view, eye, items);
            break;
        case ShotSnap::blast:
            blast_queue(fx, m, life, radius, view, items);
            break;
        case ShotSnap::beam:
            beam_queue(fx, m, radius, s.intensity, view, eye, items);
            break;
        default:
            break;
        }
    }
}
