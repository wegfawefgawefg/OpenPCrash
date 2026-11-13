#pragma once
// The particle effects' data (data/tables/effects.sexp, the extractor's effects family): the
// particle records with their curves, the effect lists, the effect sets callers play, the sprite
// effect kinds and the lens flare's pieces (decomp docs/port/effects.md, include/tables.h
// ParticleRecord). In the original's frame, as the effects code runs in it.
#include <cstdint>
#include <string>
#include <vector>

struct Pack;

// a curve's key (FloatKey, IntKey): time, value; a list ends with a key of time -1
struct FloatKey {
    float time = -1, value = 0;
};
struct IntKey {
    float time = -1;
    int value = 0;
};

// ParticleRecord (0x150), by its fields; an empty curve is the record's null pointer
struct ParticleRecord {
    float forces[6] = {}; // [0], [2] force b's gain and jitter; [3], [5] force a's (and the drag)
    int draw_flag = 0;    // nonzero: additive
    bool gravity_off = false;
    float gravity[4] = {}; // x, y, z rotations of (0, -1, 0), then the strength a second squared
    bool force_a_off = false;
    float force_a = 0;
    float force_a_rot[3] = {}; // degrees
    bool force_b_off = false;
    float force_b = 0;
    bool force_b_radial = false;
    float force_b_range = 0;
    float force_b_rot[3] = {}; // degrees
    float offset[3] = {};
    int color_time = 0, alpha_time = 0; // curve_time kinds
    float life = 0, life_jitter = 0;    // seconds
    float mass = 0;
    int mass_time = 0;
    float scale = 0;
    int scale_time = 0;
    float scale_jitter = 0;
    float speed_min = 0; // speed_limits[0], a second
    int speed_min_time = 0;
    float speed_max = 0; // speed_limits[2]
    int speed_max_time = 0;
    float color[4] = {}; // r, g, b, a
    float hsv_spread[3] = {};
    float alpha_bias = 0;
    int shape = 0, shape_kind = 0, shape_axis = 0; // particle_shape_table column, row
    // [0..2] position, [3..5] rotation, [6..8] spawn scale, [9] rate a second, [10] its jitter
    float emitter_setup[11] = {};
    float spread_deg = 0, spread_jitter = 0;
    float speed = 0, speed_jitter = 0; // a second
    float burst_radius = 0;
    int texture = 0; // atlas cell
    int frame_count = 0;
    // colour bytes 0..3 of the packed ARGB (color_r, color_g, color_b, alpha: byte 0 shows as blue)
    std::vector<IntKey> color_curves[4];
    std::vector<FloatKey> scale_curve, force_b_curve, force_a_curve, rate_curve, mass_curve,
        speed_curve, speed_min_curve, speed_max_curve;
};

// EffectEmitter: a list row
struct EmitterRow {
    std::uint32_t flags = 0;
    int record = -1;
};

struct EffectList {
    std::string name;
    std::vector<EmitterRow> rows;
};

// effect_sets: effect lists and sprite effect kinds
struct EffectSet {
    std::vector<int> effects;
    std::vector<int> sprites;
};

// DebrisKind (sprite_fx_kinds)
struct SpriteKind {
    int model = -1; // the first texture slot; the frame is added
    int frames = 1;
    int step = 1;
    bool scale_from_kind = false;
    bool no_billboard = false;
    std::uint32_t flags = 0;
};

// FlareElement
struct FlarePiece {
    int cell = 0;
    float along = 0, scale = 0;
    std::uint32_t color = 0;
};

// ImpactFx (impact_fx.rows): what a shot leaves where it hits
struct ImpactRow {
    float scale = 1;
    int set = -1;
    bool mark = false;
};

// shot_model_draw's row by WeaponRecord.shot_model
struct ShotLook {
    bool billboard = false;
    int model = -1, quad = -1; // a model slot, else a quad's base slot (-1 none)
    bool flame = false;        // the flame model 0x3F3 and a trail
    float scale = 1;
    float flame_z = 0;
    float flame_scale[3] = {1, 1, 1};
};

struct EffectData {
    std::vector<ParticleRecord> records;
    std::vector<EffectList> lists;
    std::vector<EffectSet> sets;
    std::vector<SpriteKind> sprite_kinds;
    float flare_cells[12][4] = {};
    std::vector<FlarePiece> flare_pieces;
    std::vector<ImpactRow> impacts;
    int foot_landing[15][3] = {}; // foot_landing_sets: by motion state and leg group, -1 none
    std::vector<ShotLook> shot_looks;
    float arc_delays[4][4] = {}; // arc_restart_delay_max [side][arc], updates
    int lamp_glow_models[7][2] = {}; // the lamps' glow model by body class and column
    int lamp_glow_columns[4] = {};   // each lamp's column
};

bool load_effect_data(const Pack& core, EffectData& data, std::string& error);
