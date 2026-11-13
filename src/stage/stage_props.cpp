// The stage's props (decomp src/hand/u_06eda0.c, src/od/u_072650.c): parked cars, crates, signs
// and the swinging gates. Each has a per-update callback, run by the stage task before it draws
// the prop; a shot or a touch swaps the callback for the prop type's row of the hit or touch
// table. All in the world, fixed point.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/stage_collision.hpp"
#include "stage/stage_world.hpp"

namespace {

// burning: frame_cadence * 6 (prop_hit_burn and kin); written with frame_cadence, so 6 s
// at either rate: 180 updates in retail, 360 steps here (docs/sixty.md "burning props", auto)
constexpr int burn_steps = 60 * 6;
// gate swing 0.019333333 rad per update (gate_opening_update, gate_closing_update) -> half a step
// (R5), rounded up so the swing still reaches its 0.58 clamp at the end of the opening window
constexpr Fx gate_swing = fx_raw(634); // fx_f(0.019333333) is 1267 raw
constexpr Fx gate_reach = fx_f(0.58f);
// gate timer, update counts doubled (R4, docs/sixty.md "gate timer"): swing while <= 30 -> 60,
// passable from 20 -> 40, closes at 150 -> 300, closing 30 -> 60
constexpr int gate_swing_steps = 60;
constexpr int gate_passable_steps = 40;
constexpr int gate_hold_steps = 300;

// D_0022E7F8, the hit table, by prop type
PropStep hit_step(int type) {
    switch (type) {
    case 0: return PropStep::Alternate;
    case 1: return PropStep::TwoStage;
    case 2:
    case 3:
    case 4: return PropStep::Explode;
    case 5: return PropStep::Vanish;
    case 7: return PropStep::Burn;
    case 8: return PropStep::TimerClear;
    default: return PropStep::None;
    }
}

// D_0022E81C, the touch table: gates open, type 8 clears its timer, the rest do nothing
PropStep touch_step(int type) {
    return type == 6 ? PropStep::GateTouch : type == 8 ? PropStep::TimerClear : PropStep::None;
}

FxVec3 prop_at(const Prop& prop) {
    return prop.transform.w;
}

void prop_sound(StageWorld& stage, const Prop& prop, int cue, unsigned flags) {
    stage_event(stage, StageEventKind::Sound, prop.sound_pos, bank_battle, cue, flags);
}

// the burn's sounds: the blast, then the fire looped until the burn ends
void burn_sounds(StageWorld& stage, const Prop& prop) {
    prop_sound(stage, prop, se_explosion_large, sound_positioned);
    prop_sound(stage, prop, se_prop_fire_loop, sound_positioned | sound_loop);
}

// a gate's transform: its swing about y, then as placed (the port's y turn is the original's
// negated, docs/assets.md)
void gate_turn(Prop& prop) {
    const FxMat3 swing = mat3_rotation_y(-angle_from_radians(prop.gate_angle));
    prop.transform = mat4_mul(mat4_from(swing, FxVec3{}), prop.rest);
}

// both halves of a gate pair start to open (gate_touch's flag changes)
void gate_start_opening(Prop& prop) {
    prop.step = PropStep::GateOpening;
    prop.flags |= prop_opening;
    prop.flags &= ~(prop_closing | prop_touched | prop_called);
}

void gate_start_closing(Prop& prop) {
    prop.flags &= ~(prop_opening | prop_open);
    prop.flags |= prop_shutting | prop_closing;
    prop.timer = 0;
    prop.step = PropStep::GateClosing;
}

// the other half of the gate pair: the second half follows the first in the placements
Prop& gate_partner(StageWorld& stage, Prop& prop) {
    const std::ptrdiff_t at = &prop - stage.props.data();
    return stage.props[prop.gate_second ? at - 1 : at + 1];
}

} // namespace

