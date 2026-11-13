#pragma once
// A pack being built in memory, then written whole (to a temporary name, then renamed over the old
// one, so a game holding the old pack mapped keeps reading it).
#include "cook/pack_format.hpp"

#include <string>
#include <vector>

struct BuiltEntry {
    PackEntry entry; // name, at and size are filled in by write_pack
    std::string name;
    std::vector<std::uint8_t> blob;
};

struct PackBuild {
    std::string name;
    std::vector<BuiltEntry> entries;
    std::vector<std::string> sources; // relative to data/, stamped with hashes when written
};

void build_add(PackBuild& build, PackKind kind, int id, const std::string& name,
               std::vector<std::uint8_t> blob);
template <class T>
void build_add_value(PackBuild& build, PackKind kind, int id, const std::string& name,
                     const T& value) {
    BlobWriter writer;
    writer(value);
    build_add(build, kind, id, name, std::move(writer.bytes));
}

bool write_pack(const PackBuild& build, const std::string& path, const std::string& data_dir,
                std::uint64_t plan_hash, std::uint64_t& written, std::string& error);
