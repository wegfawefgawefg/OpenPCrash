#pragma once
// The cockpit instruments a frame draws (hud_draw_meters, decomp docs/port/hud.md "The
// cockpit instruments"): the HUD decides which of models 0x4DD..0x4F1 show, where, in what colour
// and with which gauge, and the effects queue them in pass 11 (src/effects/cockpit_draw.cpp),
// where they draw under an identity view, z off and unlit, after the composite.
#include "render/d3dx_math.hpp"

#include <cstdint>
#include <vector>

inline constexpr int cockpit_first_model = 0x4dd;
inline constexpr int cockpit_model_count = 21;   // 0x4DD..0x4F1
inline constexpr int cockpit_glow_model = 0x4dd; // the cloak glow, a unit ahead (hud_meters_build)

// one submitted instrument: its model slot, its matrix under the identity view (the original's
// frame), its texture factor (ModelRoot.factor with flag 0x20) and its second node's scale.x
// (the gauges; negative leaves the node as loaded)
struct CockpitModel {
    int slot = 0;
    Mat4 world;
    std::uint32_t factor = 0xFFFFFFFF;
    float gauge = -1;
};

// one in-world readout quad (HudElem::draw_either in the world, uv_override_submit): a sprite's
// own texture on the unit quad under its matrix (the original's frame), tinted by its colour
struct CockpitQuad {
    Mat4 world;
    int texture = -1;
    std::uint32_t color = 0xFFFFFFFF;
};

struct HudCockpit {
    bool shown = false;
    std::vector<CockpitModel> models; // in submit order (pass 11 isn't sorted)
    std::vector<CockpitQuad> quads;   // the weapon readout's panels, after the models
    // cockpit_uv_set's rows by model - 0x4DD: the texture offsets (texels / 512) at draw time
    float uv[cockpit_model_count][2] = {};
};
