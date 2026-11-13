#pragma once
// The trophies in 3D (decomp result_trophy_draw, record_model_draw; docs/port/
// screens.md "The trophy", the records screen's grids; cameras.md "Menu cameras"): a static model
// by its render model slot (battle_trophy_model_slots, record_trophy_models), its model set found
// through the manifest and loaded the first time, drawn with the result and records camera, eye
// (0, 0, -10) on the origin at 52 degrees, and the menu light (0, 0, 3): the result's as render
// pass 13 (`model_render_submit(.., 0xD)`: over the sheet's 2D of pass 12, under the captions of
// pass 14), the records' as pass 2 (record_model_draw's `model_render_submit(.., 2)`: over the
// backgrounds of pass 0, under the cards and grids of pass 12). What a screen loads is freed when
// it goes.
#include "assets/manifest.hpp"
#include "assets/model_sets.hpp"
#include "render/scene_draw.hpp"
#include "render/stage_lighting.hpp"

#include <map>

struct Ui;

struct TrophyModels {
    bool started = false;
    bool failed = false;
    AssetManifest manifest;
    TextureCache textures;
    TextureSlots slots;
    struct Loaded {
        bool ok = false;
        ModelSet set;
        GpuModelSet gpu;
        int texture_base = 0;
    };
    std::map<int, Loaded> sets; // by model set asset
    StageLighting menu_stage;   // no stage: no lamps, the default point light
    StageLightingTable menu_table;
};

// one model at the original's position (x, y, z) and turns (radians about x, y, z), turned about
// its own y by `yaw` first, into ui.scene as render pass `pass` (13 late, over the 2D, or 2)
void trophy_draw(Ui& ui, TrophyModels& trophies, int model_slot, const float position[3],
                 const float rotation[3], float yaw, int pass);
// the models' GPU memory given back (the screen's destroy)
void trophy_models_release(Ui& ui, TrophyModels& trophies);
