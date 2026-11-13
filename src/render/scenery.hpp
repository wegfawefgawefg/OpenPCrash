#pragma once
// What the first slice shows (docs/plan.md): a stage and one fighter standing on its first entry
// point, loaded from data/ and drawn the way render_passes_run draws a battle frame for a
// camera: the stage's lighting, the passes in order with their queues sorted and culled, the
// shadow volumes, the composite. Presentation only: the fighter is drawn where the world puts it.
#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "assets/stage_tables.hpp"
#include "effects/billboards.hpp"
#include "mech/mech_parts.hpp"
#include "render/scene_draw.hpp"
#include "render/stage_lighting.hpp"
#include "stage/stage_data.hpp"
#include "stage/stage_scene.hpp"
#include "stage/stage_view.hpp"
#include "view/view.hpp"

#include <array>
#include <string>

struct Dance;
struct Effects;
struct HudCockpit;

struct Scenery {
    AssetManifest manifest;
    StageTables stages;
    StageLightingTable lighting;
    FighterTables fighters;
    TextureCache textures;
    TextureSlots slots;
    StageScene stage;
    StageData stage_data; // the stage simulation's tables (the world points at them)
    StageView stage_view; // props, craft, crates, sky craft, moon on screen
    MechParts mech;       // slot 0's
    // the other slots' fighters (--opponents), index slot - 1; a build loaded once per slot
    std::array<MechParts, 5> other_mechs;
    float mech_position[3] = {};
    float mech_heading = 0; // port frame: forward is (sin, 0, cos)
    int bump_light = -1, yurayura = -1, wave02 = -1, muteki = -1;
    int day = 0;            // the game day the lighting cycles by
    bool safe_rect = false; // the 3D in the original's 576x448 safe rectangle
    Effects* effects = nullptr; // the battle's effects (src/effects), drawn when set
    // --dance: slot 0 dancing alone (mech/dance_pose.hpp), no stage, effects or shadow, over a
    // black or white clear
    const Dance* dance = nullptr;
    // draws added after the frame's own (the front end's 3D over a demo battle: render pass 13)
    const FrameDraws* late = nullptr;
    // the viewer's cockpit instruments (pass 11, hud_draw_meters), drawn in a single view
    const HudCockpit* cockpit = nullptr;
    // a frame's lists, cleared and refilled each frame and view (nothing carries over), kept here
    // so their vectors keep their room instead of growing again every frame
    FrameDraws draws;
    std::vector<PassItem> items;
    std::array<std::vector<const PassItem*>, 17> queues;
    std::vector<Billboard> billboards;
};

// the tables from the core pack, the stage and the fighter from theirs
// (extra: build rows after the table's, as load_battle_data's)
bool load_scenery(Scenery& scenery, Renderer& renderer, PackLibrary& packs, int stage, int build,
                  std::string& error, const std::vector<FighterBuild>& extra = {});
// the frame for a camera pose: the fighter as the world has it, or (none in the world) standing on
// the entry point playing its idle clip by ticks (60 Hz ticks); alpha is how far between the last
// two steps (the stage's props, craft and animation)
bool draw_scenery(Scenery& scenery, Renderer& renderer, const CameraPose& camera,
                  const FighterPose* fighter, float ticks, float alpha,
                  const std::string& capture_path);
// the same with every slot's fighter (fighters[6]; absent slots skipped)
bool draw_scenery_fighters(Scenery& scenery, Renderer& renderer, const CameraPose& camera,
                           const FighterPose* fighters, float ticks, float alpha,
                           const std::string& capture_path);
// split screen: each view a camera through its rectangle of the 640x480 screen (the original's
// viewport_layouts, src/view/split_views.hpp), all drawn in one frame
struct SceneryView {
    CameraPose camera;
    float rect[4] = {0, 0, 640, 480};
    // the seat it shows and retail's rectangle for it (where its sprites are placed): the
    // effects' 2D goes into each view, the hit static only into hit_seat's
    int seat = -1;
    float sprite_rect[4] = {32, 16, 576, 448};
};
bool draw_scenery_views(Scenery& scenery, Renderer& renderer, const SceneryView* views, int count,
                        const FighterPose* fighters, float ticks, float alpha,
                        const std::string& capture_path);
// another slot's fighter (1..5) from its build's packs
bool load_scenery_fighter(Scenery& scenery, Renderer& renderer, PackLibrary& packs, int slot,
                          int build, std::string& error);
const MechParts& scenery_mech(const Scenery& scenery, int slot);
