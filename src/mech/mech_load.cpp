// A fighter's files to the GPU (fighter_assets_load), in three steps so the garage stands
// can read off the main thread as retail's loader thread does (model_viewer_thread): the
// packs opened (main thread: it may cook), the models, motions and pictures read from them (any
// thread: only the opened packs and the step's own copies), then the uploads and the packs closed
// (main thread). load_mech runs the three in a row.
#include "mech/mech_parts.hpp"

#include <cstdio>
#include <cstring>

namespace {

// sv_sticker_assets: sticker s's picture is texture set 1349 + s (zz_emblemtex), 206 rows
constexpr int sticker_first_asset = 1349;
constexpr int sticker_rows = 0xce;

int table_at(const std::vector<int>& table, int index) {
    return index >= 0 && index < static_cast<int>(table.size()) ? table[index] : -1;
}

// the part's pack for its class: body_3, leg_0, arm_a_12...
std::string part_pack(const char* part, int part_class) {
    return std::string(part) + "_" + std::to_string(part_class);
}

const Pack* open_pack(PackLibrary& packs, MechLoad& load, const std::string& name,
                      std::string& error) {
    const Pack* pack = pack_open(packs, name, error);
    if (pack) {
        load.opened.push_back(name);
    }
    return pack;
}

// a cooked texture's blocks copied out of its pack (touching the pages here, not at the upload)
void read_picture(MechPicture& picture) {
    picture.blocks.clear();
    const PackEntry* entry =
        picture.pack ? pack_find(*picture.pack, PackKind::texture, picture.path) : nullptr;
    if (!entry) {
        return;
    }
    picture.format = entry->format == TextureFormat::bc1   ? BlockFormat::bc1
                     : entry->format == TextureFormat::bc3 ? BlockFormat::bc3
                                                           : BlockFormat::bc7;
    picture.width = static_cast<int>(entry->width);
    picture.height = static_cast<int>(entry->height);
    picture.levels = static_cast<int>(entry->levels);
    const std::uint8_t* blob = pack_blob(*picture.pack, *entry);
    picture.blocks.assign(blob, blob + entry->size);
}

// the picture as a renderer texture, once per path in the cache (as texture_for)
int upload_picture(Renderer& renderer, TextureCache& cache, const MechPicture& picture) {
    if (picture.path.empty()) {
        return -1;
    }
    const auto found = cache.loaded.find(picture.path);
    if (found != cache.loaded.end()) {
        return found->second;
    }
    int handle = -1;
    if (!picture.blocks.empty()) {
        handle =
            renderer_upload_blocks(renderer, picture.format, picture.width, picture.height,
                                   picture.levels, picture.blocks.data(), picture.blocks.size());
    }
    if (handle < 0) {
        std::fprintf(stderr, "texture: no %s in pack %s\n", picture.path.c_str(),
                     picture.pack ? picture.pack->name.c_str() : "(none)");
    }
    cache.loaded[picture.path] = handle;
    return handle;
}

// a model set without its preview pictures: the parts draw in the build's paint
bool read_model(const Pack* pack, int asset, ModelSet& out, std::string& error) {
    if (asset < 0 || !pack) {
        return true; // no such part on this build
    }
    if (!load_model_set(*pack, asset, out, error)) {
        return false;
    }
    for (ModelObject& object : out.objects) {
        for (ModelSection& section : object.sections) {
            section.picture.clear();
        }
    }
    return true;
}

bool read_motion(MotionSet& set, int asset, const Pack* pack, std::string& error) {
    return asset < 0 || !pack || load_motion_set(*pack, asset, set, error);
}

} // namespace

