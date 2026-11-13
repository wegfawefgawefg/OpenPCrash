#pragma once
// The stage's part of the world: its props, the supply craft and its three crates, and the hit
// static and shake counters, all stepped by the stage task's update (stage_props_render,
// after the fighters, the shot pool and the weapon tasks: decomp docs/port/battle.md, "One
// update"). Fixed point; every per-update constant is converted for the 60 Hz step
// (docs/sixty.md, "Pickups and the supply craft", "Battle flow and the stage").
//
// What it does that only presentation shows (sounds, particle sets, the static's scratches) it
// says in `events`, cleared at the start of each world step (step_world, so the fighters' and
// weapons' events before the stage's survive); presentation reads them after the step. The props
// and events are fixed arrays with counts, so the world is a flat value a snapshot copies without
// allocating (docs/multiplayer.md "Change now", item 9).
#include "fx/hit.hpp"
#include "fx/mat.hpp"
#include "stage/stage_data.hpp"
#include "world/fighter_slots.hpp"
#include "world/world_random.hpp"

#include <array>
#include <cstdint>
#include <span>

// supply_craft_hit_test's bits: the craft, a crate
inline constexpr int hit_craft = 0x80000;
inline constexpr int hit_crate = 0x100000;

struct StageCollision;
struct SphereQuery;

// Prop.flags (include/stage.h)
enum PropFlag : int {
    prop_hit = 1,         // shot this update
    prop_touched = 2,     // a fighter's sphere touched it
    prop_gone = 0x10,     // no collision, no shots
    prop_hidden = 0x20,   // not drawn
    prop_opening = 0x40,  // gate
    prop_closing = 0x80,  // gate
    prop_open = 0x200,    // gate passable: no collision, no shots
    prop_burnt = 0x400,   // on its alternate model
    prop_called = 0x800,  // gate opened by the intro (gate_open), so it opens in VS too
    prop_shutting = 0x1000, // gate closed by the door scene: touching doesn't reopen it
};

// the prop's per-update callback (Prop.callback), one case each
enum class PropStep : std::uint8_t {
    None,           // prop_callback_noop
    Alternate,      // stage_prop_alternate_transition
    TimedParticles, // stage_prop_timed_particles
    TwoStage,       // prop_hit_two_stage
    BurnUpdate,     // prop_burn_update
    Explode,     // prop_hit_explode_2, _3 and _4 (the same)
    Vanish,      // prop_hit_vanish
    GateTouch,   // gate_touch
    GateOpening, // gate_opening_update
    GateClosing, // gate_closing_update
    Burn,        // prop_hit_burn
    TimerClear,  // prop_timer_clear
};

// a placed prop (include/stage.h Prop)
struct Prop {
    FxMat4 rest;      // as placed
    FxMat4 transform; // drawn at; a gate's swing
    int type = 0;     // behaviour, the row of the hit and touch tables
    PropStep step = PropStep::None;
    int model = -1;
    int alt_model = -1;
    int timer = 0;
    int flags = 0;
    int cell[3] = {};
    bool gate_second = false;
    FxVec3 sound_pos;
    FxTri tris[12]; // placed once; a swinging gate's don't move
    Fx gate_angle;  // radians, the original's sign (+ for the second half)
};

// craft_state
enum CraftState : int {
    craft_off = 0,
    craft_flying = 1,   // stage_route_ingress
    craft_dropping = 2, // stage_route_pickup_release
    craft_hatch = 3,    // supply_craft_fall_hover
    craft_crashing = 4, // supply_craft_crash
    craft_parked = 6,   // pickups_collected_clear, waiting for the crates to clear
    craft_paused = 7,   // supply_craft_pause
    craft_installed = 8,
};

enum class CraftStep : std::uint8_t { Ingress, Release, FallHover, Crash, CollectedClear };

// crate_state
enum CrateState : int {
    crate_aboard = 0,
    crate_falling = 1,
    crate_lying = 2,
    crate_burst = 3,
    crate_gone = 4,
};

struct Crate {
    FxVec3 pos;
    int drop_delay = 0; // steps
    int life = 0;       // steps
    int state = crate_aboard;
    int kind = 0; // 0..3 score, 4..6 repair, 7..9 refill
    Fx glow;      // radians 0..pi, cosmetic
    bool shootable = true;
    bool claimed[fighter_slots] = {}; // pickup_find_nearest's AI claims (never set: see supply_craft.cpp)
};

