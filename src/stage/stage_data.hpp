#pragma once
// The constant tables the stage simulation and its drawing read: data/tables/stage_props.sexp
// (the stage-props extractor family) and the crate weights of tournaments.sexp. Loaded once;
// the world points at it and never changes it. Positions are in the port frame (z negated).
#include "fx/angle.hpp"
#include "fx/vec.hpp"

#include <array>
#include <string>
#include <vector>

// a StageProp row (decomp include/tables.h)
struct PropPlacement {
    int model = -1;
    int alt_model = -1;
    FxVec3 pos;
    Angle rot[3];   // port frame (-x, -y, z), applied x, y, z
    int cell[3] = {}; // pos / 15 truncated, z negated
    int shape = 0;    // row of the selector's shapes and types
    int gate = -1;    // not -1 on the second half of a gate pair
};

// twelve triangles, vertices as stored (the normal is (v2 - v0) x (v1 - v0) in the port frame)
struct PropShape {
    FxVec3 v[12][3];
};

struct StagePropSet {
    std::vector<PropPlacement> placements;
    std::vector<int> types; // behaviour by shape row (D_0022E700)
    std::vector<PropShape> shapes;
};

// a cubic Bezier segment (RouteSegment)
struct RouteSegment {
    FxVec3 p[4];
};

// a StageRoute row: which route, its last segment, where the craft changes state
struct RouteChoice {
    int route = 0; // into StageData::routes
    int count = 0;
    int ingress = 0;
    int release_start = 0;
    int release_end = 0;
    Fx height;
};

struct StageData {
    int prop_scores[9] = {};
    int prop_assets[8] = {}; // model sets of the props, by selector
    int craft_assets[2] = {}; // the supply craft's set, by night
    int sky_assets[2] = {};   // the sky craft's set, by night
    StagePropSet props[8];

    std::vector<std::vector<RouteSegment>> routes; // count + 1 segments each
    RouteChoice choices[31];
    int route_counts[8] = {};
    Fx ending_speeds[7];
    int sky_route[8] = {};
    int sky_count[8] = {};

    // model slots by night: body, wreck, rotor, the two hatch doors; crates by kind
    int craft_body[2] = {}, craft_wreck[2] = {}, craft_rotor[2] = {}, craft_hatch_a[2] = {},
        craft_hatch_b[2] = {};
    int crate_models[10][2] = {};
    int sky_screen_textures[6] = {};

    // presentation
    float ambience_points[4][3] = {};
    float moon_points[4][3] = {};
    int moon_textures[30] = {};

    // crate kind weights per tournament (tournaments.sexp pickup_weights)
    std::vector<std::vector<Fx>> pickup_weights;
    std::vector<std::array<int, 4>> day_tournaments; // day_schedule's tournament per area slot
};

// the tournament whose crate weights a battle on this selector uses on this day
// (pickup_kinds_draw: the training stage's are tournament 6's)
int stage_crate_tournament(const StageData& data, int selector, int day);

struct Pack;
// from the core pack
bool load_stage_data(StageData& data, const Pack& core, std::string& error);
