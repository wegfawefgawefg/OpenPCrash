// The stage task in the world: battle_stage_init to set it up, and the simulation half of
// stage_props_render once a step (decomp src/hand/u_06eda0.c).
#include "stage/stage_world.hpp"

namespace {

// the training stage's random props (battle_stage_init, selectors 6 and 7 outside free play):
// 80 distinct grid cells 5 units apart, none next to the middle, then 10..79 crates on them and a
// fifth as many stacked on the first ones, 5 units up each time
void place_training_props(StageWorld& stage, const StagePropSet& set, WorldRandom& random,
                          int& count) {
    int cells[80][2];
    FxVec3 spots[80];
    for (int i = 0; i < 80; ++i) {
        // (int)(rand_float(34) - 17), truncated toward zero
        const int gx = fx_trunc_div(rand_fx(random, fx(34)) - fx(17), fx(1));
        const int gz = fx_trunc_div(rand_fx(random, fx(34)) - fx(17), fx(1));
        if (gx >= -1 && gx <= 1 && gz >= -1 && gz <= 1) {
            --i;
            continue;
        }
        bool taken = false;
        for (int j = 0; j < i && !taken; ++j) {
            taken = cells[j][0] == gx && cells[j][1] == gz;
        }
        if (taken) {
            --i;
            continue;
        }
        cells[i][0] = gx;
        cells[i][1] = gz;
        spots[i] = {fx(gx * 5), Fx{}, fx(-gz * 5)}; // the original's (x, 0, z), z negated
    }
    int groups[2];
    groups[0] = rand_int(random, 70) + 10;
    groups[1] = groups[0] / 5; // (int)(n * 0.2f): 0.2f is a hair over 0.2, so exactly n / 5
    // every random prop is placement row 8 with its model and place written over: the same shape
    // (and so the same collision and behaviour) whatever crate model it shows (docs/bugs.md)
    PropPlacement row = set.placements[8];
    for (int group = 0; group < 2; ++group) {
        for (int j = 0; j < groups[group]; ++j) {
            row.pos = spots[j];
            row.model = rand_int(random, 4) + (stage.setup.selector == 6 ? 0x492 : 0x498);
            const Fx cell = fx(15);
            row.cell[0] = fx_trunc_div(row.pos.x, cell);
            row.cell[1] = fx_trunc_div(row.pos.y, cell);
            row.cell[2] = fx_trunc_div(row.pos.z, cell);
            if (Prop* prop = stage_prop_add(stage)) {
                prop_init(*prop, row, set);
            }
            ++count;
            spots[j].y = spots[j].y + fx(5);
        }
    }
}

} // namespace

void stage_event(StageWorld& stage, StageEventKind kind, FxVec3 at, int a, int b,
                 unsigned flags) {
    StageEvent event;
    event.kind = kind;
    event.at = at;
    event.a = a;
    event.b = b;
    event.flags = flags;
    stage_event_push(stage, event);
}

void stage_event_push(StageWorld& stage, const StageEvent& event) {
    if (stage.event_count < stage_event_capacity) {
        stage.events[static_cast<std::size_t>(stage.event_count++)] = event;
    }
}

Prop* stage_prop_add(StageWorld& stage) {
    if (stage.prop_count >= stage_prop_capacity) {
        return nullptr;
    }
    Prop& prop = stage.props[static_cast<std::size_t>(stage.prop_count++)];
    prop = Prop{};
    return &prop;
}

// decomp: battle_stage_init 0x6F02B
void stage_world_init(StageWorld& stage, const StageData& data, const StageSetup& setup,
                      WorldRandom& random) {
    stage = StageWorld{};
    stage.data = &data;
    stage.setup = setup;
    // the route and the crate kinds come first (stage_route_choose), then the props
    stage_route_choose(stage, random);
    const StagePropSet& set = data.props[setup.selector];
    int count = 0;
    int gates = 0;
    const auto place = [&](const PropPlacement& placement) {
        if (Prop* prop = stage_prop_add(stage)) {
            prop_init(*prop, placement, set);
        }
        if (placement.gate != -1 && gates < 4) {
            stage.gates[gates++] = count;
        }
        ++count;
    };
    if (setup.selector == 6 || setup.selector == 7) {
        for (int i = 0; i < 8 && i < static_cast<int>(set.placements.size()); ++i) {
            place(set.placements[i]);
        }
        if (!setup.free_play && set.placements.size() > 8) {
            // up to 8 + 79 + 15 = 102: retail writes the last two past its 100 props
            // (docs/bugs.md); the port keeps them all
            place_training_props(stage, set, random, count);
        }
    } else {
        for (const PropPlacement& placement : set.placements) {
            if (count >= 100) {
                break;
            }
            place(placement);
        }
    }
    stage.ready = true;
}

// decomp: stage_props_render 0x6F5ED
void step_stage(StageWorld& stage, WorldRandom& random, std::int64_t step,
                const StageCollision* ground) {
    // each prop's callback (stage_prop_render runs it before drawing)
    if (stage.ready) {
        for (Prop& prop : stage_props(stage)) {
            step_prop(stage, prop);
        }
    }
    supply_craft_update(stage, random, ground);

    // the hit static's scratches and the shake: retail draws 5 and 3 table values an update
    // while they run, so the port counts and draws on even steps (docs/sixty.md R11). The
    // static's arguments are evaluated right to left: speed, life, y; then life, y
    if (step % 2 != 0) {
        return;
    }
    if (stage.hit_static_frames > 0) {
        StageEvent scratch;
        scratch.kind = StageEventKind::Scratch;
        scratch.value2 = rand_fx(random, fx(3));
        scratch.a = fx_trunc_div(rand_fx(random, fx(30)) + fx(1), fx(1));
        scratch.value = rand_fx(random, fx(300));
        scratch.b = 0;
        stage_event_push(stage, scratch);
        scratch.value2 = Fx{};
        scratch.a = fx_trunc_div(rand_fx(random, fx(3)) + fx(1), fx(1));
        scratch.value = rand_fx(random, fx(300));
        scratch.b = 1;
        stage_event_push(stage, scratch);
        stage.hit_static_frames -= 1;
    }
    if (stage.hit_shake_frames > 0) {
        // hit_shake_jitter: the up vector's tilt (t, 1 - t, 0), then the cockpit's x, y;
        // held until the next even step
        stage.shake_tilt = rand_fx(random, fx_f(0.1f));
        stage.shake_x = rand_fx(random, fx_f(0.8f));
        stage.shake_y = rand_fx(random, fx_f(0.8f));
        stage.hit_shake_frames -= 1;
    } else {
        // hit_shake_clear
        stage.shake_tilt = stage.shake_x = stage.shake_y = Fx{};
    }
}

// decomp: hit_static_frames_set 0x6F794
void hit_static_frames_set(StageWorld& stage, int frames) {
    stage.hit_static_frames = frames;
}

// decomp: hit_shake_frames_set 0x6F7A1
void hit_shake_frames_set(StageWorld& stage, int frames) {
    stage.hit_shake_frames = frames;
}
