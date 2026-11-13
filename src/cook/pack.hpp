#pragma once
// One cooked pack in memory: mapped (POSIX) or read whole (elsewhere), its index used in place.
#include "cook/pack_format.hpp"

#include <string>
#include <string_view>

struct Pack {
    std::string name;
    std::string path;
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    const PackHeader* header = nullptr;
    const PackEntry* entries = nullptr;
    const PackSource* sources = nullptr;
    const char* strings = nullptr;
    void* mapping = nullptr;         // mmap'd bytes, released by pack_release
    std::vector<std::uint8_t> owned; // or the read bytes

    Pack() = default;
    Pack(const Pack&) = delete;
    Pack& operator=(const Pack&) = delete;
    ~Pack();
};

// reads and checks a pack file; false (with why) when missing, short or of another version
bool pack_read(const std::string& path, Pack& pack, std::string& error);
void pack_release(Pack& pack);

const char* pack_string(const Pack& pack, std::uint32_t offset);
const PackEntry* pack_find(const Pack& pack, PackKind kind, int id);
const PackEntry* pack_find(const Pack& pack, PackKind kind, std::string_view name);
const std::uint8_t* pack_blob(const Pack& pack, const PackEntry& entry);
BlobReader pack_reader(const Pack& pack, const PackEntry& entry);

// a data/ file's size, write time and (when asked) hash; false when it can't be read
struct FileStamp {
    std::uint64_t size = 0;
    std::int64_t time = 0;
    std::uint64_t hash = 0;
};
bool stamp_file(const std::string& path, FileStamp& stamp, bool with_hash);
// every source still as cooked: same size and time, or else the same hash (then the new time is
// written into the pack file, so it isn't hashed again)
bool pack_sources_fresh(const Pack& pack, const std::string& data_dir);
