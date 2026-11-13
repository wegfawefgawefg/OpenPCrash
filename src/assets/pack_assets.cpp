// The game's side of the packs: each kind of cooked entry back into the struct it was cooked
// from (the io() layouts beside each struct), and the kept .sexp files parsed from memory.
#include "assets/collision_grid.hpp"
#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "assets/model_sets.hpp"
#include "assets/motion_sets.hpp"
#include "assets/pack_sexp.hpp"
#include "assets/stage_tables.hpp"
#include "cook/pack.hpp"

namespace {

template <class T>
bool load_entry(const Pack& pack, const PackEntry* entry, const char* what, int id, T& out,
                std::string& error) {
    if (!entry) {
        error = std::string("pack ") + pack.name + " has no " + what +
                (id >= 0 ? " " + std::to_string(id) : std::string());
        return false;
    }
    out = T{};
    BlobReader reader = pack_reader(pack, *entry);
    reader(out);
    if (!reader.ok) {
        error = std::string("pack ") + pack.name + ": short " + what;
        return false;
    }
    return true;
}

} // namespace

bool load_model_set(const Pack& pack, int asset, ModelSet& out, std::string& error) {
    return load_entry(pack, pack_find(pack, PackKind::model_set, asset), "model set", asset, out,
                      error);
}

bool load_motion_set(const Pack& pack, int asset, MotionSet& out, std::string& error) {
    return load_entry(pack, pack_find(pack, PackKind::motion_set, asset), "motion set", asset, out,
                      error);
}

bool load_collision_grid(const Pack& pack, int asset, CollisionGrid& out, std::string& error) {
    return load_entry(pack, pack_find(pack, PackKind::collision, asset), "collision grid", asset,
                      out, error);
}

bool load_stage_tables(const Pack& core, StageTables& tables, std::string& error) {
    return load_entry(core, pack_find(core, PackKind::stage_tables, -1), "stage table", -1, tables,
                      error);
}

bool load_fighter_tables(const Pack& core, FighterTables& tables, std::string& error) {
    return load_entry(core, pack_find(core, PackKind::fighter_tables, -1), "fighter table", -1,
                      tables, error);
}

bool load_manifest(const Pack& core, AssetManifest& manifest, std::string& error) {
    return load_entry(core, pack_find(core, PackKind::manifest, -1), "manifest", -1, manifest,
                      error);
}

std::string asset_path(const AssetManifest& manifest, int id) {
    const auto found = manifest.assets.find(id);
    return found == manifest.assets.end() ? std::string() : found->second.file;
}

bool pack_sexp(const Pack& pack, std::string_view name, gsexp::ParseResult& out,
               std::string& error) {
    const PackEntry* entry = pack_find(pack, PackKind::sexp, name);
    if (!entry) {
        error = "pack " + pack.name + " has no " + std::string(name);
        return false;
    }
    const char* text = reinterpret_cast<const char*>(pack_blob(pack, *entry));
    out = gsexp::parse(std::string_view(text, entry->size));
    if (!out.ok) {
        error = "can't parse " + std::string(name);
        return false;
    }
    return true;
}
