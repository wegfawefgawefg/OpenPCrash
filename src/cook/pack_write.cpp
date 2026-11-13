#include "cook/pack_write.hpp"

#include "cook/pack.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>

namespace {

std::uint64_t align16(std::uint64_t at) {
    return (at + 15) & ~std::uint64_t{15};
}

struct Strings {
    std::vector<char> bytes{'\0'}; // offset 0 is ""
    std::map<std::string, std::uint32_t> offsets;

    std::uint32_t add(const std::string& text) {
        if (text.empty()) {
            return 0;
        }
        const auto found = offsets.find(text);
        if (found != offsets.end()) {
            return found->second;
        }
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), text.begin(), text.end());
        bytes.push_back('\0');
        offsets[text] = offset;
        return offset;
    }
};

} // namespace

void build_add(PackBuild& build, PackKind kind, int id, const std::string& name,
               std::vector<std::uint8_t> blob) {
    BuiltEntry built;
    built.entry.kind = kind;
    built.entry.id = id;
    built.name = name;
    built.blob = std::move(blob);
    build.entries.push_back(std::move(built));
}

bool write_pack(const PackBuild& build, const std::string& path, const std::string& data_dir,
                std::uint64_t plan_hash, std::uint64_t& written, std::string& error) {
    // the sources as they are now
    Strings strings;
    std::vector<std::string> source_paths = build.sources;
    std::sort(source_paths.begin(), source_paths.end());
    source_paths.erase(std::unique(source_paths.begin(), source_paths.end()), source_paths.end());
    std::vector<PackSource> sources;
    for (const std::string& source : source_paths) {
        FileStamp stamp;
        if (!stamp_file(data_dir + "/" + source, stamp, true)) {
            error = "can't read " + data_dir + "/" + source;
            return false;
        }
        PackSource row;
        row.path = strings.add(source);
        row.size = stamp.size;
        row.time = stamp.time;
        row.hash = stamp.hash;
        sources.push_back(row);
    }
    std::vector<PackEntry> entries;
    for (const BuiltEntry& built : build.entries) {
        PackEntry entry = built.entry;
        entry.name = strings.add(built.name);
        entry.size = built.blob.size();
        entries.push_back(entry);
    }

    // header | entries | sources | strings | blobs, each blob 16-byte aligned
    PackHeader header;
    header.entry_count = static_cast<std::uint32_t>(entries.size());
    header.source_count = static_cast<std::uint32_t>(sources.size());
    header.entries_at = sizeof(PackHeader);
    header.sources_at = header.entries_at + entries.size() * sizeof(PackEntry);
    header.strings_at = header.sources_at + sources.size() * sizeof(PackSource);
    header.strings_size = strings.bytes.size();
    header.plan_hash = plan_hash;
    std::uint64_t at = align16(header.strings_at + header.strings_size);
    for (PackEntry& entry : entries) {
        entry.at = at;
        at = align16(at + entry.size);
    }
    header.file_size = at;

    const std::string temporary = path + ".tmp";
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file) {
        error = "can't write " + temporary;
        return false;
    }
    std::vector<std::uint8_t> head(align16(header.strings_at + header.strings_size), 0);
    std::memcpy(head.data(), &header, sizeof header);
    std::memcpy(head.data() + header.entries_at, entries.data(),
                entries.size() * sizeof(PackEntry));
    std::memcpy(head.data() + header.sources_at, sources.data(),
                sources.size() * sizeof(PackSource));
    std::memcpy(head.data() + header.strings_at, strings.bytes.data(), strings.bytes.size());
    bool ok = std::fwrite(head.data(), 1, head.size(), file) == head.size();
    const std::uint8_t zeros[16] = {};
    for (std::size_t i = 0; i < entries.size() && ok; ++i) {
        const std::vector<std::uint8_t>& blob = build.entries[i].blob;
        ok = std::fwrite(blob.data(), 1, blob.size(), file) == blob.size();
        const std::size_t pad = align16(blob.size()) - blob.size();
        ok = ok && std::fwrite(zeros, 1, pad, file) == pad;
    }
    ok = std::fclose(file) == 0 && ok;
    std::error_code failed;
    if (ok) {
        std::filesystem::rename(temporary, path, failed);
    }
    if (!ok || failed) {
        error = "can't write " + path;
        std::filesystem::remove(temporary, failed);
        return false;
    }
    written = header.file_size;
    return true;
}
