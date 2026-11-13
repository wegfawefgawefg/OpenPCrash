#pragma once
// What goes in which pack (docs/assets.md, "Packs"), grouped the way the original loads:
//   core       always loaded: the manifest, the tables, the plan, the font, the standard images and
//              the boot models (game_init's resources 0xD5, 0xD6)
//   battle     what battle_task_begin loads for every battle (effects models, texture sets)
//   stage_N    one per stage selector: its model set, the textures it shows, its collision grid
//              (stage_assets_load)
//   body_C leg_C arm_a_C arm_b_C back_a_C back_b_C paint_L
//              a fighter's parts by class, as fighter_assets_load loads them; a leg pack
//              also holds the arm motions for that leg class, a paint pack every colour of one
//              leg class (only the colour used is touched)
//   screen_X   a screen's layout tables and the sprites it draws; ui_S the sprites no screen claims
//   text       event text and scripts
//   cab_X      every other model, motion and texture set, by the disc cabinet it came from
// The plan is cooked into the core pack, so one pack can be re-cooked without re-reading data/'s
// tables.
#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "assets/stage_tables.hpp"

#include <cstdint>
#include <string>
#include <vector>

enum class CookItemKind : std::uint32_t {
    texture,    // path: a PNG; mips made
    sprite,     // path: a 2D PNG, top level only
    model_set,  // id, path: a glTF; its pictures join the pack as textures
    motion_set, // id, path
    collision,  // id, path
    sexp,       // path: kept as text
    manifest,   // core only: the four below come from the plan's own reading of data/
    stage_tables,
    fighter_tables,
    plan,
    pc_art, // path: the sexp written, PC CONFIG's letters scanned from their PNGs (cook_pc_art.hpp)
};

enum CookItemFlags : std::uint32_t {
    item_no_pictures = 1, // fighter parts: painted per build, the glTF's preview paint isn't packed
};

struct CookItem {
    CookItemKind kind = CookItemKind::sexp;
    int id = -1;
    std::string path; // relative to data/
    std::uint32_t flags = 0;
};

struct PackPlan {
    std::string name;
    std::vector<CookItem> items;
};

// the original's format and size of a texture, to pick its BC format
struct TextureHint {
    std::string path;   // relative to data/
    std::string format; // DXT1, DXT5, A8R8G8B8, P8
    int width = 0, height = 0;
};

struct CookPlan {
    std::vector<PackPlan> packs;
    std::vector<TextureHint> hints;        // sorted by path
    std::vector<std::string> core_sources; // what the plan was read from, relative to data/
};

// the plan for data/ and what the core pack holds besides files: the manifest with each asset's
// pack filled in, and the tables the game reads at start
struct CoreTables {
    CookPlan plan;
    AssetManifest manifest;
    StageTables stages;
    FighterTables fighters;
};
bool make_cook_plan(const std::string& data_dir, CoreTables& core, std::string& error);
const PackPlan* plan_pack(const CookPlan& plan, const std::string& name);
const TextureHint* plan_hint(const CookPlan& plan, const std::string& path);
// what a pack was cooked from: its own items, or for the core pack (which holds the plan) the whole
// plan
std::uint64_t pack_plan_hash(const CookPlan& plan, const PackPlan& pack);

// pack names, for the loaders
inline std::string stage_pack_name(int selector) {
    return "stage_" + std::to_string(selector);
}

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, CookItem& item) {
    a(item.kind);
    a(item.id);
    a(item.path);
    a(item.flags);
}
template <class A> void io(A& a, PackPlan& pack) {
    a(pack.name);
    a(pack.items);
}
template <class A> void io(A& a, TextureHint& hint) {
    a(hint.path);
    a(hint.format);
    a(hint.width);
    a(hint.height);
}
template <class A> void io(A& a, CookPlan& plan) {
    a(plan.packs);
    a(plan.hints);
    a(plan.core_sources);
}
