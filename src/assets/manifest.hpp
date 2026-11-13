#pragma once
// data/manifest.sexp as the core pack keeps it: per original asset id its converted file and the
// pack the cook put it in (docs/assets.md).
#include <cstdint>
#include <map>
#include <string>

struct Pack;

struct ManifestEntry {
    std::string kind; // model-set, texture-set, motion-set, collision-grid
    std::string file; // relative to data/: a file, or a folder for texture sets
    std::string pack; // the cooked pack that holds it, "" when none does
    int first_slot = -1;
    int slot_count = 0;
    int texture_set = -1; // model sets: the descriptor's texture set
};

struct AssetManifest {
    std::map<int, ManifestEntry> assets; // by registry id
};

// from the core pack
bool load_manifest(const Pack& core, AssetManifest& manifest, std::string& error);
// the asset's file relative to data/ (a pack's texture keys and names start from it), empty if
// the manifest doesn't have it
std::string asset_path(const AssetManifest& manifest, int id);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, ManifestEntry& e) {
    a(e.kind);
    a(e.file);
    a(e.pack);
    a(e.first_slot);
    a(e.slot_count);
    a(e.texture_set);
}
template <class A> void io(A& a, AssetManifest& m) {
    auto count = static_cast<std::uint32_t>(m.assets.size());
    a(count);
    if constexpr (A::reading) {
        m.assets.clear();
        for (std::uint32_t i = 0; i < count; ++i) {
            int id = 0;
            a(id);
            io(a, m.assets[id]);
        }
    } else {
        for (auto& [id, entry] : m.assets) {
            int key = id;
            a(key);
            io(a, entry);
        }
    }
}
