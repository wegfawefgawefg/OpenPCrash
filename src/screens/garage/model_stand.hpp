#pragma once
// The garage screens' model stand (ModelViewer, decomp src/od/u_082de0.c model_viewer_*;
// docs/port/screens.md "The model stand"): machines assembled from builds, standing at the mode's
// place for their leg class and turning at the mode's rate, drawn by the renderer's fighter pass
// over the screen's 2D backdrop (the layers under 9 draw before the 3D). Camera eye (0, 2, -12)
// looking at (0, 2, 0), 52 degrees, the mode's light at diffuse 1, ambient 0.25
// (model_viewer_cameras, menu_light_set). An incomplete build is laid out as its parts, a shop list
// shows the part under its cursor, the dealer and Wild Machines' used Scoobees keep a line-up of
// machines that slide in under the cursor and out beside it. Machines load on a thread and swap
// in when done, as retail's (stand_loader.cpp). The places come from the original's tables
// (data/ui/layouts/model_viewer.sexp). PC: the mouse drags the machine round and two bound keys
// turn it (stand_turn.cpp); the pad has no turning in retail and gets none.
#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "mech/mech_parts.hpp"
#include "render/renderer.hpp"
#include "render/scene_draw.hpp"
#include "render/stage_lighting.hpp"
#include "screens/garage/part_tables.hpp"
#include "screens/ui/menu_pointer.hpp"

#include <array>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct Ui;

// the original's place tables (src/data/sv_attach.c), read from the ui data at the start
struct StandTables {
    float places[7][5] = {};    // model_viewer_places: x, y, z, heading (radians), turn (degrees)
    float lights[7][3] = {};    // model_viewer_cameras' light direction
    float spread[2][6][6] = {}; // D_00241BB8 row 0 (view 0), D_002417C8 row 6 (view 6): per part
                                // x, y, z and turns about x, y, z
    std::vector<std::array<float, 6>> part_places;      // D_00242038: a fitted part's place
    std::vector<int> by_body, by_leg, by_arm, by_back;  // D_002426BC .. D_002427C0: place rows
    int class_points[7] = {-1, -1, -1, -1, -1, -1, -1}; // D_00242648: the mode's per-class table
};

// one load at a time: open on the main thread, read on a worker, upload when it's done
struct StandJob {
    std::string key;
    std::unique_ptr<MechParts> mech;
    MechLoad load;
    std::future<bool> read; // last, so it is waited for before the rest goes
};

// a line-up machine (modes 2 and 3): one of retail's six slots, its task hidden until the cursor
// first reaches it
struct StandSlot {
    ShopBuild build;
    bool used = false;
    MechParts* mech = nullptr; // built: loaded and placed
    bool visible = false;      // its task's hide bits (0x24) cleared
    float point[3] = {};
    float heading = 0;
};

struct ModelStand {
    bool started = false;
    bool failed = false; // the tables couldn't load: no 3D on the garage screens
    AssetManifest manifest;
    FighterTables fighters; // the core pack's, the stand's build appended as the last row
    TextureCache textures;
    TextureSlots slots;
    StandTables tables;
    // every machine assembled so far, by its classes and paint (meshes stay on the GPU)
    std::map<std::string, std::unique_ptr<MechParts>> mechs;
    std::set<std::string> broken; // couldn't load: not asked again
    // the loader: the next load asked for (only the latest ask waits), the one reading
    bool asked = false;
    std::string asked_key;
    FighterBuild asked_row;
    bool asked_parts_only = false;
    std::unique_ptr<StandJob> job;

    int mode = 0; // ModelViewer.mode: 0 garage, 1 stock, 2 dealer, 3 Wild Machines, 4 Plus Tech,
                  // 5 spec, 6 the hangar
    // the one machine (garage_stand_load_loadout): the build asked for, swapped in when loaded
    bool want = false;
    ShopBuild wanted;
    MechParts* shown = nullptr;
    // an incomplete build is shown laid out as the parts it has (scoobee_parts_draw_spread)
    bool shown_spread = false;
    bool shown_present[6] = {}; // body, legs, arm A, arm B, back A, back B
    // a shop list's part under the cursor (model_viewer_fit_part): asked for, then its machine,
    // its place row (D_00242038) and the grow in (model_viewer_draw_grow: 0..15 updates)
    int want_kind = -1, want_id = -1;
    MechParts* part = nullptr;
    int part_place = -1;
    int grow = 0;
    float heading = 0.4f; // the actor's; mode 0 keeps it across swaps (ModelViewer.heading)
    float part_heading = 0.4f;
    float turn = 0.5f; // degrees an update
    float point[3] = {3.2f, 0.0f, -1.8f};
    int ticks = 0; // the idle clip's clock
    // the line-up (dealer_model_slot_pick, wild_machines_used_model_slot_pick): rows by slot,
    // the one under the cursor shown (viewer.shown), -1 none
    StandSlot line[6];
    int line_shown = -1;
    // PC: the mouse and the keys turning the machine (stand_turn.cpp); the area the mouse may
    // grab, in 640x480 screen pixels (the screen sets it clear of its own hit boxes)
    PointerRect area = {340, 96, 640, 330};
    bool dragging = false;
    float drag_x = 0;
    float user_turn = 0;    // radians this update
    bool user_held = false; // turning by hand: the turn waits
    // the menu has no stage: no lamps, the default point light
    StageLighting menu_stage;
    StageLightingTable menu_table;
};

