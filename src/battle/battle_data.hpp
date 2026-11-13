#pragma once
// What a battle reads and never changes: the part records, the stage's collision grid and entry
// points, per leg class the motion timing, the root-motion curves, the radius and the attachment
// vectors, and what each build is posed from (mech/rig_data.hpp). Loaded once
// from the cooked packs (the core pack's tables, the stage pack's grid, the leg packs' motions)
// into Q16.16; the world holds it by a shared pointer to const, so copying a World (for
// interpolation, later rollback) copies only the pointer.
#include "ai/ai_data.hpp"
#include "battle/part_records.hpp"
#include "battle/scene_curves.hpp"
#include "battle/battle_ending.hpp"
#include "battle/stage_collision.hpp"
#include "weapons/combat_tables.hpp"
#include "fx/angle.hpp"
#include "fx/vec.hpp"
#include "mech/rig_data.hpp"

#include <cstdint>
#include <string>
#include <vector>

inline constexpr int leg_class_count = 21;
inline constexpr int motion_slot_count = 37;

// sv_motion_timing_records [leg class][slot] (MotionTiming, 16 bytes): the clip's root track and
// the three phases the state handlers compare with, ticks
struct MotionTiming {
    int root_track = 0;
    Fx loop_start, loop_end, end;
};

// the root track's x and z translation curves of a slot's clip (motion_root_sample reads
// the raw curves, so the port file's x and z, already mirrored, are the port's local x and z)
struct RootCurves {
    FxCurve x, z;
    bool present = false;
};

struct LegClassData {
    MotionTiming timing[motion_slot_count];
    RootCurves root[motion_slot_count];
    Fx radius;          // D_0022F338 column 1, clamped 0..2 by actor_init
    Fx walk_root_scale; // sv_geometry_response_table: 0 none, else the clip moves the machine
    Fx run_root_scale;
    bool motion_loaded = false; // the root curves are loaded for the leg classes the builds use
    LegAttachment attachments[leg_attachment_count]; // sv_leg_attachment_table
};

struct EntryPoint {
    FxVec3 position;
    Angle heading; // port frame: forward is (sin, 0, cos)
};

// camera_rails for the battle's day mode (view 5): the eye points (port frame) and each
// stage zone's [first, end) of them (camera_rail_zone_ranges); none when the table
// isn't in the data
struct CameraRails {
    int selector = 0;
    std::vector<FxVec3> points;
    int zones[5][2] = {};
};

struct BattleData {
    PartRecords parts;
    CombatTables combat; // weapons, chips, options, durability, the RNG table (src/weapons)
    AiData ai; // the AI's tables and the stage's route graph (src/ai/ai_data.hpp)
    StageCollision stage;
    std::vector<EntryPoint> entries;
    LegClassData legs[leg_class_count];
    // the pose's: every SV graph node (indices as in the fighter tables) and a rig per build
    std::vector<RigNode> rig_nodes;
    std::vector<FighterRig> rigs;
    // the scenes' lengths and the entrances' walks (scene_camera_rows 0x258E68)
    SceneWalks scene_walks;
    // the in-engine ending's shots (scene_ending_shots)
    EndingShots ending;
    // the Area Rankers' defeat lines by area, in ticks: how long scene_boss_defeat_update waits
    // for its script task (measured from the script by battle_run_start; 0 none)
    int ranker_line_ticks[3] = {};
    CameraRails rails;
    // the lesson table entries (their addresses, sorted) that point at a script: tutorial_say
    // starts nothing for the others (set by battle_run_start; empty, every entry is taken to
    // have one)
    std::vector<int> scene_text_scripts;
};

// a part record row as the fighter assets name it (the garage's machine)
struct FighterBuild;
FighterBuild fighter_build_of(const BuildRecord& row);

struct PackLibrary;
// stage: the selector 0..7 (stage * 2 + night); builds: the rows of rumble_scoobee_loadouts that
// will fight, whose leg classes' motions are loaded
// will fight, whose leg classes' motions are loaded; extra: build rows appended after the table's
// (a garage's machine, row table size + k), which `builds` may name
bool load_battle_data(BattleData& data, PackLibrary& packs, int stage,
                      const std::vector<int>& builds, std::string& error,
                      const std::vector<BuildRecord>& extra = {});
