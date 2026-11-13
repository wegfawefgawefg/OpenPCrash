// The supply craft and its three crates (decomp src/hand/falling_debris.c; docs/port/battle.md,
// "The supply craft and the crates"). Retail runs them in the stage task's update; here they are
// world state stepped by step_stage at the same place in the order. Constants converted per
// docs/sixty.md, "Pickups and the supply craft".
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/stage_collision.hpp"
#include "stage/stage_world.hpp"

namespace {

// craft_route_speed 0x3CF5C28F (0.03 of a segment an update; fx 1966 raw) -> half a step (R1)
constexpr Fx route_speed = fx_raw(983);
// the hatch: 0.03 rad an update to pi/4 and back (supply_craft_fall_hover) -> 0.015 a step (R5).
// Opening ends at > 0.80 and closing at < -0.015 instead of > pi/4 and < 0, so each takes 54 steps
// like retail's 27 updates (docs/sixty.md, "hatch")
constexpr Fx hatch_rate = fx_raw(983);
constexpr Fx hatch_open = fx_f(0.7853982f);
constexpr Fx hatch_open_test = fx_f(0.80);
constexpr Fx hatch_shut_test = fx_raw(-983);
// crates: drop gap 50 updates -> 100 steps (R4); fall 0.5 an update -> 0.25 a step (R1); on the
// ground 900, burst below 0 (901 updates) -> 1801 (1802 steps, R12 E)
constexpr int crate_drop_gap = 100;
constexpr Fx crate_fall = fx_f(0.25);
constexpr int crate_life = 1801;
// shot down: 0.6 of the last route step, down 0.3 an update -> 0.15 a step (R1); timer 500,
// burst below 0 (501 updates) -> 1001 (1002 steps, R12 E)
constexpr Fx crash_keep = fx_f(0.6f);
constexpr Fx crash_drop = fx_f(-0.15);
constexpr int crash_steps = 1001;
// cosmetic: the rotor 0.03 rad an update -> 0.015 (R5); the crate glow 0.05 x rate (pickups_draw)
constexpr Fx rotor_rate = fx_raw(983);
constexpr Fx two_pi = fx_f(6.2831855f);
constexpr Fx pi = fx_f(3.1415927f);
// local points in the port frame (the original's z negated): a crate aboard (D_009667B0
// (0, 0.5, -2.7)) and the body's centre (D_0096688C (0, 0, -2))
constexpr FxVec3 crate_seat{Fx{}, fx_f(0.5), fx_f(2.7f)};
constexpr FxVec3 body_point{Fx{}, Fx{}, fx(2)};

FxVec3 route_point(const StageWorld& stage) {
    const std::vector<RouteSegment>& route = stage.data->routes[stage.craft.route];
    const int segment = stage.craft.segment < static_cast<int>(route.size())
                            ? stage.craft.segment
                            : static_cast<int>(route.size()) - 1;
    // stage_route_eval_bezier: the cubic through the segment's four points
    const RouteSegment& s = route[segment];
    const Fx t = stage.craft.t;
    const Fx u = fx(1) - t;
    const Fx a = fx_mul_near(fx_mul_near(u, u), u);
    const Fx b = fx_mul_near(fx_mul_near(fx(3) * t, u), u);
    const Fx c = fx_mul_near(fx_mul_near(fx(3) * t, t), u);
    const Fx d = fx_mul_near(fx_mul_near(t, t), t);
    return s.p[0] * a + s.p[1] * b + s.p[2] * c + s.p[3] * d;
}

// along the route by one step; true when the segment moved on (then `segment` is the new one)
bool route_advance(SupplyCraft& craft) {
    craft.t = craft.t + craft.speed;
    if (craft.t > fx(1)) {
        craft.t = craft.t - fx(1);
        craft.segment += 1;
        return true;
    }
    return false;
}

void route_move(StageWorld& stage) {
    stage.craft.prev_pos = stage.craft.pos;
    stage.craft.pos = route_point(stage);
}

// decomp: stage_route_install 0x67967
void stage_route_install(StageWorld& stage, const RouteChoice& choice, Fx speed) {
    SupplyCraft& craft = stage.craft;
    const int crates = 3;
    craft = SupplyCraft{};
    craft.route = choice.route;
    craft.last_segment = choice.count;
    craft.pos = craft.prev_pos = stage.data->routes[choice.route][0].p[0];
    craft.state = craft_installed;
    craft.crate_count = crates;
    craft.step = CraftStep::Ingress;
    craft.body = craft.body_prev = craft.pos;
    craft.shootable = true;
    craft.speed = speed;
    craft.matrix = mat4_identity();
    craft.ingress = choice.ingress;
    craft.release_start = choice.release_start;
    craft.release_end = choice.release_end;
    craft.height = choice.height;
}

// one rand_float(1) a crate against the tournament's weights. Retail
// resets its running total only on a match, so a roll past the weights' sum carries the sum into
// the next crate's walk; the port starts each crate from 0 (docs/bugs.md). A roll past the sum
// keeps the crate's kind as it was, as retail's does
// decomp: pickup_kinds_draw 0x66B11
void pickup_kinds_draw(StageWorld& stage, WorldRandom& random, int count) {
    if (stage.craft.state == craft_hatch) {
        return;
    }
    stage.craft.crate_count = count;
    const std::vector<Fx>& weights =
        stage.data->pickup_weights[static_cast<std::size_t>(stage.setup.crate_tournament) %
                                   stage.data->pickup_weights.size()];
    for (int i = 0; i < 3; ++i) {
        Fx total;
        const Fx roll = rand_fx(random, fx(1));
        for (int k = 0; k < 10; ++k) {
            total = total + weights[k];
            if (roll <= total) {
                stage.crates[i].kind = k;
                break;
            }
        }
        // VS rolls the score crates again
        if (stage.setup.vs && stage.crates[i].kind <= 3) {
            --i;
        }
    }
}

// decomp: pickups_set_up 0x66C4D
void pickups_set_up(StageWorld& stage, WorldRandom& random) {
    for (Crate& crate : stage.crates) {
        crate = Crate{};
        crate.pos = {Fx{}, fx(1000), Fx{}};
        crate.drop_delay = crate_drop_gap;
        crate.life = crate_life;
    }
    stage.crates[0].drop_delay = 0;
    pickup_kinds_draw(stage, random, 3);
}

// decomp: pickup_clear 0x66D41
void pickup_clear(Crate& crate) {
    crate.pos = {};
    crate.drop_delay = 0;
    crate.life = crate_life;
    crate.state = crate_gone;
    for (bool& claimed : crate.claimed) {
        claimed = false;
    }
}

void craft_step_ingress(StageWorld& stage) {
    SupplyCraft& craft = stage.craft;
    route_move(stage);
    if (route_advance(craft)) {
        if (craft.segment > craft.last_segment) {
            // past the end: parked out of sight at the start, waiting for the crates
            craft.segment = 0;
            craft.state = craft_parked;
            craft.pos = craft.prev_pos = stage.data->routes[craft.route][0].p[0];
            craft.step = CraftStep::CollectedClear;
        } else if (craft.segment == craft.ingress) {
            craft.state = craft_dropping;
            craft.step = CraftStep::Release;
            stage_event(stage, StageEventKind::CraftFire, craft.pos);
        }
    }
    craft.velocity = craft.pos - craft.prev_pos;
    craft.rotor = craft.rotor + rotor_rate;
    if (craft.rotor > two_pi) {
        craft.rotor = craft.rotor - two_pi;
    }
}

void craft_step_release(StageWorld& stage) {
    SupplyCraft& craft = stage.craft;
    route_move(stage);
    if (route_advance(craft)) {
        if (craft.segment > craft.last_segment) {
            craft.segment = 0;
        } else if (craft.segment == craft.release_start) {
            craft.state = craft_hatch;
            craft.hatch_phase = 1;
            craft.step = CraftStep::FallHover;
        } else if (craft.segment >= craft.release_end) {
            craft.state = craft_flying;
            craft.step = CraftStep::Ingress;
            stage_event(stage, StageEventKind::CraftJets, craft.pos);
        }
    }
    craft.velocity = craft.pos - craft.prev_pos;
}

void craft_step_fall_hover(StageWorld& stage) {
    SupplyCraft& craft = stage.craft;
    route_move(stage);
    if (route_advance(craft) && craft.segment > craft.last_segment) {
        craft.segment = 0;
    }
    switch (craft.hatch_phase) {
    case 1:
        craft.hatch = craft.hatch + hatch_rate;
        if (craft.hatch > hatch_open_test) {
            craft.hatch = hatch_open;
            craft.hatch_phase = 2;
        }
        break;
    case 2: {
        // the first crate still aboard counts its delay down, then drops
        for (int i = 0; i < craft.crate_count; ++i) {
            Crate& crate = stage.crates[i];
            if (crate.drop_delay == 0) {
                if (crate.state == crate_aboard) {
                    crate.state = crate_falling;
                }
            } else {
                crate.drop_delay -= 1;
                break;
            }
        }
        bool all_out = true;
        for (int i = 0; i < craft.crate_count; ++i) {
            all_out = all_out && stage.crates[i].state != crate_aboard;
        }
        if (all_out) {
            craft.hatch_phase = 3;
        }
        break;
    }
    case 3:
        craft.hatch = craft.hatch - hatch_rate;
        if (craft.hatch < hatch_shut_test) {
            craft.hatch = Fx{};
            craft.hatch_phase = 0;
            craft.step = CraftStep::Release;
        }
        break;
    default:
        break;
    }
    craft.velocity = craft.pos - craft.prev_pos;
}

// a radius-3 sphere at the craft, moving by its velocity,
// against the floor list and the faces of its cell. The port asks collision_query_sphere (faces,
// then edges, then corners) on a copy and takes a face or floor contact as the landing; without a
// stage (no battle) the wreck falls until its timer runs out
// decomp: collision_query_sphere_land 0x53424
bool craft_crash_landed(const StageWorld& stage, const StageCollision* ground) {
    if (!ground || !ground->loaded) {
        return false;
    }
    SphereQuery sphere;
    sphere.centre = stage.craft.pos;
    sphere.previous = stage.craft.prev_pos;
    sphere.velocity = stage.craft.velocity;
    sphere.radius = fx(3);
    return collision_query_sphere(*ground, sphere) & (contact_face | contact_floor);
}

void craft_step_crash(StageWorld& stage, const StageCollision* ground) {
    SupplyCraft& craft = stage.craft;
    craft.prev_pos = craft.pos;
    craft.pos += craft.velocity;
    stage_event(stage, StageEventKind::CraftCrash, craft.pos);
    craft.crash_timer -= 1;
    if (craft_crash_landed(stage, ground) || craft.crash_timer < 0) {
        stage_event(stage, StageEventKind::SoundStop, craft.pos, bank_battle,
                    se_supply_craft_engine_loop);
        stage_event(stage, StageEventKind::Effect, craft.pos, 0x3F);
        craft.state = craft_parked;
        craft.step = CraftStep::CollectedClear;
    }
}

// z along the flat velocity, x = (z.z, 0, -z.x), y = z x x, in
// the original's frame, then mirrored into the port's (M W M with M the z flip)
// decomp: matrix_orthonormalize 0x697CA
FxMat4 craft_matrix(const SupplyCraft& craft) {
    const FxVec3 z = normalized(FxVec3{craft.velocity.x, Fx{}, -craft.velocity.z});
    const FxVec3 x = normalized(FxVec3{z.z, Fx{}, -z.x});
    const FxVec3 y = normalized(cross(z, x));
    FxMat4 m;
    m.x = {x.x, x.y, -x.z};
    m.y = {y.x, y.y, -y.z};
    m.z = {-z.x, -z.y, z.z};
    m.w = craft.pos;
    return m;
}

// pickups_draw, gameplay despite the name: moves, times and bursts the crates, then
// supply_craft_start starts the next craft once all three are clear
// decomp: pickups_draw 0x69CD2
void crates_step(StageWorld& stage, WorldRandom& random) {
    SupplyCraft& craft = stage.craft;
    for (int i = 0; i < 3; ++i) {
        Crate& crate = stage.crates[i];
        switch (crate.state) {
        case crate_aboard:
        case crate_gone:
            crate.pos = transform_coord(crate_seat, craft.matrix);
            break;
        case crate_falling:
            crate.pos.y = crate.pos.y - crate_fall;
            if (crate.pos.y < fx(1) + craft.height) {
                crate.pos.y = fx(1) + craft.height;
                crate.state = crate_lying;
            }
            break;
        case crate_lying:
            if (craft.state != craft_paused) {
                crate.life -= 1;
                if (crate.life < 0) {
                    crate.state = crate_burst;
                }
            }
            break;
        case crate_burst:
            stage_event(stage, StageEventKind::Effect, crate.pos, 0x40);
            crate.state = crate_gone;
            break;
        default:
            break;
        }
        // the glow blinks faster near the end: phase += 0.05 x (900 / life above 60 updates,
        // else 15), here 0.025 x (1800 / life above 120 steps, else 15) (cosmetic)
        if (crate.state != crate_aboard && crate.state != crate_gone && i <= craft.crate_count) {
            const Fx rate = crate.life > 120 ? fx_ratio(1800, crate.life) : fx(15);
            crate.glow = crate.glow + fx_mul_near(fx_f(0.025), rate);
            if (crate.glow > pi) {
                crate.glow = crate.glow - pi;
            }
        }
    }
    if (craft.state == craft_parked) {
        int total = 0;
        for (const Crate& crate : stage.crates) {
            total += crate.state;
        }
        if (total == 0) {
            stage_route_choose(stage, random);
        }
    }
}

} // namespace