// one machine on the stand: where and which way it faces (the original's frame)
struct StandPose {
    const MechParts* mech = nullptr;
    float point[3] = {};
    float heading = 0;
    // the parts laid out instead: the spread's view (0 the garage's, 6 the hangar's), the parts
    // the build has; or the one part under a shop's cursor at its place row, grown by scale
    int spread = -1;
    bool present[6] = {};
    int part_place = -1;
    float scale = 1;
};

// model_viewer_set / model_viewer_start: the tables once, the mode's place (6 the hangar's row)
void stand_start(Ui& ui, ModelStand& stand, int mode);
// a build's machine, assembled once and kept: whole with body and legs (and the Chip when
// need_chip), else only the parts it has (complete says which); nullptr while it loads (ask again
// next update; loading says so) or for no parts at all
MechParts* stand_load(Ui& ui, ModelStand& stand, const ShopBuild& build, bool need_chip,
                      bool* complete = nullptr, bool* loading = nullptr);
// the loader once an update: a finished read uploaded, the latest ask started
void stand_service(Ui& ui, ModelStand& stand);
// model_viewer_stop's thread stop: waits for the load in flight and finishes it (every screen's
// destroy, before anything else may close the packs it reads)
void stand_settle(Ui& ui, ModelStand& stand);
// nothing reading or asked
bool stand_idle(const ModelStand& stand);
// which of body, legs, arm A, arm B, back A, back B a build has
void stand_parts_present(const ShopBuild& build, bool out[6]);
// model_viewer_fit_part: a shop list's part of kind 0..3 (body, legs, arms, backs) and row id onto
// the stand, growing in once loaded; any other kind clears it
void stand_fit_part(Ui& ui, ModelStand& stand, int kind, int id);
// garage_stand_load_loadout: show this build once it has loaded (a complete one; modes 0, 1, 2
// and 4 need the Chip too), or its parts laid out
void stand_show(Ui& ui, ModelStand& stand, const ShopBuild& build);
void stand_hide(ModelStand& stand);
// model_viewer_offset: where a machine of this leg class (body class without legs) stands in the
// stand's mode; shifted: 8 to the side in modes 2 and 3 (model_viewer_offset_shifted)
void stand_offset(const ModelStand& stand, const MechParts& mech, bool shifted, float out[3]);
// the line-up's machines, rows 0..count-1 (the dealer's three, the day's used Scoobees)
void stand_line_set(ModelStand& stand, const ShopBuild* builds, int count);
// each input step: the cursor's row shown once built, else none, and the next row to load
// (_shown_machine_update, _model_slot_pick); -1 for no machine row
void stand_line_follow(Ui& ui, ModelStand& stand, int row);
// every line-up machine hidden (leaving the list)
void stand_line_hide(ModelStand& stand);
// model_viewer_actor_update: one update's turn, slide and clock, and the swaps loaded since
void stand_update(Ui& ui, ModelStand& stand);
// the stand's draws for this frame into ui's scene
void stand_draw(Ui& ui, ModelStand& stand);
// several machines at once (the hangar's row), under the mode's camera and light
void stand_draw_poses(Ui& ui, ModelStand& stand, const StandPose* poses, int count);
// stand_parts.cpp: a pose's loose parts (the spread, the fitted part) as pass 2 items
void stand_parts_items(const StandTables& tables, const StandPose& pose,
                       std::vector<PassItem>& items);
// a fitted part's place row for its kind and machine (D_002426BC .. D_002427C0), -1 none
int stand_part_place(const StandTables& tables, int kind, const MechParts& mech);

// stand_turn.cpp (PC): the mouse's drag inside stand.area and the turn keys into
// stand.user_turn / user_held, once an update before stand_update; the hangar reads them too
void stand_turn_input(Ui& ui, ModelStand& stand);
// the area the mouse may grab on this screen
inline void stand_area_set(ModelStand& stand, PointerRect area) {
    stand.area = area;
}
