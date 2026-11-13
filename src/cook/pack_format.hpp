#pragma once
// The cooked/ pack format (docs/assets.md, "Packs"): a header, an entry index, the list of data/
// files the pack was cooked from with their hashes, a string table, then the entries' blobs, each
// 16-byte aligned. A pack is read whole into memory (mmap or one read) and used in place.
//
// Entries are either raw (textures: BC blocks for every mip level, top level first) or written
// with BlobWriter and read back with BlobReader through one `io` function per type, so the cook
// and the game can't disagree on a layout. Plain-old-data arrays (vertices, indices, node records)
// are single copies. Little-endian only.
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

constexpr std::uint32_t pack_magic = 0x4B504350; // "PCPK"
// bump on any change to these structs, an io() layout or what the cook writes
constexpr std::uint32_t pack_version = 8; // 8: the shield bubble's muteki01 in the battle pack

enum class PackKind : std::uint32_t {
    texture = 1,    // name: data-relative PNG path; format, width, height, levels; BC blocks
    model_set,      // id: asset; ModelSet
    motion_set,     // id: asset; MotionSet
    collision,      // id: asset; CollisionGrid
    sexp,           // name: data-relative path; the file's text, parsed when read
    manifest,       // core: AssetManifest
    stage_tables,   // core: StageTables
    fighter_tables, // core: FighterTables
    plan,           // core: CookPlan, what every pack holds (cook_plan.hpp)
};

enum class TextureFormat : std::uint32_t { bc1 = 1, bc3, bc7 };

struct PackHeader {
    std::uint32_t magic = pack_magic;
    std::uint32_t version = pack_version;
    std::uint32_t entry_count = 0;
    std::uint32_t source_count = 0;
    std::uint64_t entries_at = 0; // PackEntry[entry_count]
    std::uint64_t sources_at = 0; // PackSource[source_count]
    std::uint64_t strings_at = 0; // zero-terminated strings, named by offset
    std::uint64_t strings_size = 0;
    std::uint64_t file_size = 0;
    std::uint64_t plan_hash = 0; // of the pack's CookPlan entry: a changed plan re-cooks it
};

struct PackEntry {
    PackKind kind = PackKind::texture;
    std::int32_t id = -1;   // asset id, -1 when named
    std::uint32_t name = 0; // string offset
    TextureFormat format = TextureFormat::bc1;
    std::uint32_t width = 0, height = 0, levels = 0;
    std::uint32_t reserved = 0;
    std::uint64_t at = 0; // blob offset in the file
    std::uint64_t size = 0;
};

// a data/ file the pack was cooked from: re-cooked when its size or time moved and its hash with
// them
struct PackSource {
    std::uint32_t path = 0; // string offset, relative to data/
    std::uint32_t reserved = 0;
    std::uint64_t size = 0;
    std::int64_t time = 0; // last write, the filesystem clock's ticks
    std::uint64_t hash = 0;
};

static_assert(sizeof(PackHeader) == 64 && sizeof(PackEntry) == 48 && sizeof(PackSource) == 32);

// a BC block's bytes, and the bytes of one level (whole 4x4 blocks)
inline std::uint32_t texture_block_bytes(TextureFormat format) {
    return format == TextureFormat::bc1 ? 8 : 16;
}
inline std::uint64_t texture_level_bytes(TextureFormat format, std::uint32_t width,
                                         std::uint32_t height) {
    return static_cast<std::uint64_t>((width + 3) / 4) * ((height + 3) / 4) *
           texture_block_bytes(format);
}

// Writes values in order. Trivially copyable values and vectors of them are copied whole; other
// structs go through their io(archive, value).
struct BlobWriter {
    std::vector<std::uint8_t> bytes;
    static constexpr bool reading = false;

    void raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
    template <class T> void operator()(const T& value) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            raw(&value, sizeof value);
        } else {
            io(*this, const_cast<T&>(value));
        }
    }
    void operator()(const std::string& text) {
        const auto size = static_cast<std::uint32_t>(text.size());
        (*this)(size);
        raw(text.data(), size);
    }
    template <class T> void operator()(const std::vector<T>& list) {
        const auto count = static_cast<std::uint32_t>(list.size());
        (*this)(count);
        if constexpr (std::is_trivially_copyable_v<T>) {
            raw(list.data(), list.size() * sizeof(T));
        } else {
            for (const T& item : list) {
                (*this)(item);
            }
        }
    }
};

// Reads what a BlobWriter wrote; a short blob leaves `ok` false and zeros after the end.
struct BlobReader {
    const std::uint8_t* at = nullptr;
    const std::uint8_t* end = nullptr;
    bool ok = true;
    static constexpr bool reading = true;

    void raw(void* data, std::size_t size) {
        if (size == 0) {
            return; // an empty list's data() may be null, which memcpy mustn't see
        }
        if (static_cast<std::size_t>(end - at) < size) {
            ok = false;
            std::memset(data, 0, size);
            at = end;
            return;
        }
        std::memcpy(data, at, size);
        at += size;
    }
    template <class T> void operator()(T& value) {
        if constexpr (std::is_trivially_copyable_v<T>) {
            raw(&value, sizeof value);
        } else {
            io(*this, value);
        }
    }
    void operator()(std::string& text) {
        std::uint32_t size = 0;
        (*this)(size);
        if (size > static_cast<std::size_t>(end - at)) {
            ok = false;
            size = 0;
        }
        text.assign(reinterpret_cast<const char*>(at), size);
        at += size;
    }
    template <class T> void operator()(std::vector<T>& list) {
        std::uint32_t count = 0;
        (*this)(count);
        if constexpr (std::is_trivially_copyable_v<T>) {
            if (static_cast<std::size_t>(count) * sizeof(T) > static_cast<std::size_t>(end - at)) {
                ok = false;
                count = 0;
            }
            list.resize(count);
            raw(list.data(), count * sizeof(T));
        } else {
            if (count > static_cast<std::size_t>(end - at)) {
                ok = false;
                count = 0;
            }
            list.resize(count);
            for (T& item : list) {
                (*this)(item);
            }
        }
    }
};

// 64-bit hash of a byte run (word-at-a-time multiply and rotate, then an avalanche): change
// detection, not security
std::uint64_t hash_bytes(const void* data, std::size_t size);