// decomp: stage_route_choose 0x668C0
void stage_route_choose(StageWorld& stage, WorldRandom& random) {
    // stage_route_choose and supply_craft_start: a route row selector + choice * 8
    const int selector = stage.setup.selector;
    const int choice = rand_int(random, stage.data->route_counts[selector]);
    stage_route_install(stage, stage.data->choices[selector + choice * 8], route_speed);
    pickups_set_up(stage, random);
}

// decomp: supply_craft_update 0x670FC
void supply_craft_update(StageWorld& stage, WorldRandom& random, const StageCollision* ground) {
    SupplyCraft& craft = stage.craft;
    if (stage.setup.craft_frozen || craft.state == craft_off) {
        return;
    }
    if (craft.state == craft_installed) {
        stage_event(stage, StageEventKind::CraftJets, craft.pos);
        craft.state = craft_flying;
    }
    if (craft.state != craft_paused) {
        switch (craft.step) {
        case CraftStep::Ingress:
            craft_step_ingress(stage);
            break;
        case CraftStep::Release:
            craft_step_release(stage);
            break;
        case CraftStep::FallHover:
            craft_step_fall_hover(stage);
            break;
        case CraftStep::Crash:
            craft_step_crash(stage, ground);
            break;
        case CraftStep::CollectedClear:
            // pickups_collected_clear
            for (Crate& crate : stage.crates) {
                if (crate.state == crate_gone) {
                    crate.state = crate_aboard;
                }
            }
            break;
        }
    }
    craft.matrix = craft_matrix(craft);
    craft.body_prev = craft.body;
    craft.body = transform_coord(body_point, craft.matrix);
    if (!stage.setup.ending) {
        crates_step(stage, random);
    }
}

