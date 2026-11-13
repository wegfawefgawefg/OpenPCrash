// The stage's effects (decomp src/hand/falling_debris.c, src/od/u_072650.c, u_06eda0.c): the sets
// props and crates play, the supply craft's jets, fires and crash smoke following its six points
// (supply_craft_effects_update), the ending's paper storm, the burning props' lights, the
// hit static's scratches.
#include "battle/battle_ending.hpp"
#include "effects/effects.hpp"
#include "stage/stage_view.hpp"
#include "world/app_states.hpp"
#include "world/world.hpp"

namespace {

// craft_fx_points and craft_fx_dirs (func_0011D7E0, func_0011D890): two jets,
// three fires, the crash smoke, the paper storm's point
constexpr float craft_points[7][3] = {{-1.65f, 0.14f, -6.12f},  {1.65f, 0.14f, -6.12f},
                                      {-2.39f, -0.84f, -4.43f}, {2.39f, -0.84f, -4.43f},
                                      {0.0f, -0.28f, -1.29f},   {0.0f, -0.5f, -1.7f},
                                      {0.0f, -0.5f, -1.7f}};
constexpr float craft_dirs[7][3] = {{0, 0, -3},         {0, 0, -3},     {-0.36f, -0.41f, 0},
                                    {0.36f, -0.41f, 0}, {0, -0.64f, 0}, {0, -0.62f, -1},
                                    {0, -0.62f, -1}};
// the sets each group plays: jets 0x2E, fires 0x2F, the crash 0x30
constexpr int craft_first[3] = {0, 2, 5};
constexpr int craft_count[3] = {2, 3, 1};
constexpr int craft_sets[3] = {0x2E, 0x2F, 0x30};
// prop burn timers: frame_cadence x 6 updates, 360 world steps (stage_props.cpp)
constexpr float burn_steps = 360;

Mat4 craft_matrix(const World& world) {
    return port_matrix(mat_of(world.stage.craft.matrix));
}

// one craft point: where it is now, where it was, and its direction
void craft_point(const Effects& fx, int i, Vec3f& at, Vec3f& before, Vec3f& dir) {
    const Vec3f p{craft_points[i][0], craft_points[i][1], craft_points[i][2]};
    at = transform_coord(p, fx.craft_matrix);
    before = transform_coord(p, fx.craft_prev_matrix);
    Mat4 turn = fx.craft_matrix;
    turn.m[3][0] = turn.m[3][1] = turn.m[3][2] = 0;
    dir = vec_normalize(
        transform_coord({craft_dirs[i][0], craft_dirs[i][1], craft_dirs[i][2]}, turn));
}

// supply_craft_jets_start and its kin (effect_flying_machine_*): a group's emitters, each started
// only when its handle is free
void craft_start(Effects& fx, int group) {
    for (int k = 0; k < craft_count[group]; ++k) {
        const int i = craft_first[group] + k;
        if (fx.craft[i].slot != -1) {
            continue;
        }
        Vec3f at, before, dir;
        craft_point(fx, i, at, before, dir);
        fx.craft[i] = effect_set_play(fx.pool, craft_sets[group], at, before);
        effect_set_matrix(fx.pool, fx.craft[i], from_direction(dir, 0));
    }
}

void craft_finish(Effects& fx, int group) {
    for (int k = 0; k < craft_count[group]; ++k) {
        effect_finish(fx.pool, fx.craft[craft_first[group] + k]);
    }
}

// the paper storm (supply_craft_paper_storm, effect_paper_storm): four groups of
// craft_effects 6..22, the first at the craft's paper point, the others at the ending's points A,
// B and C (D_0098CA14 and the two after it, func_0011E500), each where the one before left it
// when its point is unset
constexpr int paper_first[4] = {0, 5, 10, 15};
constexpr int paper_count[4] = {5, 5, 5, 2};
constexpr float paper_points[3][3] = {{98.5118f, 7.9257f, 18.0496f},
                                      {106.7f, 28.0f, 28.6f},
                                      {117.5908f, 15.4518f, 18.9555f}};

// effect_paper_storm's set: by the emitter's node (its place in the group) and the group
int paper_set(int group, int node) {
    // the craft's 0x41.., point A's 0x49.., B's 0x45.., C's 0x4D..; C has no node 2 or 3 sets
    // of its own (0x4B, 0x4C, as A's), never asked for: C has two emitters
    static const int sets[4][4] = {{0x41, 0x42, 0x43, 0x44},
                                   {0x49, 0x4A, 0x4B, 0x4C},
                                   {0x45, 0x46, 0x47, 0x48},
                                   {0x4D, 0x4E, 0x4B, 0x4C}};
    return sets[group][node];
}

int paper_node(int group, int k) {
    if (group == 3) {
        return k < 1 ? 0 : 1;
    }
    return k < 2 ? 0 : k < 3 ? 1 : k < 4 ? 2 : 3;
}

// where each group plays: the craft's point, then each set point in turn (the buffer carries on)
void paper_places(const Effects& fx, Vec3f at[4], Vec3f before[4], Vec3f& dir) {
    craft_point(fx, 6, at[0], before[0], dir);
    for (int g = 1; g < 4; ++g) {
        at[g] = at[g - 1];
        before[g] = before[g - 1];
        const int row = fx.paper_anchors.point[g - 1];
        if (row >= 0 && row < 3) {
            at[g] = before[g] = {paper_points[row][0], paper_points[row][1], paper_points[row][2]};
        }
    }
}

// the craft installed in the ending (craft state 8): every free handle plays its set
void paper_storm_start(Effects& fx) {
    Vec3f at[4], before[4], dir;
    paper_places(fx, at, before, dir);
    const Mat4 turn = from_direction(dir, 0);
    for (int g = 0; g < 4; ++g) {
        for (int k = 0; k < paper_count[g]; ++k) {
            EffectHandle& h = fx.paper[paper_first[g] + k];
            if (h.slot != -1) {
                continue;
            }
            h = effect_set_play(fx.pool, paper_set(g, paper_node(g, k)), at[g], before[g]);
            effect_set_position(fx.pool, h, at[g]);
            effect_set_prev_position(fx.pool, h, before[g]);
            effect_set_matrix(fx.pool, h, turn);
        }
    }
}

// supply_craft_effects_update's state 0xA block: the craft's group follows its point, each point
// group follows its point while it is set, a dead emitter frees its handle; each group shown by
// its flag (the craft's by the last byte, f_10F0D)
void paper_storm_update(Effects& fx, const World& world) {
    if (const EndingPaper* anchors = ending_paper(world)) {
        fx.paper_anchors = *anchors;
    }
    const EndingPaper& anchors = fx.paper_anchors;
    // the paper particles' flutter height and size read GameState +0x10EFC and +0x10F04
    fx.pool.paper_near = anchors.flag[0] != 0;
    fx.pool.paper_high = anchors.flag[1] != 0;
    Vec3f at[4], before[4], dir;
    paper_places(fx, at, before, dir);
    const Mat4 turn = from_direction(dir, 0);
    const bool shown[4] = {anchors.last != 0, anchors.flag[0] != 0, anchors.flag[1] != 0,
                           anchors.flag[2] != 0};
    for (int g = 0; g < 4; ++g) {
        const bool follows = g == 0 || anchors.point[g - 1] != -1;
        for (int k = 0; k < paper_count[g]; ++k) {
            EffectHandle& h = fx.paper[paper_first[g] + k];
            if (h.slot != -1 && follows) {
                if (!effect_active(fx.pool, h)) {
                    h = {};
                } else {
                    effect_set_position(fx.pool, h, at[g]);
                    effect_set_matrix(fx.pool, h, turn);
                }
            }
            effect_show(fx.pool, h.slot, shown[g]);
        }
    }
}

bool in_ending(const World& world) {
    return world.battle_state.menu_state == app_ending_data;
}

} // namespace

