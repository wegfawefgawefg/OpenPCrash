// Spent casings, the only debris (decomp src/od/u_099770.c weapon_eject_casing and
// debris_update, src/hand/battle_frame.c debris_alloc; docs/port/effects.md
// "Shots, muzzle flashes, casings, impacts and explosions"). One a trigger pull that let a shot
// out, from the weapon block's casing streams (2 the origin, 3 the direction) under the barrel
// that fired; they fall for 40 ticks without colliding or spinning. Presentation: retail moves
// them in battle_update, once an update, and so do these; the draws fall between updates.
#include "effects/effects.hpp"
#include "mech/rig_data.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

constexpr int casing_slot = 0x39B;
constexpr float casing_scale = 2.0f; // weapon_fire_controller passes 2.0 (0x40000000)
constexpr float casing_life = 40.0f; // ticks, less 2 an update
constexpr float casing_push = 0.05f;
constexpr float pi = 3.1415927f;

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

// the first free slot from the cursor on, wrapping; none free, none
// decomp: debris_alloc 0x46951
Debris* debris_alloc(Effects& fx) {
    const int count = static_cast<int>(std::size(fx.debris));
    for (int k = 0; k < count; ++k) {
        const int i = (fx.debris_cursor + k) % count;
        if (fx.debris[i].life == 0.0f) {
            fx.debris_cursor = i + 1;
            return &fx.debris[i];
        }
    }
    return nullptr;
}

} // namespace

void casing_eject(Effects& fx, const World& world, int slot, int mount) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const Weapon& w = f.combat.weapons[mount];
    const FighterRig* rig = world.battle ? rig_of_build(*world.battle, f.build) : nullptr;
    if (!rig || !w.record) {
        return;
    }
    // weapon_attach_matrix with stream 2 at the current barrel, and weapon_attach_point stream 3
    const std::vector<RigMuzzle>& origins = rig->blocks[mount].streams[2];
    const std::vector<RigMuzzle>& directions = rig->blocks[mount].streams[3];
    if (w.muzzle < 0 || w.muzzle >= static_cast<int>(origins.size()) ||
        w.muzzle >= static_cast<int>(directions.size())) {
        return;
    }
    const RigMuzzle& origin = origins[static_cast<std::size_t>(w.muzzle)];
    if (origin.node < 0 || origin.node >= f.pose.weapon_node_count[mount]) {
        return;
    }
    Debris* d = debris_alloc(fx);
    if (!d) {
        return;
    }
    // the point under its node and the mount, in the original's frame (the pose's matrices are
    // the original's mirrored), then scaled 2
    const Mat4 node = port_matrix(floats_of(f.pose.weapon_nodes[mount][origin.node]));
    const Vec3f point = original_of(origin.point);
    Mat4 out = mat_multiply(mat_translation(point.x, point.y, point.z), node);
    d->life = casing_life;
    d->matrix = mat_multiply(mat_scaling(casing_scale, casing_scale, casing_scale), out);
    d->before = mat_position(d->matrix);
    // pushed along the direction point through the frame's turn, tilted about z by up to
    // 0.05 pi either way (rand_crt), 0.05 a tick; not the owner's velocity
    out.m[3][0] = out.m[3][1] = out.m[3][2] = 0;
    const float angle =
        (static_cast<float>(crt_rand(fx.seed)) / 32767.0f - 0.5f) * pi * 0.1f;
    out = mat_multiply(mat_rotation_z(angle), out);
    const Vec3f dir = original_of(directions[static_cast<std::size_t>(w.muzzle)].point);
    d->velocity = vec_scale(transform_normal(dir, out), casing_push);
}

void casings_update(Effects& fx) {
    // debris_update: two ticks of the velocity, gravity 9.8 / 900 off y, 2 ticks of life
    for (Debris& d : fx.debris) {
        if (d.life == 0.0f) {
            continue;
        }
        d.before = mat_position(d.matrix);
        d.matrix.m[3][0] += 2.0f * d.velocity.x;
        d.matrix.m[3][1] += 2.0f * d.velocity.y;
        d.matrix.m[3][2] += 2.0f * d.velocity.z;
        d.velocity.y -= 9.8f / 900.0f;
        d.life -= 2.0f;
        if (d.life < 0.0f) {
            d.life = 0.0f;
        }
    }
}

void casings_queue(const Effects& fx, float t, const Mat4& view, std::vector<PassItem>& items) {
    for (const Debris& d : fx.debris) {
        if (d.life == 0.0f) {
            continue;
        }
        Mat4 m = d.matrix;
        const Vec3f at = mat_position(d.matrix);
        m.m[3][0] = d.before.x + (at.x - d.before.x) * t;
        m.m[3][1] = d.before.y + (at.y - d.before.y) * t;
        m.m[3][2] = d.before.z + (at.z - d.before.z) * t;
        effects_queue_slot(fx, casing_slot, m, 2, 0, 0xFFFFFFFF, view, items);
    }
}