// decomp: pickup_collect_test 0x6735B
PickupResult pickup_collect_test(StageWorld& stage, FxVec3& centre, Fx prev_y, Fx radius,
                                 bool collect) {
    PickupResult result;
    SupplyCraft& craft = stage.craft;
    // the craft's body is solid: set on top and carried, or pushed out sideways
    if (craft.state != craft_off && craft.state != craft_parked) {
        const FxVec3 out = centre - craft.body;
        const Fx height = centre.y - craft.body.y;
        if (shorter_than(out, radius + fx(3)) && radius + fx(1) > height) {
            if (craft.body.y + radius < prev_y) {
                centre.y = radius + fx(1) + craft.body.y;
                centre += craft.velocity;
                result.bits |= 8;
            } else {
                centre = craft.body + scaled_to(out, radius + fx(3));
            }
            result.bits |= hit_craft;
        }
    }
    for (int i = 0; i < 3; ++i) {
        Crate& crate = stage.crates[i];
        if (crate.state == crate_aboard || crate.state == crate_gone) {
            continue;
        }
        // retail lets a wreck's sphere take a crate too, for nothing (docs/bugs.md): only the
        // living collect here
        if (collect && shorter_than(centre - crate.pos, radius + fx(1))) {
            // pickup_apply runs its fighter side after the step (pickup_apply.cpp) with
            // the kind and where the crate was; the crate is cleared now
            result.kinds[i] = crate.kind;
            result.at[i] = crate.pos;
            pickup_clear(crate);
            result.bits |= hit_crate;
        }
    }
    return result;
}