struct SupplyCraft {
    int state = craft_off;
    int saved_state = craft_off; // while paused
    CraftStep step = CraftStep::Ingress;
    int route = 0;        // StageData::routes row
    int segment = 0;
    int last_segment = 0; // the route's count: it reads one segment past it
    Fx t;                 // along the segment
    Fx speed;             // per step
    FxVec3 pos, prev_pos, velocity; // velocity: this step's move
    int ingress = 0, release_start = 0, release_end = 0;
    Fx height;            // crates land on height + 1
    Fx rotor;             // radians, cosmetic
    Fx hatch;             // radians
    int hatch_phase = 0;  // 1 opening, 2 dropping, 3 closing
    int crate_count = 3;
    bool shootable = true;
    int crash_timer = 0;
    FxMat4 matrix;          // from the flat velocity (matrix_orthonormalize)
    FxVec3 body, body_prev; // the body point (0, 0, -2) through the matrix: its solid sphere
};

enum class StageEventKind : std::uint8_t {
    Sound,     // bank a, cue b, flags (audio.hpp SoundFlags), at
    SoundStop, // bank a, cue b
    Effect,    // effect set a at `at` (effect_set_play)
    Scratch,   // screen_scratch_add: y, life a, speed, kind b
    CraftJets,
    CraftFire,
    CraftCrash, // supply_craft_*_start: its particle sets
    // for the effects (src/effects; impacts and muzzles come from world.combat.events):
    Light,   // point_light_add: a ARGB, value the size, at
    GetItem, // effect_get_item: a the collector's slot, b the crate's kind (0..9), at the
             // crate
};

struct StageEvent {
    StageEventKind kind = StageEventKind::Sound;
    std::int64_t step = 0; // World::step it happened on (step_world stamps it)
    int a = 0, b = 0;
    unsigned flags = 0;
    FxVec3 at;
    Fx value, value2;
};

// how the battle around the stage is set up (battle_stage_init's reads of the game state)
struct StageSetup {
    int selector = 0;
    bool free_play = false; // menu state 0x2A: the training stage keeps its 8 fixed props; no craft
    bool craft_frozen = false; // menu states 0x2A and 0x2F: the craft is rolled but never stepped
    bool vs = false;
    bool gates_locked = false; // VS or tournament rule 0x16, 0x17: touching a gate does nothing
    int crate_tournament = 6;  // whose pickup_weights roll the crate kinds
    bool ending = false;       // menu state 0xA: the ending's craft, no crates
};

// the prop pool: retail's 100 (findings.md), and the two the training stage's random props write
// past it (8 + 79 + 15, docs/bugs.md), which the port keeps
inline constexpr int stage_prop_capacity = 102;
// a step's stage events: the most a demo fight made in one step is far below (docs/multiplayer.md
// "Measured"); more are dropped, which only presentation would notice
inline constexpr int stage_event_capacity = 64;

struct StageWorld {
    const StageData* data = nullptr; // constant tables
    StageSetup setup;
    bool ready = false; // stage_props_ready: battle_stage_init done
    int prop_count = 0;
    std::array<Prop, stage_prop_capacity> props;
    int gates[4] = {-1, -1, -1, -1}; // prop of each gate pair's second half, by entry point
    SupplyCraft craft;
    Crate crates[3];
    int hit_static_frames = 0;
    int hit_shake_frames = 0;
    // the seat (controller) whose hit set the static and shake, so a view shows them only for that
    // seat (docs/multiplayer.md "Change now", item 7); the world's draws are the same either way
    int hit_seat = -1;
    Fx shake_tilt, shake_x, shake_y; // hit_shake_jitter's three draws; 0 when clear
    int event_count = 0;             // this step's events
    std::array<StageEvent, stage_event_capacity> events;
};

// the placed props and this step's events, for range-for
// decomp: stage_props 0x972F10
inline std::span<Prop> stage_props(StageWorld& stage) {
    return {stage.props.data(), static_cast<std::size_t>(stage.prop_count)};
}
// decomp: stage_props 0x972F10
inline std::span<const Prop> stage_props(const StageWorld& stage) {
    return {stage.props.data(), static_cast<std::size_t>(stage.prop_count)};
}
inline std::span<const StageEvent> stage_events(const StageWorld& stage) {
    return {stage.events.data(), static_cast<std::size_t>(stage.event_count)};
}
// a new prop after the placed ones (null when the pool is full)
Prop* stage_prop_add(StageWorld& stage);

// battle_stage_init: the route and crate kinds, then the props (the training stage's
// random ones from the world RNG), the gates
void stage_world_init(StageWorld& stage, const StageData& data, const StageSetup& setup,
                      WorldRandom& random);
