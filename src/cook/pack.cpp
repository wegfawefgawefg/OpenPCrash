#include "cook/pack.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

std::uint64_t hash_bytes(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    const std::uint64_t k = 0x9E3779B97F4A7C15ull;
    std::uint64_t h = 0x243F6A8885A308D3ull ^ (size * k);
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        std::uint64_t word;
        std::memcpy(&word, p + i, 8);
        h = ((h ^ (word * k)) << 27 | (h ^ (word * k)) >> 37) * 0xC2B2AE3D27D4EB4Full;
    }
    std::uint64_t tail = 0;
    std::memcpy(&tail, p + i, size - i);
    h ^= tail * k;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    return h ^ (h >> 33);
}

Pack::~Pack() {
    pack_release(*this);
}

void pack_release(Pack& pack) {
#ifndef _WIN32
    if (pack.mapping) {
        munmap(pack.mapping, pack.size);
    }
#endif
    pack.mapping = nullptr;
    pack.owned.clear();
    pack.owned.shrink_to_fit();
    pack.bytes = nullptr;
    pack.size = 0;
    pack.header = nullptr;
}

bool pack_read(const std::string& path, Pack& pack, std::string& error) {
    pack_release(pack);
#ifndef _WIN32
    // mapped: an entry's pages are read when first touched, so a big pack costs what's used
    const int fd = open(path.c_str(), O_RDONLY);
    struct stat info {};
    if (fd < 0 || fstat(fd, &info) != 0) {
        if (fd >= 0) {
            close(fd);
        }
        error = "no pack " + path;
        return false;
    }
    pack.size = static_cast<std::size_t>(info.st_size);
    void* map = pack.size ? mmap(nullptr, pack.size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    close(fd);
    if (map == MAP_FAILED) {
        pack.size = 0;
        error = "can't map " + path;
        return false;
    }
    pack.mapping = map;
    pack.bytes = static_cast<const std::uint8_t*>(map);
#else
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "no pack " + path;
        return false;
    }
    pack.owned.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(pack.owned.data()), pack.owned.size());
    pack.bytes = pack.owned.data();
    pack.size = pack.owned.size();
#endif
    // the header and every table inside the file
    const auto* header = reinterpret_cast<const PackHeader*>(pack.bytes);
    const auto inside = [&](std::uint64_t at, std::uint64_t bytes) {
        return at <= pack.size && bytes <= pack.size - at;
    };
    if (pack.size < sizeof(PackHeader) || header->magic != pack_magic ||
        header->version != pack_version || header->file_size != pack.size ||
        !inside(header->entries_at, std::uint64_t{header->entry_count} * sizeof(PackEntry)) ||
        !inside(header->sources_at, std::uint64_t{header->source_count} * sizeof(PackSource)) ||
        !inside(header->strings_at, header->strings_size) || header->strings_size == 0) {
        error = "stale or broken pack " + path;
        pack_release(pack);
        return false;
    }
    pack.header = header;
    pack.entries = reinterpret_cast<const PackEntry*>(pack.bytes + header->entries_at);
    pack.sources = reinterpret_cast<const PackSource*>(pack.bytes + header->sources_at);
    pack.strings = reinterpret_cast<const char*>(pack.bytes + header->strings_at);
    for (std::uint32_t i = 0; i < header->entry_count; ++i) {
        if (!inside(pack.entries[i].at, pack.entries[i].size)) {
            error = "broken entry in " + path;
            pack_release(pack);
            return false;
        }
    }
    pack.name = std::filesystem::path(path).stem().string();
    pack.path = path;
    return true;
}

const char* pack_string(const Pack& pack, std::uint32_t offset) {
    return offset < pack.header->strings_size ? pack.strings + offset : "";
}

const PackEntry* pack_find(const Pack& pack, PackKind kind, int id) {
    for (std::uint32_t i = 0; i < pack.header->entry_count; ++i) {
        if (pack.entries[i].kind == kind && pack.entries[i].id == id) {
            return &pack.entries[i];
        }
    }
    return nullptr;
}

const PackEntry* pack_find(const Pack& pack, PackKind kind, std::string_view name) {
    for (std::uint32_t i = 0; i < pack.header->entry_count; ++i) {
        if (pack.entries[i].kind == kind && pack_string(pack, pack.entries[i].name) == name) {
            return &pack.entries[i];
        }
    }
    return nullptr;
}

const std::uint8_t* pack_blob(const Pack& pack, const PackEntry& entry) {
    return pack.bytes + entry.at;
}

BlobReader pack_reader(const Pack& pack, const PackEntry& entry) {
    BlobReader reader;
    reader.at = pack_blob(pack, entry);
    reader.end = reader.at + entry.size;
    return reader;
}

bool stamp_file(const std::string& path, FileStamp& stamp, bool with_hash) {
    std::error_code failed;
    const auto size = std::filesystem::file_size(path, failed);
    if (failed) {
        return false;
    }
    const auto time = std::filesystem::last_write_time(path, failed);
    if (failed) {
        return false;
    }
    stamp.size = size;
    stamp.time = static_cast<std::int64_t>(time.time_since_epoch().count());
    stamp.hash = 0;
    if (with_hash) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes(size);
        if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(size))) {
            return false;
        }
        stamp.hash = hash_bytes(bytes.data(), bytes.size());
    }
    return true;
}

bool pack_sources_fresh(const Pack& pack, const std::string& data_dir) {
    std::vector<std::pair<std::uint32_t, PackSource>> touched;
    for (std::uint32_t i = 0; i < pack.header->source_count; ++i) {
        const PackSource& source = pack.sources[i];
        const std::string path = data_dir + "/" + pack_string(pack, source.path);
        FileStamp stamp;
        if (!stamp_file(path, stamp, false)) {
            return false;
        }
        if (stamp.size == source.size && stamp.time == source.time) {
            continue;
        }
        // touched: the same bytes still count as fresh
        if (stamp.size != source.size || !stamp_file(path, stamp, true) ||
            stamp.hash != source.hash) {
            return false;
        }
        PackSource row = source;
        row.time = stamp.time;
        touched.push_back({i, row});
    }
    // the new times into the file's source table (only freshness checks read them)
    std::FILE* file = touched.empty() ? nullptr : std::fopen(pack.path.c_str(), "r+b");
    for (const auto& [index, row] : touched) {
        const auto at = static_cast<long>(pack.header->sources_at + index * sizeof row);
        if (file && std::fseek(file, at, SEEK_SET) == 0) {
            std::fwrite(&row, sizeof row, 1, file);
        }
    }
    if (file) {
        std::fclose(file);
    }
    return true;
}
