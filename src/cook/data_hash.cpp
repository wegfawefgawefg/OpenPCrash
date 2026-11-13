#include "cook/data_hash.hpp"

#include "cook/pack_library.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace {

// the packs a battle builds its world from, besides the core pack
bool gameplay_pack(const std::string& name) {
    for (const char* prefix : {"stage", "leg", "body", "arm_", "back_"}) {
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

struct Mixer {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void word(std::uint64_t v) {
        h = (h ^ v) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 32;
    }
    void bytes(const std::uint8_t* at, std::size_t size) {
        word(size);
        std::size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            std::uint64_t v = 0;
            std::memcpy(&v, at + i, 8); // little endian on every target the game builds for
            word(v);
        }
        std::uint64_t tail = 0;
        std::memcpy(&tail, at + i, size - i);
        word(tail);
    }
    void text(const char* s) {
        bytes(reinterpret_cast<const std::uint8_t*>(s), std::strlen(s));
    }
};

// every entry but the textures, in the pack's order, by kind, id, name and bytes
void hash_pack(Mixer& m, const Pack& pack, DataHash& out) {
    m.text(pack.name.c_str());
    for (std::uint32_t i = 0; i < pack.header->entry_count; ++i) {
        const PackEntry& e = pack.entries[i];
        if (e.kind == PackKind::texture) {
            continue;
        }
        m.word(static_cast<std::uint64_t>(e.kind));
        m.word(static_cast<std::uint64_t>(static_cast<std::int64_t>(e.id)));
        m.text(pack_string(pack, e.name));
        m.bytes(pack_blob(pack, e), static_cast<std::size_t>(e.size));
        out.entries += 1;
        out.bytes += e.size;
    }
    out.packs += 1;
}

} // namespace

bool gameplay_data_hash(PackLibrary& packs, DataHash& out, std::string& error) {
    out = DataHash{};
    Mixer m;
    hash_pack(m, packs.core, out);
    // the plan's packs in its order, each opened for the hash and closed again unless it was open
    for (const PackPlan& plan : packs.plan.packs) {
        if (!gameplay_pack(plan.name)) {
            continue;
        }
        const bool was_open = packs.open.count(plan.name) != 0;
        const Pack* pack = pack_open(packs, plan.name, error);
        if (!pack) {
            return false;
        }
        hash_pack(m, *pack, out);
        if (!was_open) {
            pack_close(packs, plan.name);
        }
    }
    out.hash = m.h;
    return true;
}

void data_hash_print(PackLibrary& packs) {
    const auto start = std::chrono::steady_clock::now();
    DataHash hash;
    std::string error;
    if (!gameplay_data_hash(packs, hash, error)) {
        std::fprintf(stderr, "data hash: %s\n", error.c_str());
        return;
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("data hash: %016llx (%d packs, %d entries, %.1f MB) in %.1f ms\n",
                static_cast<unsigned long long>(hash.hash), hash.packs, hash.entries,
                static_cast<double>(hash.bytes) / 1048576.0, ms);
}
