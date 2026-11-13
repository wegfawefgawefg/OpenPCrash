#pragma once
// The cook's readers of data/ (the extractor's formats, docs/assets.md). Only the cook reads
// data/; the game reads cooked/ packs.
#include "assets/collision_grid.hpp"
#include "assets/fighter_tables.hpp"
#include "assets/model_sets.hpp"
#include "assets/motion_sets.hpp"
#include "assets/stage_tables.hpp"
#include "cook/sexp_read.hpp"

#include <string>
#include <utility>
#include <vector>

// a model set's glTF (relative to data_dir), in the extractor's layout: object roots in scene
// order, node and material extras; pictures come back relative to data/
bool read_model_set(const std::string& data_dir, const std::string& path, ModelSet& out,
                    std::string& error);
bool read_motion_set(const std::string& path, MotionSet& out, std::string& error);
// one "(curve mode (times ...) (values ...))" form; anything else leaves the curve absent
void read_motion_curve(gsexp::Node node, MotionCurve& curve);
bool read_collision_grid(const std::string& path, CollisionGrid& out, std::string& error);
bool read_stage_tables(StageTables& tables, const std::string& data_dir, std::string& error);
bool read_fighter_tables(FighterTables& tables, const std::string& data_dir, std::string& error);

// data/manifest.sexp, as far as the plan needs it
struct ManifestTexture {
    std::string file; // relative to data/
    std::string format;
    int width = 0, height = 0;
};
struct ManifestAsset {
    int id = -1;
    std::string kind;    // model-set, texture-set, motion-set, collision-grid
    std::string file;    // or the folder of a texture set
    std::string cabinet; // the disc file it came from, before any +offset
    int first_slot = -1, slot_count = 0, texture_set = -1;
    std::vector<ManifestTexture> textures;
};
struct ManifestRead {
    std::vector<ManifestAsset> assets;
    std::vector<std::string> layouts;   // ui-layouts files
    std::vector<std::string> timelines; // ui-timeline files
    std::vector<std::string> tables;    // table files
    std::vector<std::string> texts;     // text-lines, text-strings and scripts files
    std::string font, font_map;
    // nav-graph files (the AI's ENPT route graphs) and the stage selectors that load each
    std::vector<std::pair<std::string, std::vector<int>>> navs;
};
bool read_manifest(const std::string& data_dir, ManifestRead& out, std::string& error);

// data/ui/images.sexp: each sprite's file and the screens that draw it
struct SpriteRead {
    std::string file; // relative to data/ui/images
    std::vector<std::string> screens;
    bool standard = false; // a systemdata image (standard-image), not a registry sprite
};
bool read_sprites(const std::string& data_dir, std::vector<SpriteRead>& out, std::string& error);
