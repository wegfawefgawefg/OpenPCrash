#include "models.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "gltf_write.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

namespace {

// what the fighter tables say about model assets: which are fighter parts, and which paint set to
// show them with in a viewer (the game picks the paint per build: part_color_assets[leg class])
struct FighterParts {
    std::set<int> parts;
    std::map<int, int> preview_paint; // part asset -> texture set asset
    std::set<int> stages;
};

void add_table(const Disc& disc, std::uint32_t va, int count, std::set<int>& into) {
    for (int i = 0; i < count; ++i) {
        const int id = xbe_i32(disc.xbe, va + i * 4);
        if (id > 0) {
            into.insert(id);
        }
    }
}

FighterParts read_fighter_parts(const Disc& disc) {
    FighterParts out;
    add_table(disc, xbe_at::body_model_assets, 7, out.parts);
    add_table(disc, xbe_at::body_sticker_model_assets, 7, out.parts);
    add_table(disc, xbe_at::leg_model_assets, 21, out.parts);
    add_table(disc, xbe_at::arm_a_model_assets, 34, out.parts);
    add_table(disc, xbe_at::arm_b_model_assets, 34, out.parts);
    add_table(disc, xbe_at::back_a_model_assets, 37, out.parts);
    add_table(disc, xbe_at::back_b_model_assets, 37, out.parts);
    add_table(disc, xbe_at::stage_model_assets, 8, out.stages);
    // a leg's own paint; any other part takes the paint of the first leg from its cabinet
    std::map<int, int> cabinet_paint;
    for (int leg = 0; leg < 21; ++leg) {
        const int model = xbe_i32(disc.xbe, xbe_at::leg_model_assets + leg * 4);
        const std::uint32_t ids = xbe_u32(disc.xbe, xbe_at::part_color_assets + leg * 8);
        const int paint = ids ? xbe_i32(disc.xbe, ids) : -1;
        if (model <= 0 || paint < 0) {
            continue;
        }
        out.preview_paint[model] = paint;
        cabinet_paint.emplace(disc.assets[model].cabinet, paint);
    }
    for (int part : out.parts) {
        if (!out.preview_paint.count(part) && cabinet_paint.count(disc.assets[part].cabinet)) {
            out.preview_paint[part] = cabinet_paint[disc.assets[part].cabinet];
        }
    }
    return out;
}

int texture_set_count(const Disc& disc, int id) {
    const std::vector<std::uint8_t> bytes = read_asset(disc, id);
    if (bytes.size() < 8) {
        return 0;
    }
    return bytes[4] | bytes[5] << 8 | bytes[6] << 16 | bytes[7] << 24;
}

} // namespace

bool extract_models(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const FighterParts fighters = read_fighter_parts(disc);
    int written = 0, failed = 0;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        const AssetRecord& record = disc.assets[id];
        if (record.kind != 0 || record.descriptor.size() != 3 || !asset_wanted(options, id)) {
            continue;
        }
        std::string error;
        const std::vector<std::uint8_t> bytes = read_asset(disc, id);
        const std::vector<DiscObject> objects =
            bytes.empty() ? std::vector<DiscObject>{} : decode_model_set(bytes, error);
        if (objects.empty()) {
            std::fprintf(stderr, "models: asset %d: %s\n", id,
                         bytes.empty() ? "unreadable" : error.c_str());
            ++failed;
            continue;
        }

        // where its sections' textures come from
        GltfModelInfo info;
        info.asset = id;
        info.first_slot = record.descriptor[0];
        int texture_set = record.descriptor[2];
        const bool part = fighters.parts.count(id) > 0;
        if (texture_set < 0 && part && fighters.preview_paint.count(id)) {
            texture_set = fighters.preview_paint.at(id);
            info.textures.part_colour = true;
        }
        const char* category = fighters.stages.count(id) ? "stages" : part ? "mechs" : "misc";
        if (texture_set >= 0) {
            info.textures.dir_uri = "../../textures/" + asset_folder_name(disc, texture_set);
            info.textures.count = texture_set_count(disc, texture_set);
        }

        const std::string file =
            std::string("models/") + category + "/" + asset_folder_name(disc, id) + ".gltf";
        std::error_code made;
        std::filesystem::create_directories(
            std::filesystem::path(options.out_dir + "/" + file).parent_path(), made);
        if (!write_model_gltf(options.out_dir + "/" + file, objects, info, error)) {
            std::fprintf(stderr, "models: %s\n", error.c_str());
            return false;
        }
        // (model-set (asset id) (source ...) (slots first count) (texture-set id) (file ...))
        std::string entry = "(model-set (asset " + sx_int(id) + ") (source " +
                            sx_string(asset_source(disc, id)) + ") (slots " +
                            sx_int(record.descriptor[0]) + " " + sx_int(record.descriptor[1]) +
                            ") (texture-set " + sx_int(record.descriptor[2]) + ")";
        if (info.textures.part_colour) {
            entry += " (preview-paint " + sx_int(texture_set) + ")";
        }
        entry += " (objects " + sx_int(static_cast<long long>(objects.size())) + ") (file " +
                 sx_string(file) + "))";
        manifest.entries.push_back(entry);
        ++written;
    }
    std::printf("models: %d sets written, %d unread\n", written, failed);
    return true;
}
