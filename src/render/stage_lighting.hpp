#pragma once
// A stage's light, fog, tint, gamma, shadow colour, depth of field and lamps
// (data/tables/stage_lighting.sexp, the extractor's stages family), and what
// battle_lighting_enter and battle_lighting_update make of them for a day.
#include "render/d3dx_math.hpp"
#include "render/renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct Pack;

// BumpLight: a point light (point_light_add), or the default each row is reset to
struct PointLight {
    float position[3] = {0, -1000, 0};
    std::uint32_t colour = 0;
    float range = 0.01f;
};

// LampColors with its position: a fixed spot of the stage
struct StageLamp {
    float position[3] = {};
    float direction[3] = {}; // its length is the lamp's range
    float colour[3] = {};
};

struct StageLighting {
    int fog_kind = 0; // the record's: 0 none, 1 linear, 2 exp, 3 exp2
    std::uint32_t fog_colour = 0;
    float fog_density = 0, fog_start = 0, fog_end = 1;
    float light_direction[3] = {0, -1, 0}; // the way the light travels, port frame
    std::uint32_t light_colour = 0xFFFFFF;
    float diffuse_scale = 1, ambient_scale = 0.25f;
    int tint[4] = {255, 255, 255, 255}; // a r g b
    int gamma_low[3] = {0, 0, 0}, gamma_high[3] = {255, 255, 255};
    std::uint32_t shadow_colour = 0;
    float dof_level = 80;
    bool heat_wave = false;
    int lamp_count = 0;
    int lamp_ranges[5][2] = {};
    std::vector<StageLamp> lamps;
};

struct StageLightingTable {
    PointLight point_default;
    float height_scale = 100; // D_00280604
    float lamp_default[3] = {0, -1000, 0};
    StageLighting stages[8];
};

// from the core pack (a table the cook keeps as text)
bool load_stage_lighting(const Pack& core, StageLightingTable& table, std::string& error);

// what the draws read: light 0 (render_lights[0]) and the stage's point lights and lamps
struct SceneLights {
    Vec3f direction{0, -1, 0}; // D3DLIGHT8.Direction, normalized
    float diffuse[4] = {1, 1, 1, 0};
    float ambient[4] = {0.25f, 0.25f, 0.25f, 1};
    float specular[4] = {1, 1, 1, 1};
    PointLight points[6]; // point_lights_reset every battle update: six defaults until effects add
    const StageLighting* stage = nullptr;
    const StageLightingTable* table = nullptr;
    int selector = 0; // the stage selector (rumble_selected): Shibuya's lamps go by zone
};

// the battle's lighting for a stage selector on a game day: light 0, and the frame's fog, tint,
// gamma ramp, shadow colour and depth of field
void battle_lighting(const StageLightingTable& table, int selector, int day, SceneLights& lights,
                     FrameSetup& setup);