// decomp: prop_init 0x70B00
void prop_init(Prop& prop, const PropPlacement& placement, const StagePropSet& set) {
    // prop_init: rotation x, then y, then z, then the translation
    const FxMat3 turn = mat3_mul(mat3_mul(mat3_rotation_x(placement.rot[0]),
                                          mat3_rotation_y(placement.rot[1])),
                                 mat3_rotation_z(placement.rot[2]));
    prop = Prop{};
    prop.rest = mat4_from(turn, placement.pos);
    prop.transform = prop.rest;
    prop.sound_pos = placement.pos;
    prop.model = placement.model;
    prop.alt_model = placement.alt_model;
    const int shape = placement.shape;
    prop.type = shape >= 0 && shape < static_cast<int>(set.types.size()) ? set.types[shape] : 8;
    prop.cell[0] = placement.cell[0];
    prop.cell[1] = placement.cell[1];
    prop.cell[2] = placement.cell[2];
    prop.gate_second = placement.gate != -1;
    // the shape's twelve triangles, placed; normal (v2 - v0) x (v1 - v0) in the port frame is
    // the original's (v1 - v0) x (v2 - v0) mirrored
    if (shape >= 0 && shape < static_cast<int>(set.shapes.size())) {
        for (int t = 0; t < 12; ++t) {
            FxTri& tri = prop.tris[t];
            tri.v0 = transform_coord(set.shapes[shape].v[t][0], prop.rest);
            tri.v1 = transform_coord(set.shapes[shape].v[t][1], prop.rest);
            tri.v2 = transform_coord(set.shapes[shape].v[t][2], prop.rest);
            tri.normal = unit_cross(tri.v2 - tri.v0, tri.v1 - tri.v0);
        }
    }
}

void step_prop(StageWorld& stage, Prop& prop) {
    switch (prop.step) {
    case PropStep::None:
        break;
    case PropStep::Alternate:
        // stage_prop_alternate_transition: burns on its alternate model
        stage_event(stage, StageEventKind::Effect, prop_at(prop), 0xE);
        if (prop.alt_model != -1) {
            prop.model = prop.alt_model;
        }
        prop.flags = (prop.flags | prop_burnt) & ~prop_hit;
        prop.step = PropStep::TimedParticles;
        burn_sounds(stage, prop);
        prop.timer = burn_steps;
        break;
    case PropStep::TimedParticles:
        // stage_prop_timed_particles: the fire's light (presentation reads the timer)
        prop.timer -= 1;
        if (prop.timer == 0) {
            stage_event(stage, StageEventKind::SoundStop, prop.sound_pos, bank_battle,
                        se_prop_fire_loop);
            prop.step = PropStep::TimerClear;
        }
        break;
    case PropStep::TwoStage:
        // prop_hit_two_stage: the first hit burns it, the second blows it away
        if (prop.flags & prop_burnt) {
            stage_event(stage, StageEventKind::Effect, prop_at(prop), 0xF);
            prop.flags = (prop.flags & ~prop_burnt) | prop_hidden | prop_gone;
            prop.timer = 0;
        } else {
            stage_event(stage, StageEventKind::Effect, prop_at(prop), 0xD);
            if (prop.alt_model != -1) {
                prop.model = prop.alt_model;
            }
            prop.flags |= prop_burnt;
            burn_sounds(stage, prop);
            prop.timer = burn_steps;
        }
        prop.flags &= ~prop_hit;
        prop.step = PropStep::BurnUpdate;
        break;
    case PropStep::BurnUpdate:
        // prop_burn_update
        prop.timer -= 1;
        if (prop.flags & prop_burnt) {
            if (prop.timer <= 0) {
                stage_event(stage, StageEventKind::SoundStop, prop.sound_pos, bank_battle,
                            se_prop_fire_loop);
                prop.step = PropStep::None;
            }
        } else if (prop.timer <= 0) {
            prop.step = PropStep::TimerClear;
        }
        break;
    case PropStep::Explode:
        stage_event(stage, StageEventKind::Effect, prop_at(prop), 0xF);
        prop.flags = (prop.flags | prop_hidden | prop_gone) & ~prop_hit;
        prop.step = PropStep::None;
        prop_sound(stage, prop, se_explosion_large, sound_positioned);
        break;
    case PropStep::Vanish:
        stage_event(stage, StageEventKind::Effect, prop_at(prop), 0x3A);
        prop.flags = (prop.flags | prop_hidden | prop_gone) & ~prop_hit;
        prop.step = PropStep::None;
        break;
    case PropStep::Burn:
        // prop_hit_burn: alternate model, burning, no longer solid
        stage_event(stage, StageEventKind::Effect, prop_at(prop), 0xD);
        if (prop.alt_model != -1) {
            prop.model = prop.alt_model;
        }
        prop.flags = (prop.flags | prop_gone | prop_burnt) & ~prop_hit;
        prop.step = PropStep::BurnUpdate;
        burn_sounds(stage, prop);
        prop.timer = burn_steps;
        break;
    case PropStep::GateTouch: {
        // gate_touch: in VS and the 0x16, 0x17 rules only a gate the intro opened (flags
        // exactly 0x800) swings
        if (stage.setup.gates_locked && prop.flags != prop_called) {
            prop.step = PropStep::None;
            break;
        }
        const int timer = prop.timer;
        gate_start_opening(prop);
        Prop& other = gate_partner(stage, prop);
        gate_start_opening(other);
        other.timer = timer;
        stage_event(stage, StageEventKind::Sound, other.sound_pos, bank_battle_player,
                    se_gate_motor, sound_positioned);
        stage_event(stage, StageEventKind::Sound, other.sound_pos, bank_battle_player, se_gate_open,
                    sound_positioned);
        break;
    }
    case PropStep::GateOpening:
        // gate_opening_update: swing open, hold, then close
        if (prop.timer <= gate_swing_steps) {
            prop.gate_angle = prop.gate_second
                                  ? fx_min(prop.gate_angle + gate_swing, gate_reach)
                                  : fx_max(prop.gate_angle - gate_swing, -gate_reach);
            gate_turn(prop);
            prop.timer += 1;
            if (prop.timer >= gate_passable_steps) {
                prop.flags |= prop_open;
            }
        } else if (prop.timer >= gate_hold_steps) {
            prop.flags = (prop.flags & ~(prop_opening | prop_open)) | prop_closing;
            prop.timer = 0;
            prop.step = PropStep::GateClosing;
            stage_event(stage, StageEventKind::Sound, prop.sound_pos, bank_battle_player,
                        se_gate_motor, sound_positioned);
        } else {
            prop.timer += 1;
            prop.gate_angle = prop.gate_second ? gate_reach : -gate_reach;
            prop.flags |= prop_open;
            gate_turn(prop);
        }
        break;
    case PropStep::GateClosing:
        // gate_closing_update
        if (prop.timer < gate_swing_steps) {
            prop.gate_angle = prop.gate_second ? fx_max(prop.gate_angle - gate_swing, Fx{})
                                               : fx_min(prop.gate_angle + gate_swing, Fx{});
            gate_turn(prop);
            prop.timer += 1;
        } else {
            prop.flags = 0;
            prop.step = PropStep::None;
            prop.transform = prop.rest;
        }
        break;
    case PropStep::TimerClear:
        prop.timer = 0;
        break;
    }
}