void stage_effects_event(Effects& fx, const World& world, const StageEvent& event) {
    const Vec3f at = original_of(event.at);
    switch (event.kind) {
    case StageEventKind::Effect:
        // the craft's explosions (shot down, landed) finish what it was running
        if (event.a == 0x3F) {
            craft_finish(fx, 2);
            craft_finish(fx, 1);
            craft_finish(fx, 0);
        }
        effects_play(fx, event.a, at, at);
        break;
    case StageEventKind::CraftJets:
        fx.craft_matrix = craft_matrix(world);
        craft_finish(fx, 1);
        craft_start(fx, 0);
        // the ending's craft brings the paper storm as it is installed (supply_craft_update's
        // craft state 8 in state 0xA; a route's release ends past segment 0)
        if (in_ending(world) && world.stage.craft.segment == 0) {
            if (const EndingPaper* anchors = ending_paper(world)) {
                fx.paper_anchors = *anchors;
            }
            paper_storm_start(fx);
        }
        break;
    case StageEventKind::CraftFire:
        fx.craft_matrix = craft_matrix(world);
        craft_finish(fx, 0);
        craft_start(fx, 1);
        break;
    case StageEventKind::CraftCrash:
        fx.craft_matrix = craft_matrix(world);
        craft_start(fx, 2);
        break;
    case StageEventKind::Scratch:
        screen_scratch_add(fx, fx_float(event.value), event.a, fx_float(event.value2), event.b);
        break;
    default:
        break;
    }
}

void stage_effects_update(Effects& fx, const World& world) {
    const StageWorld& stage = world.stage;
    // supply_craft_pause stops all three groups (with -1 guarded: docs/bugs.md)
    if (stage.craft.state == craft_paused && fx.craft_state != craft_paused) {
        for (EffectHandle& h : fx.craft) {
            effect_stop(fx.pool, h, true);
            h = {};
        }
    }
    fx.craft_state = stage.craft.state;
    // the craft's emitters follow its points (effect_arm_break_follow): a dead one frees its handle
    fx.craft_matrix = craft_matrix(world);
    for (int i = 0; i < 6; ++i) {
        EffectHandle& h = fx.craft[i];
        if (h.slot == -1) {
            continue;
        }
        if (!effect_active(fx.pool, h)) {
            h = {};
            continue;
        }
        Vec3f at, before, dir;
        craft_point(fx, i, at, before, dir);
        effect_set_position(fx.pool, h, at);
        effect_set_matrix(fx.pool, h, from_direction(dir, 0));
    }
    if (in_ending(world)) {
        paper_storm_update(fx, world);
    }
    fx.craft_prev_matrix = fx.craft_matrix;

    // the burning props' lights (stage_prop_timed_particles, prop_burn_update):
    // orange, 6 + 6t (or 4t while burnt) + rand(2), t the burn's time left
    for (const Prop& prop : stage_props(stage)) {
        const bool timed = prop.step == PropStep::TimedParticles;
        const bool burning = prop.step == PropStep::BurnUpdate && (prop.flags & prop_burnt);
        if (!timed && !burning) {
            continue;
        }
        const float t = static_cast<float>(prop.timer) / burn_steps;
        effects_light(fx, original_of(prop.transform.w), 0xFFFF8000,
                      t * (timed ? 6.0f : 4.0f) + 6.0f + rand_float_crt(fx.seed, 2.0f));
    }
}
