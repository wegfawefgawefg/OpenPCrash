#pragma once
// The gameplay data hash (docs/multiplayer.md "Change now", item 12): one hash over the cooked data
// the world is built from, so two machines can tell they hold the same data before a battle (the
// lobby handshake) and a run log says which data it ran on. Every player converts their own disc;
// another disc release, converter or cook gives another hash. It covers every entry but the
// textures of the core pack (the tables, the manifest, the fighter and stage tables) and of the
// packs a battle reads (stages: collision, nav; legs: motions; bodies, arms and backs: the models'
// node trees and muzzles, with their vertices). The front end's packs and all textures are
// presentation and left out.
#include <cstdint>
#include <string>

struct PackLibrary;

struct DataHash {
    std::uint64_t hash = 0;
    int packs = 0;
    int entries = 0;
    std::uint64_t bytes = 0;
};

// opens (and closes again) each pack it reads; false with why when one won't open
bool gameplay_data_hash(PackLibrary& packs, DataHash& out, std::string& error);
// "data hash: HEX (packs, entries, MB) in ms" for the run log
void data_hash_print(PackLibrary& packs);