bool mech_load_open(MechParts& mech, MechLoad& load, const FighterTables& tables, int build,
                    const AssetManifest& manifest, PackLibrary& packs, std::string& error,
                    bool parts_only) {
    if (build < 0 || build >= static_cast<int>(tables.builds.size())) {
        error = "no build row " + std::to_string(build);
        return false;
    }
    mech.tables = &tables;
    mech.build = build;
    const FighterBuild& row = tables.builds[build];
    mech.body_class = table_at(tables.body_class, row.body);
    mech.leg_class = table_at(tables.leg_class, row.leg);
    mech.weapon_class[0] = row.weapons[0] >= 0 ? table_at(tables.arm_class, row.weapons[0]) : -1;
    mech.weapon_class[1] = row.weapons[1] >= 0 ? table_at(tables.arm_class, row.weapons[1]) : -1;
    mech.weapon_class[2] = row.weapons[2] >= 0 ? table_at(tables.back_class, row.weapons[2]) : -1;
    mech.weapon_class[3] = row.weapons[3] >= 0 ? table_at(tables.back_class, row.weapons[3]) : -1;
    // body class 2 wears its own pair on empty back mounts (D_0025AE90.f_20, D_0025AF48[0]: back
    // class 30)
    for (int back = 2; back < 4; ++back) {
        if (row.weapons[back] < 0 && mech.body_class == 2) {
            mech.weapon_class[back] = 30;
        }
    }
    std::printf("build %d %s: body class %d, leg class %d, weapon classes %d %d %d %d, colour %d\n",
                build, row.name.c_str(), mech.body_class, mech.leg_class, mech.weapon_class[0],
                mech.weapon_class[1], mech.weapon_class[2], mech.weapon_class[3], row.colour);
    if (!parts_only && (mech.body_class < 0 || mech.leg_class < 0)) {
        error = "build " + std::to_string(build) + " has no body or legs";
        return false;
    }

    // the packs of this build's classes, and what to read from each
    const struct {
        MechPart part;
        const char* name;
        int part_class;
        const std::vector<int>& models;
    } pack_rows[part_count] = {{part_body, "body", mech.body_class, tables.body_models},
                               {part_legs, "leg", mech.leg_class, tables.leg_models},
                               {part_arm_a, "arm_a", mech.weapon_class[0], tables.arm_a_models},
                               {part_arm_b, "arm_b", mech.weapon_class[1], tables.arm_b_models},
                               {part_back_a, "back_a", mech.weapon_class[2], tables.back_a_models},
                               {part_back_b, "back_b", mech.weapon_class[3], tables.back_b_models}};
    for (const auto& pack_row : pack_rows) {
        load.model_asset[pack_row.part] = table_at(pack_row.models, pack_row.part_class);
        if (pack_row.part_class >= 0) {
            load.part_pack[pack_row.part] =
                open_pack(packs, load, part_pack(pack_row.name, pack_row.part_class), error);
            if (!load.part_pack[pack_row.part]) {
                mech_load_close(load, packs);
                return false;
            }
        }
    }

    // the paint: part_color_assets[leg class][colour] (colour 0 when out of range, as the loader
    // does), from the leg class's paint pack; a build without legs takes its body class's row
    // (fighter_assets_load). One with neither (a weapon alone on a shop's stand) gets leg class
    // 0's: retail leaves the slot as the last load left it (docs/bugs.md)
    const int paint_class = mech.leg_class >= 0    ? mech.leg_class
                            : mech.body_class >= 0 ? mech.body_class
                            : parts_only           ? 0
                                                   : -1;
    if (paint_class >= 0 && paint_class < static_cast<int>(tables.part_paints.size())) {
        const std::vector<int>& paints = tables.part_paints[paint_class];
        const int colour =
            row.colour >= 0 && row.colour < 0x39 && row.colour < static_cast<int>(paints.size())
                ? row.colour
                : 0;
        const std::string folder = paints.empty() ? "" : asset_path(manifest, paints[colour]);
        if (!folder.empty()) {
            load.paint.pack = open_pack(packs, load, part_pack("paint", paint_class), error);
            load.paint.path = folder + "/00.png";
        }
    }

    // the sticker: the body class's sticker model with the build's sticker (0..0xCD;
    // fighter_assets_load loads 0xCD's blank row too) on it
    load.sticker_model = table_at(tables.body_sticker_models, mech.body_class);
    if (load.sticker_model >= 0 && row.sticker >= 0 && row.sticker < sticker_rows &&
        load.part_pack[part_body]) {
        const auto picture = manifest.assets.find(sticker_first_asset + row.sticker);
        if (picture != manifest.assets.end() && !picture->second.pack.empty()) {
            load.sticker.pack = open_pack(packs, load, picture->second.pack, error);
            load.sticker.path = picture->second.file + "/00.png";
        }
    } else {
        load.sticker_model = -1;
    }

    // the graphs and motions (the arm motions are in the leg pack: they go by leg class)
    mech.leg_graph = table_at(tables.leg_graphs, mech.leg_class);
    mech.arm_a_graph =
        mech.weapon_class[0] >= 0 ? table_at(tables.arm_a_graphs, mech.weapon_class[0]) : -1;
    mech.arm_b_graph =
        mech.weapon_class[1] >= 0 ? table_at(tables.arm_b_graphs, mech.weapon_class[1]) : -1;
    const int arm_a_class = table_at(tables.arm_motion_class, mech.weapon_class[0]);
    const int arm_b_class = table_at(tables.arm_motion_class, mech.weapon_class[1]);
    load.motion_asset[0] = table_at(tables.leg_motions, mech.leg_class);
    load.motion_asset[1] = table_at(tables.body_motions, mech.body_class);
    load.motion_asset[2] =
        arm_a_class >= 0 ? table_at(tables.arm_a_motions, mech.leg_class * 2 + arm_a_class) : -1;
    load.motion_asset[3] =
        arm_b_class >= 0 ? table_at(tables.arm_b_motions, mech.leg_class * 2 + arm_b_class) : -1;
    error.clear();
    return true;
}

