// The contact decals, the "shockwave" (decomp src/hand/u_06eda0.c contact_decal_spawn,
// contact_decal_draw and effopt_glow_time; docs/port/effects.md "Contact
// decals"): a 30-model sequence from 0x3FD drawn through effopt over the backdrop in pass 6,
// where a shot hits or a fighter stands on a surface with bit 0x2000 (stage surface type 8).
// Each decal lives 30 updates. Retail gives every decal the reveal sphere of texture slot 2,
// which holds the last decal's centre when the frame is drawn, and indexes the glow by draw
// count: here each decal has its own centre and its own life (docs/bugs.md).
#include "battle/stage_collision.hpp"
#include "effects/effects.hpp"
#include "render/d3dx_math.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int decal_first_slot = 0x3FD;
constexpr int decal_life = 30;

// sv_contact_route's decal frame: collision_face_test's matrix of the type 8 face the
// lower sphere met (rows v2 - v0, the normal, v1 - v0, at the sphere's centre) through
// matrix_axes_normalize (the third row cross(v2 - v0, normal)), dropped 2 along the normal, so
// the ring lies on the face. The world keeps no contact frame: the face is found again from the
// sphere where the step left it (position + radius up, within radius + 0.1 + a little of the face,
// as the push leaves a wall's; in the air the upper sphere's, whose face test writes the same
// matrix after the lower's); none found, the world's up at the centre
// decomp: sv_contact_route 0x7C740
Mat4 contact_frame(const World& world, const Fighter& f) {
    FxVec3 centre = f.position + FxVec3{Fx{}, f.radius, Fx{}};
    Vec3f x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    const StageTriangle* t = nullptr;
    if (world.battle) {
        const Fx reach = f.radius + fx_f(0.2f);
        if (!f.grounded) {
            const FxVec3 upper = centre + FxVec3{Fx{}, f.head_height - f.radius * 2, Fx{}};
            t = collision_surface_face(world.battle->stage, upper, reach, surface_edge_wall);
            centre = t ? upper : centre;
        }
        if (!t) {
            t = collision_surface_face(world.battle->stage, centre, reach, surface_edge_wall);
        }
    }
    const Vec3f c = original_of(centre);
    if (t) {
        const Vec3f v0 = original_of(t->tri.v0), v2 = original_of(t->tri.v2);
        const Vec3f n = mirror(Vec3f{fx_float(t->tri.normal.x), fx_float(t->tri.normal.y),
                                     fx_float(t->tri.normal.z)});
        const Vec3f a = vec_sub(v2, v0);
        x = vec_normalize(a);
        y = vec_normalize(n);
        z = vec_normalize(vec_cross(a, n));
    }
    Mat4 m = mat_identity();
    const Vec3f rows[3] = {x, y, z};
    for (int r = 0; r < 3; ++r) {
        m.m[r][0] = rows[r].x;
        m.m[r][1] = rows[r].y;
        m.m[r][2] = rows[r].z;
    }
    m.m[3][0] = c.x - y.x * 2.0f;
    m.m[3][1] = c.y - y.y * 2.0f;
    m.m[3][2] = c.z - y.z * 2.0f;
    return m;
}

} // namespace

// decomp: contact_decal_spawn 0x701FB
void contact_decal_spawn(Effects& fx, const Mat4& matrix) {
    for (ContactDecal& d : fx.decals) {
        if (!d.life) {
            d.matrix = matrix;
            d.life = decal_life;
            return;
        }
    }
}

void contact_decals_update(Effects& fx, const World& world) {
    // sv_contact_route: a fighter whose sphere touched a 0x2000 surface this update, the decal
    // on the face it met (contact_frame)
    if (world.battle) {
        // PHANTOMCRASH_DEV_DECAL=period[,distance] (captures): a decal ahead of fighter 0 every
        // period updates
        if (const char* test = std::getenv("PHANTOMCRASH_DEV_DECAL")) {
            int period = 30;
            float distance = 8;
            std::sscanf(test, "%d,%f", &period, &distance);
            const Fighter& f = world.fighters[0];
            if (f.present && period > 0 && fx.updates % period == 0) {
                const float yaw = fx_float(angle_radians(f.heading));
                Mat4 m = mat_identity();
                const Vec3f at = original_of(f.position);
                m.m[3][0] = at.x + std::sin(yaw) * distance;
                m.m[3][1] = at.y - 0.5f;
                m.m[3][2] = at.z - std::cos(yaw) * distance;
                contact_decal_spawn(fx, m);
            }
        }
        for (const Fighter& f : world.fighters) {
            if (f.present && f.contact_flags & surface_edge_wall) {
                contact_decal_spawn(fx, contact_frame(world, f));
            }
        }
    }
    // contact_decal_draw: the model by age, then the life down a step
    for (ContactDecal& d : fx.decals) {
        d.shown_age = -1;
        if (d.life > 0) {
            d.shown_age = decal_life - d.life;
            d.life -= 1;
            d.glow = static_cast<float>(d.life) / 30.0f; // effopt_glow_time, the life as drawn
        }
    }
}

void contact_decals_queue(const Effects& fx, const Mat4& view, std::vector<PassItem>& items) {
    for (const ContactDecal& d : fx.decals) {
        if (d.shown_age < 0) {
            continue;
        }
        const float scale = static_cast<float>(d.shown_age) / 30.0f * 0.05f + 0.1f;
        const Mat4 m = mat_multiply(mat_scaling(scale, 0.2f, scale), d.matrix);
        const std::size_t first = items.size();
        effects_queue_slot(fx, decal_first_slot + d.shown_age, m, 6, 0, 0xFFFFFFFF, view, items);
        // the reveal sphere at the decal's own centre (port frame, as the record's world), radius 0
        const Vec3f centre = mirror(mat_position(d.matrix));
        for (std::size_t i = first; i < items.size(); ++i) {
            items[i].item.effect[0] = centre.x;
            items[i].item.effect[1] = centre.y;
            items[i].item.effect[2] = centre.z;
            items[i].item.effect[3] = 0;
            items[i].item.effect[4] = d.glow;
        }
    }
}
