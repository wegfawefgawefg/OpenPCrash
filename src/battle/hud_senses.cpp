// The HUD's gameplay half (decomp src/hand/u_04a840.c): the reticle's point (hud_update),
// the senses of every other fighter (hud_senses_update) and the reticle's target
// (hud_lock_arrows_update 0x4F620). Retail runs them in each slot's HUD task after the fighters
// and the shots; the AI reads them the next update. Drawing stays in the view.
#include "ai/ai_links.hpp"
#include "battle/fighter_hud.hpp"
#include "fx/mat.hpp"
#include "world/world.hpp"

namespace {

// pass_projections for view 0 in battle: D3DXMatrixPerspectiveFovLH(52 degrees, 576 / 448, 0.1,
// 10000) (viewports_fov_set, the one-view safe rectangle; docs/port/render.md). An AI
// tests against viewport 0's projection whatever its own view (controller -1 taken as 0)
constexpr Fx projection_y = fx_f(2.0503038415792987);             // cot(26 degrees)
constexpr Fx projection_x = fx_f(2.0503038415792987 * 448 / 576); // over the aspect
constexpr Fx near_z = fx_f(0.1);
// the reticle's point: 64 ahead of the fighter's centre along its aim (hud_update)
constexpr Fx reticle_reach = fx(64);
// the senses past which hud_lock_arrows_update ignores a fighter
constexpr Fx reticle_target_range = fx(75);

struct Projected {
    Fx x, y, z;
    bool in_front = false;
};

// a world point through a view matrix and the projection, divided by w (d3dx_vec3_transform then
// x / w, y / w, z / |w|). In the port's mirrored frame x comes out mirrored; every test reads |x|
// or a difference of two such x
Projected project(const FxMat4& view, FxVec3 point) {
    const FxVec3 v = transform_coord(point, view);
    Projected p;
    if (v.z < near_z) {
        // behind the near plane: z / |w| is negative, off screen
        p.z = fx(-1);
        return p;
    }
    p.x = fx_mul_div(v.x, projection_x, v.z);
    p.y = fx_mul_div(v.y, projection_y, v.z);
    // (z q - q zn) / |w| with q = 10000 / 9999.9: 0 at the near plane, toward 1 far off
    p.z = fx_div_near(v.z - near_z, v.z);
    p.in_front = true;
    return p;
}

FxMat4 view_matrix(const Fighter& f) {
    return mat4_look_at_lh(f.camera.eye, f.camera.target, f.camera.up);
}

void hud_reticle(World& world, int slot, const FxMat4& view) {
    Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    FighterHud& hud = world.huds[static_cast<std::size_t>(slot)];
    // (0, 0, -64) turned by the camera's pitch and heading + aim_yaw, from position + radius up;
    // reticle_center_world is that point back through the inverse of the same matrices
    const FxMat3 turn =
        mat3_mul(mat3_rotation_x(f.camera.pitch), mat3_rotation_y(f.heading + f.aim_yaw));
    f.reticle_center_world =
        f.position + FxVec3{Fx{}, f.radius, Fx{}} + transform({Fx{}, Fx{}, reticle_reach}, turn);
    const Projected screen = project(view, f.reticle_center_world);
    hud.reticle_center_x = screen.in_front ? screen.x : Fx{};
    hud.reticle_center_y = screen.in_front ? screen.y : Fx{};
}

// decomp: hud_senses_update 0x4F0D2
void hud_senses_update(World& world, int slot, const FxMat4& view) {
    const Fighter& me = world.fighters[static_cast<std::size_t>(slot)];
    FighterHud& hud = world.huds[static_cast<std::size_t>(slot)];
    const int team = battle_roster_entry(world, slot).pilot.team;
    const FxVec3 head = me.position + me.camera.head_offset;
    const Angle aim = me.heading + me.aim_yaw;
    hud.visible_count = 0;
    for (int i = 0; i < fighter_slots; ++i) {
        const Fighter& other = world.fighters[static_cast<std::size_t>(i)];
        if (i == slot || !other.present || !fighter_alive(world, i)) {
            continue;
        }
        HudSense& sense = hud.sense[static_cast<std::size_t>(i)];
        const FxVec3 aim_point = fighter_aim_point(other);
        const FxVec3 v = aim_point - head;
        // same team (pilot team, not -1): not in the list; nor, in VS, a player already out of
        // play (retail still lets them be targeted and locked; docs/bugs.md)
        const int other_team = battle_roster_entry(world, i).pilot.team;
        const bool out = world.vs.mode && !world.vs.in_play[static_cast<std::size_t>(i)];
        if ((team == -1 || team != other_team) && !out) {
            hud.visible[static_cast<std::size_t>(hud.visible_count++)] = i;
        }
        sense.distance = length(v);
        // retail turns the direction by -(heading + aim_yaw) and takes atan2(x, -z): with the
        // port's angles negated that is the direction's own port heading less the port aim
        sense.yaw = angle_radians(angle_atan2(v.x, v.z) - aim);
        sense.pitch = angle_radians(angle_atan2(v.y, length(FxVec2{v.x, v.z})));
        const Projected screen = project(view, aim_point);
        sense.screen_x = screen.x;
        sense.screen_y = screen.y;
        sense.screen_z = screen.z;
        sense.on_screen = screen.in_front && fx_abs(screen.x) <= fx(1) &&
                          fx_abs(screen.y) <= fx(1) && screen.z >= Fx{};
        // one ray a pair, from the lower slot's aim point; the higher copies it (props, which
        // retail also ray-tests here, aren't in the world yet)
        if (slot < i) {
            FxVec3 crossing;
            sense.occluded = collision_segment_hit(world.battle->stage, fighter_aim_point(me),
                                                   aim_point, &crossing, true);
        } else {
            sense.occluded = world.huds[static_cast<std::size_t>(i)]
                                 .sense[static_cast<std::size_t>(slot)]
                                 .occluded;
        }
    }
    // retail's bubble sort with an unused inner index: one pass, i against i + 1
    for (int i = 0; i < hud.visible_count - 1; ++i) {
        const int a = hud.visible[static_cast<std::size_t>(i)];
        const int b = hud.visible[static_cast<std::size_t>(i + 1)];
        if (hud.sense[static_cast<std::size_t>(a)].distance >
            hud.sense[static_cast<std::size_t>(b)].distance) {
            hud.visible[static_cast<std::size_t>(i)] = b;
            hud.visible[static_cast<std::size_t>(i + 1)] = a;
        }
    }
}

// hud_lock_arrows_update's target: the first in list order alive, uncamouflaged, unoccluded, on
// screen, under 75 away and inside the reticle's box (sense_in_reticle)
void hud_lock_arrows_update(World& world, int slot) {
    FighterHud& hud = world.huds[static_cast<std::size_t>(slot)];
    Fx box_x, box_y;
    reticle_box(world, slot, &box_x, &box_y);
    hud.reticle_target = -1;
    for (int k = 0; k < hud.visible_count; ++k) {
        const int i = hud.visible[static_cast<std::size_t>(k)];
        const HudSense& sense = hud.sense[static_cast<std::size_t>(i)];
        if (fighter_alive(world, i) && !camo_hides(world, i) && !sense.occluded &&
            sense.on_screen && sense.distance < reticle_target_range &&
            fx_abs(box_x) > fx_abs(sense.screen_x - hud.reticle_center_x) &&
            fx_abs(box_y) > fx_abs(sense.screen_y - hud.reticle_center_y)) {
            hud.reticle_target = i;
            break;
        }
    }
    for (int j = 0; j < fighter_slots; ++j) {
        hud.reticle_on_me[static_cast<std::size_t>(j)] =
            world.fighters[static_cast<std::size_t>(j)].present &&
            world.huds[static_cast<std::size_t>(j)].reticle_target == slot;
    }
}

} // namespace

void huds_update(World& world) {
    for (int slot = 0; slot < fighter_slots; ++slot) {
        if (!world.fighters[static_cast<std::size_t>(slot)].present) {
            continue;
        }
        const FxMat4 view = view_matrix(world.fighters[static_cast<std::size_t>(slot)]);
        hud_reticle(world, slot, view);
        hud_senses_update(world, slot, view);
        hud_lock_arrows_update(world, slot);
    }
}