// stage_props_render's simulation: the props' callbacks, the supply craft and crates,
// the hit static's and shake's draws (on even steps: docs/sixty.md R11). ground is the stage's
// collision (the shot-down craft lands on it), null outside a battle
void step_stage(StageWorld& stage, WorldRandom& random, std::int64_t step,
                const StageCollision* ground);

// props (stage_props.cpp)
void prop_init(Prop& prop, const PropPlacement& placement, const StagePropSet& set);
void step_prop(StageWorld& stage, Prop& prop);
// not gone and not an open gate: fighters collide with it
bool prop_solid(const Prop& prop);
// stage_props_collide_sphere: a fighter's sphere against the solid props whose cell is within one
// of its own (collision_prop_sphere), each prop marked touched or not, a touch starting its touch
// callback. The contact bits, with surface_prop (0x20000) when any prop was met
int stage_props_collide_sphere(StageWorld& stage, const StageCollision& grid, SphereQuery& sphere);
// sv_contact_route's last check of the move: whether the segment from -> to passes through a solid
// prop's face from outside (prop_ray_test, one side)
bool stage_props_block(const StageWorld& stage, FxVec3 from, FxVec3 to);
// prop_ray_test: a segment against one prop's triangles (both_sides the original's
// side). The nearest hit's point (lifted 0.05 off the face) and triangle
struct PropRayHit {
    FxVec3 point;
    int tri = -1;
    Fx distance;
};
bool prop_ray_test(const Prop& prop, FxVec3 from, FxVec3 to, bool both_sides, PropRayHit* hit);
// a shot's projectile hits prop i (battle_shots_update's case 3): the hit callback starts if
// the prop is idle. Returns the score the shooter gains (0 when it was busy); *blast says whether
// the shot also sets off the shooter's blast and bark 0x1F
int stage_prop_shot(StageWorld& stage, int prop, bool* blast);
// a beam sweeping over prop i (prop_ray_any): the hit callback starts if idle, no score
void stage_prop_beamed(StageWorld& stage, int prop);
// the gates (gate_open, gate_close, stage_door_available)
bool gate_open(StageWorld& stage, int gate);
bool gate_close(StageWorld& stage, int gate);
bool stage_door_available(const StageWorld& stage, int gate);

// the supply craft and crates (supply_craft.cpp)
void stage_route_choose(StageWorld& stage, WorldRandom& random);
void supply_craft_update(StageWorld& stage, WorldRandom& random, const StageCollision* ground);
// pickup_collect_test, from a fighter's sphere query: the craft's body is solid (pushed
// out, or set on top and carried), crates within radius + 1 are collected (when `collect`) and
// cleared. centre is moved; prev_y is the query's previous centre height. Bits as the original's
// (8 on top, 0x80000 the craft, 0x100000 a crate); each collected crate's kind and position go
// to kinds and at (-1 none) for pickup_apply (stage/pickup_apply.hpp)
struct PickupResult {
    int bits = 0;
    int kinds[3] = {-1, -1, -1};
    FxVec3 at[3];
};
PickupResult pickup_collect_test(StageWorld& stage, FxVec3& centre, Fx prev_y, Fx radius,
                                 bool collect);
// supply_craft_hit_test: a projectile's segment against the craft (radius 3) and the
// crates (radius 1). Bits 0x80000 craft, 0x100000 crate; *at where it entered
int supply_craft_hit_test(StageWorld& stage, FxVec3 from, FxVec3 to, FxVec3* at);
bool supply_craft_shot_down(StageWorld& stage);
bool supply_craft_pause(StageWorld& stage);
bool supply_craft_resume(StageWorld& stage);
// pickup_find_nearest: an AI's nearest falling or lying crate within range, -1 none
int pickup_find_nearest(const StageWorld& stage, FxVec3 at, int slot, Fx range);
void supply_craft_route_for_ending(StageWorld& stage, int route);
void supply_craft_active_clear(StageWorld& stage);

// the static and shake (hit_static_frames_set, hit_shake_frames_set)
void hit_static_frames_set(StageWorld& stage, int frames);
void hit_shake_frames_set(StageWorld& stage, int frames);
// hit_shake_jitter's draws are held (the stage drew them on the last even step and hasn't
// cleared them): the table never gives 0, so all three are 0 only when clear
inline bool hit_shake_held(const StageWorld& stage) {
    return stage.shake_x != Fx{} || stage.shake_y != Fx{} || stage.shake_tilt != Fx{};
}

void stage_event(StageWorld& stage, StageEventKind kind, FxVec3 at, int a = 0, int b = 0,
                 unsigned flags = 0);
void stage_event_push(StageWorld& stage, const StageEvent& event);
