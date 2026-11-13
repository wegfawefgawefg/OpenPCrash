#pragma once
// The stage's own animation (stage_render_lods, stage_object_animate): the sky
// turning, scrolling textures, the fading red lamps, the big screens and the sea; and the zone
// functions that pick which objects draw. Presentation: stepped once per world step with the
// 60 Hz values (docs/sixty.md, "The stage's own animation") and interpolated where it turns.
#include "assets/model_sets.hpp"
#include "render/renderer.hpp"

#include <cstdint>
#include <vector>

// an object whose vertices or section states the animation changes: its own copy of the model
// object and, for kinds 3, 4 and 7, its own mesh rewritten as the original rewrites the buffers
struct StageAnimatedObject {
    int object = -1;           // in the stage's model set
    int kind = 0;              // StageObject.animate
    int mesh = -1;             // its own mesh (kinds 3, 4, 7), else the set's
    std::vector<MeshVertex> base; // the vertices as loaded
    ModelObject drawn;         // sections with the kind's states
    std::uint32_t mesh_flags = 0;
};

struct StageAnimation {
    // 0x96CE40: the sky's turn, +0.0005 an update -> 0.00025 a step (R5), wrapped at 2 pi
    float sky_turn = 0, sky_turn_previous = 0, sky_turn_drawn = 0;
    // 0x96CE48: the lamps' alpha, -0.02 an update -> 0.01 a step, back to 1 below 0
    float lamp = 1;
    // 0x96CE50, 0x96CE4C: the v scroll of kinds 3 and 4, +0.0005 and +0.001 an update -> half
    float scroll_slow = 0, scroll_fast = 0;
    bool dirty = true; // meshes to rewrite before the next draw
    std::vector<StageAnimatedObject> objects;
};

// stage_zone_functions: the zone of a point (port frame) for the stage selector; 0 for Shinjuku
// and the training stage, shibuya_zone_at and aqualine_zone_at for the others
int stage_zone_at(int selector, const float at[3]);

struct StageInfo;
struct ModelSet;
struct GpuModelSet;
void stage_animation_load(StageAnimation& animation, const StageInfo& info, const ModelSet& set,
                          const GpuModelSet& gpu, Renderer& renderer);
void stage_animation_step(StageAnimation& animation);
// before drawing: the meshes rewritten if a step changed them, the sky turn interpolated
void stage_animation_frame(StageAnimation& animation, Renderer& renderer, float alpha);
const StageAnimatedObject* stage_animated(const StageAnimation& animation, int object);