bool prop_solid(const Prop& prop) {
    return !(prop.flags & prop_gone) && !(prop.flags & prop_open);
}

namespace {

// (int)(p / 15) per axis within one of the prop's cell
bool cell_near(const Prop& prop, FxVec3 p) {
    const Fx cell = fx(15);
    const int at[3] = {fx_trunc_div(p.x, cell), fx_trunc_div(p.y, cell),
                       fx_trunc_div(p.z, cell)};
    for (int k = 0; k < 3; ++k) {
        if (at[k] > prop.cell[k] + 1 || at[k] < prop.cell[k] - 1) {
            return false;
        }
    }
    return true;
}

} // namespace

// decomp: stage_props_collide_sphere 0x6F7AE
int stage_props_collide_sphere(StageWorld& stage, const StageCollision& grid,
                               SphereQuery& sphere) {
    int hits = 0;
    for (Prop& prop : stage_props(stage)) {
        if (!prop_solid(prop)) {
            continue;
        }
        // a prop whose cell is within one of the sphere's: its triangles, and touched or not
        if (cell_near(prop, sphere.centre)) {
            const int contact = collision_prop_sphere(grid, prop.tris, sphere);
            if (contact) {
                hits |= contact | surface_prop;
                prop.flags |= prop_touched;
            } else {
                prop.flags &= ~prop_touched;
            }
        }
        // a touch starts the type's touch callback (a gate opens, a closing gate runs the rest
        // of its swing backwards), unless the door scene shut it
        if (prop.flags & prop_shutting) {
            continue;
        }
        if (prop.flags & prop_touched && prop.step == PropStep::None) {
            prop.timer = 0;
            prop.step = touch_step(prop.type);
        } else if (prop.flags & prop_touched && prop.step == PropStep::GateClosing) {
            prop.timer = gate_swing_steps - prop.timer;
            prop.step = touch_step(prop.type);
        } else {
            prop.flags &= ~prop_touched;
        }
    }
    return hits;
}