// decomp: supply_craft_shot_down 0x6779A
bool supply_craft_shot_down(StageWorld& stage) {
    SupplyCraft& craft = stage.craft;
    if (craft.state == craft_off || craft.state == craft_crashing) {
        return false;
    }
    stage_event(stage, StageEventKind::Effect, craft.pos, 0x3F);
    craft.velocity = craft.velocity * crash_keep;
    craft.velocity.y = crash_drop;
    craft.step = CraftStep::Crash;
    craft.state = craft_crashing;
    craft.crash_timer = crash_steps;
    return true;
}

// decomp: supply_craft_pause 0x67299
bool supply_craft_pause(StageWorld& stage) {
    if (stage.craft.state == craft_paused) {
        return false;
    }
    stage.craft.saved_state = stage.craft.state;
    stage.craft.state = craft_paused;
    return true;
}

// decomp: supply_craft_resume 0x672D0
bool supply_craft_resume(StageWorld& stage) {
    if (stage.craft.state != craft_paused) {
        return false;
    }
    stage.craft.state = stage.craft.saved_state;
    if (stage.craft.state == craft_flying) {
        stage_event(stage, StageEventKind::CraftJets, stage.craft.pos);
    } else if (stage.craft.state == craft_dropping || stage.craft.state == craft_hatch) {
        stage_event(stage, StageEventKind::CraftFire, stage.craft.pos);
    }
    return true;
}

// decomp: supply_craft_route_for_ending 0x6781F
void supply_craft_route_for_ending(StageWorld& stage, int route) {
    if (route < 0 || route >= 7) {
        route = 0;
    }
    // ending_route_speeds, per update -> half a step (R1)
    const Fx speed = fx_raw(stage.data->ending_speeds[route].raw / 2);
    stage_route_install(stage, stage.data->choices[route + 24], speed);
}

// decomp: supply_craft_active_clear 0x6785A
void supply_craft_active_clear(StageWorld& stage) {
    stage.craft.state = craft_off;
}
