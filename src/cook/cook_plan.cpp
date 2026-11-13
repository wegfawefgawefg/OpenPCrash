#include "cook/cook_plan.hpp"

#include "cook/cook_pc_art.hpp"
#include "cook/cook_read.hpp"
#include "cook/pack_format.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace {

// battle_task_begin: resource_load_type0 of 0xC2, 0xD8, 0xB8, 0xB9 and texture_set_load of
// 0x615, 0x635 (0xD5 is a boot resource, in core)
constexpr int battle_models[] = {0xC2, 0xD8, 0xB8, 0xB9};
constexpr int battle_texture_sets[] = {0x615, 0x635};
// game_init's resources 0xD6 and 0xD5
constexpr int boot_models[] = {0xD5, 0xD6};

struct Planner {
    const ManifestRead& read;
    CookPlan& plan;
    AssetManifest& manifest;
    std::map<int, const ManifestAsset*> by_id;
    std::set<int> claimed;

    PackPlan& pack(const std::string& name) {
        for (PackPlan& pack : plan.packs) {
            if (pack.name == name) {
                return pack;
            }
        }
        plan.packs.push_back(PackPlan{name, {}});
        return plan.packs.back();
    }

    // the asset into a pack (the first pack to claim an asset is where the manifest points)
    void add(const std::string& name, int id, std::uint32_t flags = 0) {
        const auto found = by_id.find(id);
        if (id < 0 || found == by_id.end()) {
            return;
        }
        const ManifestAsset& asset = *found->second;
        PackPlan& target = pack(name);
        if (asset.kind == "texture-set") {
            for (const ManifestTexture& texture : asset.textures) {
                target.items.push_back({CookItemKind::texture, id, texture.file, 0});
            }
        } else {
            const CookItemKind kind = asset.kind == "model-set"    ? CookItemKind::model_set
                                      : asset.kind == "motion-set" ? CookItemKind::motion_set
                                                                   : CookItemKind::collision;
            target.items.push_back({kind, id, asset.file, flags});
        }
        if (claimed.insert(id).second) {
            manifest.assets[id].pack = name;
        }
        // a model set brings its own texture set (texture_set_load beside resource_load_type0),
        // pictures its nodes don't name included
        if (asset.kind == "model-set" && !(flags & item_no_pictures)) {
            add(name, asset.texture_set);
        }
    }
};

int at(const std::vector<int>& list, std::size_t index) {
    return index < list.size() ? list[index] : -1;
}

} // namespace

