#include "disc_image.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t sector = 2048;
// the volume descriptor's sector, and where the game partition starts in a full Redump image
// (the video partition ahead of it): XGD1 (2001-2005 discs, Phantom Crash's), XGD2, XGD3
constexpr std::uint64_t volume_sector = 32;
constexpr std::uint64_t partitions[] = {0, 0x18300000, 0xFD90000, 0x2080000};
constexpr char magic[] = "MICROSOFT*XBOX*MEDIA";
constexpr std::size_t magic_size = 20;
// a sane disc's limits, so a broken table can't run away
constexpr std::size_t most_entries = 200000;
constexpr std::uint32_t most_table = 64u << 20;

std::string upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// "Media\\sound/X.wav" -> "MEDIA/SOUND/X.WAV"
std::string key_of(const std::string& disc_path) {
    std::string key;
    for (char c : disc_path) {
        c = c == '\\' ? '/' : c;
        if (c == '/' && (key.empty() || key.back() == '/')) {
            continue;
        }
        key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    while (!key.empty() && key.back() == '/') {
        key.pop_back();
    }
    return key;
}

bool read_at(SDL_IOStream* io, std::uint64_t at, void* out, std::size_t n) {
    if (SDL_SeekIO(io, static_cast<Sint64>(at), SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    std::size_t got = 0;
    while (got < n) {
        const std::size_t step = SDL_ReadIO(io, static_cast<std::uint8_t*>(out) + got, n - got);
        if (step == 0) {
            return false;
        }
        got += step;
    }
    return true;
}

bool starts_with(const std::uint8_t* bytes, const char* text, std::size_t n) {
    return std::memcmp(bytes, text, n) == 0;
}

// one XDVDFS directory table into source.entries under prefix; false if it's broken
struct TableWalk {
    DiscSource& source;
    SDL_IOStream* io;
};

bool walk_table(TableWalk& walk, std::uint64_t table_sector, std::uint32_t table_size,
                const std::string& prefix, int depth) {
    if (table_size == 0) {
        return true; // an empty folder
    }
    const std::uint64_t at = walk.source.partition + table_sector * sector;
    if (depth > 32 || table_size > most_table || at + table_size > walk.source.image_size) {
        return false;
    }
    std::vector<std::uint8_t> table(table_size);
    if (!read_at(walk.io, at, table.data(), table.size())) {
        return false;
    }
    // the entries form a binary tree by 4-byte offsets; walk it with a stack
    std::vector<std::uint32_t> pending = {0};
    std::set<std::uint32_t> seen;
    while (!pending.empty()) {
        const std::uint32_t offset = pending.back() * 4;
        pending.pop_back();
        if (!seen.insert(offset).second) {
            continue;
        }
        if (offset + 14 > table.size()) {
            return false;
        }
        const std::uint8_t* entry = table.data() + offset;
        std::uint16_t left = 0, right = 0;
        std::uint32_t start = 0, size = 0;
        std::memcpy(&left, entry, 2);
        std::memcpy(&right, entry + 2, 2);
        std::memcpy(&start, entry + 4, 4);
        std::memcpy(&size, entry + 8, 4);
        const std::uint8_t attributes = entry[12];
        const std::uint8_t name_length = entry[13];
        if (left == 0xFFFF && right == 0xFFFF) {
            continue; // padding at a sector's end
        }
        if (name_length == 0 || offset + 14 + name_length > table.size()) {
            return false;
        }
        const std::string name(reinterpret_cast<const char*>(entry + 14), name_length);
        if (left != 0 && left != 0xFFFF) {
            pending.push_back(left);
        }
        if (right != 0 && right != 0xFFFF) {
            pending.push_back(right);
        }
        if (name == "." || name == ".." || name.find('/') != std::string::npos) {
            return false;
        }
        const std::string path = prefix.empty() ? name : prefix + "/" + name;
        DiscEntry file;
        file.offset = walk.source.partition + static_cast<std::uint64_t>(start) * sector;
        file.size = size;
        file.directory = (attributes & 0x10) != 0;
        if (!file.directory && file.offset + size > walk.source.image_size) {
            return false; // the image stops before this file does
        }
        const std::string key = upper(path);
        walk.source.entries[key] = file;
        walk.source.stored_names[key] = path;
        if (walk.source.entries.size() > most_entries) {
            return false;
        }
        if (file.directory && !walk_table(walk, start, size, path, depth + 1)) {
            return false;
        }
    }
    return true;
}

bool open_image(DiscSource& source, DiscProblem& problem, std::string& error) {
    SDL_IOStream* io = SDL_IOFromFile(source.path.c_str(), "rb");
    if (!io) {
        problem = DiscProblem::missing;
        error = "can't open " + source.path + ": " + SDL_GetError();
        return false;
    }
    const Sint64 size = SDL_GetIOSize(io);
    source.image_size = size > 0 ? static_cast<std::uint64_t>(size) : 0;
    std::uint8_t head[8] = {};
    read_at(io, 0, head, sizeof head);
    if (starts_with(head, "PK\x03\x04", 4) || starts_with(head, "7z\xBC\xAF\x27\x1C", 6) ||
        starts_with(head, "Rar!", 4)) {
        SDL_CloseIO(io);
        problem = DiscProblem::archive;
        error = source.path + " is a compressed archive; unpack it first";
        return false;
    }
    // the volume descriptor at sector 32 of the partition
    bool found = false;
    std::uint8_t volume[sector] = {};
    for (std::uint64_t partition : partitions) {
        const std::uint64_t at = partition + volume_sector * sector;
        if (at + sector <= source.image_size && read_at(io, at, volume, sector) &&
            starts_with(volume, magic, magic_size)) {
            source.partition = partition;
            found = true;
            break;
        }
    }
    if (!found) {
        SDL_CloseIO(io);
        // big enough to be a disc, but a partition start lies past its end: cut short
        const bool short_redump = source.image_size > 0 && source.image_size < partitions[1] &&
                                  source.image_size > 0x10000000;
        problem = DiscProblem::not_xbox;
        error = source.path + " is not an Xbox disc image (no XDVDFS volume)" +
                (short_redump ? std::string(", or it is cut short") : std::string());
        return false;
    }
    std::uint32_t root_sector = 0, root_size = 0;
    std::memcpy(&root_sector, volume + 0x14, 4);
    std::memcpy(&root_size, volume + 0x18, 4);
    TableWalk walk{source, io};
    const bool walked = walk_table(walk, root_sector, root_size, "", 0) &&
                        starts_with(volume + 0x7EC, magic, magic_size);
    SDL_CloseIO(io);
    if (!walked || source.entries.empty()) {
        problem = DiscProblem::damaged;
        error = source.path + ": the disc image's file table is broken or the image is cut short";
        return false;
    }
    source.image = true;
    return true;
}

// a folder's file by its disc path: as written, else matched case-insensitively part by part
// (an extracted disc may keep any case); empty if missing
fs::path folder_file(const DiscSource& source, const std::string& disc_path) {
    std::string relative = disc_path;
    for (char& c : relative) {
        c = c == '\\' ? '/' : c;
    }
    fs::path exact = fs::path(source.path) / relative;
    std::error_code error;
    if (fs::exists(exact, error)) {
        return exact;
    }
    fs::path at = source.path;
    std::size_t start = 0;
    while (start <= relative.size()) {
        std::size_t end = relative.find('/', start);
        end = end == std::string::npos ? relative.size() : end;
        const std::string part = relative.substr(start, end - start);
        start = end + 1;
        if (part.empty()) {
            continue;
        }
        const std::string wanted = upper(part);
        bool matched = false;
        for (const fs::directory_entry& entry : fs::directory_iterator(at, error)) {
            if (upper(entry.path().filename().string()) == wanted) {
                at = entry.path();
                matched = true;
                break;
            }
        }
        if (!matched) {
            return {};
        }
    }
    return at;
}

} // namespace

bool open_disc_source(DiscSource& source, const std::string& path, DiscProblem& problem,
                      std::string& error) {
    source = DiscSource{};
    source.path = path;
    problem = DiscProblem::none;
    std::error_code fs_error;
    if (path.empty() || !fs::exists(path, fs_error)) {
        problem = DiscProblem::missing;
        error = "nothing at " + (path.empty() ? std::string("(no path)") : path);
        return false;
    }
    if (!fs::is_directory(path, fs_error)) {
        return open_image(source, problem, error);
    }
    // a folder: the one holding Media/, or Media/ itself
    if (!folder_file(source, "Media").empty()) {
        return true;
    }
    const fs::path folder(path);
    if (upper(folder.filename().string()) == "MEDIA" ||
        (folder.filename().empty() && upper(folder.parent_path().filename().string()) == "MEDIA")) {
        DiscSource parent = source;
        parent.path = (folder.filename().empty() ? folder.parent_path() : folder)
                          .parent_path()
                          .string();
        if (!folder_file(parent, "Media/binlink").empty()) {
            source = parent;
            return true;
        }
    }
    problem = DiscProblem::not_xbox;
    error = path + " has no Media folder: it isn't an extracted Xbox disc";
    return false;
}

bool disc_read(const DiscSource& source, const std::string& disc_path, std::uint64_t offset,
               std::uint64_t size, std::vector<std::uint8_t>& out) {
    std::string file = source.path;
    std::uint64_t base = 0;
    if (source.image) {
        const auto entry = source.entries.find(key_of(disc_path));
        if (entry == source.entries.end() || entry->second.directory ||
            offset + size > entry->second.size) {
            return false;
        }
        base = entry->second.offset;
    } else {
        const fs::path found = folder_file(source, disc_path);
        if (found.empty()) {
            return false;
        }
        file = found.string();
    }
    SDL_IOStream* io = SDL_IOFromFile(file.c_str(), "rb");
    if (!io) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const bool read = size == 0 || read_at(io, base + offset, out.data(), out.size());
    SDL_CloseIO(io);
    if (!read) {
        out.clear();
    }
    return read;
}

std::vector<std::uint8_t> disc_file(const DiscSource& source, const std::string& disc_path) {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size = 0;
    if (source.image) {
        const auto entry = source.entries.find(key_of(disc_path));
        if (entry == source.entries.end() || entry->second.directory) {
            return {};
        }
        size = entry->second.size;
    } else {
        const fs::path found = folder_file(source, disc_path);
        std::error_code error;
        if (found.empty() || fs::is_directory(found, error)) {
            return {};
        }
        size = fs::file_size(found, error);
        if (error) {
            return {};
        }
    }
    disc_read(source, disc_path, 0, size, bytes);
    return bytes;
}

bool disc_exists(const DiscSource& source, const std::string& disc_path) {
    if (source.image) {
        return source.entries.count(key_of(disc_path)) != 0;
    }
    return !folder_file(source, disc_path).empty();
}

std::vector<std::string> disc_list(const DiscSource& source, const std::string& folder) {
    std::vector<std::string> names;
    if (source.image) {
        const std::string prefix = key_of(folder) + "/";
        for (const auto& [key, entry] : source.entries) {
            if (key.compare(0, prefix.size(), prefix) == 0 &&
                key.find('/', prefix.size()) == std::string::npos && !entry.directory) {
                const std::string& stored = source.stored_names.at(key);
                names.push_back(stored.substr(stored.find_last_of('/') + 1));
            }
        }
        return names;
    }
    const fs::path found = folder_file(source, folder);
    std::error_code error;
    if (found.empty()) {
        return names;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(found, error)) {
        if (entry.is_regular_file(error)) {
            names.push_back(entry.path().filename().string());
        }
    }
    // in the image's order (upper-cased names), so a folder and an image extract alike
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return upper(a) < upper(b); });
    return names;
}

std::string disc_source_label(const DiscSource& source) {
    if (!source.image) {
        return "folder " + source.path;
    }
    char at[32];
    std::snprintf(at, sizeof at, "0x%llx", static_cast<unsigned long long>(source.partition));
    return std::string(source.partition ? "Redump image " : "XISO image ") + source.path +
           " (game partition at " + at + ", " + std::to_string(source.entries.size()) +
           " entries)";
}