bool stage_props_block(const StageWorld& stage, FxVec3 from, FxVec3 to) {
    bool blocked = false;
    for (const Prop& prop : stage_props(stage)) {
        PropRayHit hit;
        if (prop_solid(prop) && prop_ray_test(prop, from, to, false, &hit)) {
            blocked = true;
        }
    }
    return blocked;
}

// decomp: prop_ray_test 0x6FCB2
bool prop_ray_test(const Prop& prop, FxVec3 from, FxVec3 to, bool both_sides, PropRayHit* hit) {
    if (prop.flags & prop_gone) {
        return false;
    }
    // walk the segment in 15-unit steps until one lands within a cell of the prop; then test
    // every triangle from 2 units behind `from` (retail tests again on each later step, with the
    // same result)
    const FxVec3 dir = normalized(to - from);
    const int steps = fx_trunc_div(length(to - from), fx(15)) + 1;
    FxVec3 at = from;
    bool near = false;
    for (int i = 0; i < steps && !near; ++i) {
        near = cell_near(prop, at);
        at += dir * fx(15);
    }
    if (!near) {
        return false;
    }
    const FxVec3 start = from - dir * fx(2);
    bool found = false;
    for (int t = 0; t < 12; ++t) {
        FxSegmentHit crossing;
        if (!segment_tri_hit(prop.tris[t], start, to, both_sides, fx_f(0.05), &crossing)) {
            continue;
        }
        // collision_tri_segment_frame: the point lifted 0.05 along the face's normal, and its
        // distance from the start
        const FxVec3 point = crossing.point + prop.tris[t].normal * fx_f(0.05);
        const Fx distance = length(point - start);
        if (!found || distance < hit->distance) {
            hit->point = point;
            hit->tri = t;
            hit->distance = distance;
            found = true;
        }
    }
    return found;
}

int stage_prop_shot(StageWorld& stage, int index, bool* blast) {
    *blast = false;
    Prop& prop = stage.props[index];
    if (prop.step != PropStep::None) {
        return 0;
    }
    prop.flags |= prop_hit;
    prop.step = hit_step(prop.type);
    *blast = prop.type != 6 && prop.type != 5 && prop.type != 8;
    // battle_shots_update adds D_00203654[type] through a 10-byte stride: ten times the table
    // type 8 has no row of the 8 and reads the next table's 1472: it pays 0 here, like the gates
    // (docs/bugs.md)
    if (prop.type < 0 || prop.type >= 8) {
        return 0;
    }
    return stage.data->prop_scores[prop.type] * 10;
}

void stage_prop_beamed(StageWorld& stage, int index) {
    Prop& prop = stage.props[index];
    if (!(prop.flags & prop_gone) && prop.step == PropStep::None) {
        prop.flags |= prop_hit;
        prop.step = hit_step(prop.type);
    }
}

// decomp: gate_open 0x705E7
bool gate_open(StageWorld& stage, int gate) {
    if (!stage.ready || gate < 0 || gate >= 4 || stage.gates[gate] == -1) {
        return false;
    }
    Prop& prop = stage.props[stage.gates[gate]];
    if (prop.step == PropStep::GateOpening) {
        return false;
    }
    prop.flags |= prop_called;
    prop.timer = prop.step == PropStep::None ? 0 : gate_swing_steps - prop.timer;
    prop.step = touch_step(prop.type);
    return true;
}

// decomp: gate_close 0x7072E
bool gate_close(StageWorld& stage, int gate) {
    if (!stage.ready || gate < 0 || gate >= 4 || stage.gates[gate] == -1) {
        return false;
    }
    Prop& prop = stage.props[stage.gates[gate]];
    // only once the swing is over (timer 30 updates: 60 steps)
    if (prop.step != PropStep::GateOpening || prop.timer < gate_swing_steps) {
        return false;
    }
    gate_start_closing(prop);
    gate_start_closing(gate_partner(stage, prop));
    return true;
}

// decomp: stage_door_available 0x70A9F
bool stage_door_available(const StageWorld& stage, int gate) {
    if (!stage.ready || gate < 0 || gate >= 4 || stage.gates[gate] == -1) {
        return true;
    }
    return stage.props[stage.gates[gate]].step == PropStep::None;
}