bool make_cook_plan(const std::string& data_dir, CoreTables& tables, std::string& error) {
    tables = CoreTables{};
    CookPlan& plan = tables.plan;
    AssetManifest& manifest = tables.manifest;
    const StageTables& stages = tables.stages;
    const FighterTables& fighters = tables.fighters;
    ManifestRead read;
    std::vector<SpriteRead> sprites;
    if (!read_manifest(data_dir, read, error) ||
        !read_stage_tables(tables.stages, data_dir, error) ||
        !read_fighter_tables(tables.fighters, data_dir, error) ||
        !read_sprites(data_dir, sprites, error)) {
        return false;
    }
    plan.core_sources = {"manifest.sexp", "tables/stage_scene.sexp", "tables/fighter_assets.sexp",
                         "tables/sv_graphs.sexp", "ui/images.sexp"};
    Planner planner{read, plan, manifest, {}, {}};
    for (const ManifestAsset& asset : read.assets) {
        planner.by_id[asset.id] = &asset;
        ManifestEntry& entry = manifest.assets[asset.id];
        entry.kind = asset.kind;
        entry.file = asset.file;
        entry.first_slot = asset.first_slot;
        entry.slot_count = asset.slot_count;
        entry.texture_set = asset.texture_set;
        for (const ManifestTexture& texture : asset.textures) {
            plan.hints.push_back({texture.file, texture.format, texture.width, texture.height});
        }
    }
    std::sort(plan.hints.begin(), plan.hints.end(),
              [](const TextureHint& a, const TextureHint& b) { return a.path < b.path; });

    // core: the computed tables, every table and layout-free text file, the font, the standard
    // images and the boot models
    PackPlan& core = planner.pack("core");
    for (CookItemKind kind : {CookItemKind::manifest, CookItemKind::stage_tables,
                              CookItemKind::fighter_tables, CookItemKind::plan}) {
        core.items.push_back({kind, -1, "", 0});
    }
    for (const std::string& file : read.tables) {
        core.items.push_back({CookItemKind::sexp, -1, file, 0});
    }
    core.items.push_back({CookItemKind::sexp, -1, "ui/images.sexp", 0});
    core.items.push_back({CookItemKind::pc_art, -1, pc_art_path, 0});
    for (const std::string& file : read.timelines) {
        core.items.push_back({CookItemKind::sexp, -1, file, 0});
    }
    if (!read.font.empty()) {
        core.items.push_back({CookItemKind::sprite, -1, read.font, 0});
        core.items.push_back({CookItemKind::sexp, -1, read.font_map, 0});
    }
    for (const SpriteRead& sprite : sprites) {
        if (sprite.standard) {
            core.items.push_back({CookItemKind::texture, -1, "ui/images/" + sprite.file, 0});
        }
    }
    for (int id : boot_models) {
        planner.add("core", id);
    }

    // battle, then the stages
    for (int id : battle_models) {
        planner.add("battle", id);
    }
    for (int id : battle_texture_sets) {
        planner.add("battle", id);
    }
    // the pictures the renderer loads by name (systemdata: the bump lookup, the water's waves,
    // the shield bubble's rainbow)
    for (const char* file : {"bump_light", "yurayuraTest", "wave02", "muteki01"}) {
        planner.pack("battle").items.push_back(
            {CookItemKind::texture, -1, std::string("textures/systemdata/") + file + ".png", 0});
    }
    for (int selector = 0; selector < 8; ++selector) {
        planner.add(stage_pack_name(selector), stages.stages[selector].model_set);
        planner.add(stage_pack_name(selector), stages.stages[selector].collision);
    }
    // the AI's route graph with each stage that loads it (ai_nav_resource_select)
    for (const auto& [file, selectors] : read.navs) {
        for (int selector : selectors) {
            if (selector >= 0 && selector < 8) {
                planner.pack(stage_pack_name(selector))
                    .items.push_back({CookItemKind::sexp, -1, file, 0});
            }
        }
    }

    // fighter parts by class
    const auto parts = [&](const char* prefix, const std::vector<int>& models) {
        for (std::size_t c = 0; c < models.size(); ++c) {
            planner.add(prefix + std::to_string(c), models[c], item_no_pictures);
        }
    };
    parts("body_", fighters.body_models);
    parts("leg_", fighters.leg_models);
    parts("arm_a_", fighters.arm_a_models);
    parts("arm_b_", fighters.arm_b_models);
    parts("back_a_", fighters.back_a_models);
    parts("back_b_", fighters.back_b_models);
    for (std::size_t c = 0; c < fighters.body_models.size(); ++c) {
        planner.add("body_" + std::to_string(c), at(fighters.body_sticker_models, c),
                    item_no_pictures);
        planner.add("body_" + std::to_string(c), at(fighters.body_motions, c));
    }
    for (std::size_t c = 0; c < fighters.leg_models.size(); ++c) {
        const std::string name = "leg_" + std::to_string(c);
        planner.add(name, at(fighters.leg_motions, c));
        // the arm motions are picked by leg class and the arm's motion class (0 or 1)
        for (std::size_t m = 0; m < 2; ++m) {
            planner.add(name, at(fighters.arm_a_motions, c * 2 + m));
            planner.add(name, at(fighters.arm_b_motions, c * 2 + m));
        }
    }
    for (std::size_t c = 0; c < fighters.part_paints.size(); ++c) {
        for (int id : fighters.part_paints[c]) {
            planner.add("paint_" + std::to_string(c), id);
        }
    }

    // screens: the layout tables and the sprites each one draws
    std::set<std::string> claimed_sprites;
    for (const std::string& file : read.layouts) {
        const std::size_t slash = file.find_last_of('/');
        const std::string screen = file.substr(slash + 1, file.find_last_of('.') - slash - 1);
        planner.pack("screen_" + screen).items.push_back({CookItemKind::sexp, -1, file, 0});
    }
    for (const SpriteRead& sprite : sprites) {
        for (const std::string& screen : sprite.screens) {
            planner.pack("screen_" + screen)
                .items.push_back({CookItemKind::sprite, -1, "ui/images/" + sprite.file, 0});
            claimed_sprites.insert(sprite.file);
        }
    }
    for (const SpriteRead& sprite : sprites) {
        if (!sprite.standard && !claimed_sprites.count(sprite.file)) {
            const std::string set = sprite.file.substr(0, sprite.file.find('/'));
            planner.pack("ui_" + set)
                .items.push_back({CookItemKind::sprite, -1, "ui/images/" + sprite.file, 0});
        }
    }
    for (const std::string& file : read.texts) {
        planner.pack("text").items.push_back({CookItemKind::sexp, -1, file, 0});
    }

    // everything else by cabinet: model sets first, so a texture set rides with its model set
    for (const char* kind : {"model-set", "motion-set", "collision-grid", "texture-set"}) {
        for (const ManifestAsset& asset : read.assets) {
            if (asset.kind == kind && !planner.claimed.count(asset.id)) {
                planner.add("cab_" + asset.cabinet, asset.id);
            }
        }
    }

    // one copy of each item per pack
    for (PackPlan& pack : plan.packs) {
        std::set<std::pair<CookItemKind, std::string>> seen;
        std::vector<CookItem> items;
        for (const CookItem& item : pack.items) {
            if (item.path.empty() || seen.insert({item.kind, item.path}).second) {
                items.push_back(item);
            }
        }
        pack.items = std::move(items);
    }
    return true;
}

const PackPlan* plan_pack(const CookPlan& plan, const std::string& name) {
    for (const PackPlan& pack : plan.packs) {
        if (pack.name == name) {
            return &pack;
        }
    }
    return nullptr;
}

const TextureHint* plan_hint(const CookPlan& plan, const std::string& path) {
    const auto found = std::lower_bound(
        plan.hints.begin(), plan.hints.end(), path,
        [](const TextureHint& hint, const std::string& key) { return hint.path < key; });
    return found != plan.hints.end() && found->path == path ? &*found : nullptr;
}

std::uint64_t pack_plan_hash(const CookPlan& plan, const PackPlan& pack) {
    BlobWriter writer;
    if (pack.name == "core") {
        writer(plan);
    } else {
        writer(pack);
    }
    return hash_bytes(writer.bytes.data(), writer.bytes.size());
}