bool mech_load_read(MechParts& mech, MechLoad& load) {
    std::string& error = load.error;
    for (int part = 0; part < part_count; ++part) {
        if (!read_model(load.part_pack[part], load.model_asset[part], mech.models[part], error)) {
            return false;
        }
    }
    read_picture(load.paint);
    if (load.sticker_model >= 0) {
        if (read_model(load.part_pack[part_body], load.sticker_model, mech.sticker_model, error)) {
            read_picture(load.sticker);
        } else {
            std::fprintf(stderr, "sticker: %s\n", error.c_str());
            mech.sticker_model = ModelSet{};
            load.sticker = MechPicture{};
            error.clear();
        }
    }
    const Pack* legs = load.part_pack[part_legs];
    return read_motion(mech.leg_motion, load.motion_asset[0], legs, error) &&
           read_motion(mech.body_motion, load.motion_asset[1], load.part_pack[part_body], error) &&
           read_motion(mech.arm_a_motion, load.motion_asset[2], legs, error) &&
           read_motion(mech.arm_b_motion, load.motion_asset[3], legs, error);
}

void mech_load_upload(MechParts& mech, MechLoad& load, Renderer& renderer, TextureCache& cache) {
    for (int part = 0; part < part_count; ++part) {
        upload_model_set(renderer, cache, mech.models[part], mech.gpu[part]);
        mech.loaded[part] = !mech.models[part].objects.empty();
    }
    mech.paint = upload_picture(renderer, cache, load.paint);
    if (!mech.sticker_model.objects.empty()) {
        upload_model_set(renderer, cache, mech.sticker_model, mech.sticker_gpu);
        mech.sticker_texture = upload_picture(renderer, cache, load.sticker);
    }
}

void mech_load_close(MechLoad& load, PackLibrary& packs) {
    for (const std::string& name : load.opened) {
        pack_close(packs, name);
    }
    load = MechLoad{};
}

bool load_mech(MechParts& mech, const FighterTables& tables, int build,
               const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
               TextureCache& cache, std::string& error, bool parts_only) {
    MechLoad load;
    if (!mech_load_open(mech, load, tables, build, manifest, packs, error, parts_only)) {
        return false;
    }
    const bool ok = mech_load_read(mech, load);
    if (ok) {
        mech_load_upload(mech, load, renderer, cache);
    } else {
        error = load.error;
    }
    mech_load_close(load, packs);
    return ok;
}
