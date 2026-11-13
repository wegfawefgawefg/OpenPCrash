#pragma once
// The stage table (data/tables/stage_scene.sexp, the extractor's stages family, cooked into the
// core pack): per stage selector its model set, entry points and the objects stage_render_lods
// draws.
#include <string>
#include <vector>

struct StageObject {
    int model = -1;  // model slot
    int layer = 2;   // render pass (docs/port/render.md, "Passes")
    int submit = 1;  // model_render_submit (1) or the no-fog variant (0)
    int animate = 0; // stage_object_animate kind
    int lods[3] = {-1, -1, -1};
    int zones[5] = {}; // visible in each zone of the stage's zone function
};

struct StageEntryPoint {
    float position[3] = {};
    float yaw = 0;
};

struct StageInfo {
    std::string name;
    int model_set = -1;
    int collision = -1;
    std::vector<StageEntryPoint> entry_points;
    std::vector<StageObject> objects;
};

struct StageTables {
    float lod_mid = 120;
    float lod_far = 200;
    StageInfo stages[8];
};

struct Pack;
// from the core pack
bool load_stage_tables(const Pack& core, StageTables& tables, std::string& error);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, StageInfo& s) {
    a(s.name);
    a(s.model_set);
    a(s.collision);
    a(s.entry_points);
    a(s.objects);
}
template <class A> void io(A& a, StageTables& t) {
    a(t.lod_mid);
    a(t.lod_far);
    for (StageInfo& stage : t.stages) {
        io(a, stage);
    }
}
