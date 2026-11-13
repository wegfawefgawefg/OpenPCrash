#include "disc.hpp"

#include "addresses.hpp"

#include <cstdio>
#include <cstring>

namespace {

// "D:\Media\binlink\a00_holymodel.bin" -> "a00_holymodel"
std::string cabinet_stem(const std::string& path) {
    const std::size_t slash = path.find_last_of('\\');
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = stem.rfind('.');
    return dot == std::string::npos ? stem : stem.substr(0, dot);
}

// "D:\Media\model\choco_mdl.bin" -> "Media/model/choco_mdl.bin"
std::string disc_relative(const std::string& path) {
    std::string out = path.size() > 3 && path[1] == ':' ? path.substr(3) : path;
    for (char& c : out) {
        if (c == '\\') {
            c = '/';
        }
    }
    return out;
}

// the retail XBE the port's addresses (addresses.hpp) are read from: its certificate's title
// (id 0x504C0001, "phantomcrash", North America, version 5), its size and an FNV-1a fingerprint
constexpr std::uint32_t retail_title_id = 0x504C0001;
constexpr std::size_t retail_size = 5836800;
constexpr std::uint64_t retail_fingerprint = 0xd0bffd36ceb41c6bull;

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 0x100000001b3ull;
    }
    return hash;
}

bool check_release(const Xbe& xbe, DiscProblem& problem, std::string& error) {
    // the certificate: its address at +0x118, the title id at +8, the name (UTF-16) at +0xC
    std::uint32_t certificate = 0;
    if (xbe.bytes.size() >= 0x11C) {
        std::memcpy(&certificate, xbe.bytes.data() + 0x118, 4);
    }
    const std::size_t at = certificate - xbe.base;
    if (certificate < xbe.base || at + 0x5C > xbe.bytes.size()) {
        problem = DiscProblem::damaged;
        error = "the disc's default.xbe has no certificate";
        return false;
    }
    std::uint32_t title_id = 0;
    std::memcpy(&title_id, xbe.bytes.data() + at + 8, 4);
    std::string title;
    for (std::size_t c = 0; c < 40; ++c) {
        const std::uint8_t low = xbe.bytes[at + 0xC + c * 2], high = xbe.bytes[at + 0xD + c * 2];
        if (low == 0 && high == 0) {
            break;
        }
        title.push_back(high == 0 && low >= 0x20 && low < 0x7F ? static_cast<char>(low) : '?');
    }
    char id[16];
    std::snprintf(id, sizeof id, "%08X", title_id);
    if (title_id != retail_title_id) {
        problem = DiscProblem::not_phantom_crash;
        error = "this disc is \"" + title + "\" (title id " + id + "), not Phantom Crash";
        return false;
    }
    if (xbe.bytes.size() != retail_size || fnv1a(xbe.bytes) != retail_fingerprint) {
        problem = DiscProblem::other_release;
        error = "this is Phantom Crash, but a release the port doesn't read yet (its default.xbe "
                "differs from the North American retail disc's)";
        return false;
    }
    return true;
}

} // namespace

bool open_disc(Disc& disc, const std::string& path, const std::string& xbe_path,
               const std::string& xbe_fallback, DiscProblem& problem, std::string& error) {
    disc.root = path;
    if (!open_disc_source(disc.source, path, problem, error)) {
        return false;
    }
    // the XBE: given, else the disc's own, else the fallback (an extracted folder kept without it)
    bool opened = false;
    if (!xbe_path.empty()) {
        opened = open_xbe(disc.xbe, xbe_path, error);
        problem = opened ? problem : DiscProblem::missing;
    } else if (disc_exists(disc.source, "default.xbe")) {
        opened = open_xbe_bytes(disc.xbe, disc_file(disc.source, "default.xbe"), "default.xbe",
                                error);
        problem = opened ? problem : DiscProblem::damaged;
        error = opened ? error : path + ": its default.xbe can't be read";
    } else if (!xbe_fallback.empty() && !disc.source.image) {
        opened = open_xbe(disc.xbe, xbe_fallback, error);
        problem = opened ? problem : DiscProblem::not_xbox;
    } else {
        problem = DiscProblem::not_xbox;
        error = path + " has no default.xbe: it isn't a whole Xbox disc";
    }
    if (!opened || !check_release(disc.xbe, problem, error)) {
        return false;
    }
    for (int i = 0; i < xbe_at::cabinet_count; ++i) {
        const std::uint32_t main = xbe_u32(disc.xbe, xbe_at::cabinet_names + i * 8);
        disc.cabinet_names[i] = cabinet_stem(xbe_cstring(disc.xbe, main));
    }

    // AssetRecord: path, size, kind, descriptor, cabinet, offset
    for (int i = 0; i < xbe_at::asset_count; ++i) {
        const std::uint32_t at = xbe_at::asset_table + i * 0x18;
        AssetRecord record;
        const std::uint32_t record_path = xbe_u32(disc.xbe, at);
        if (record_path) {
            record.path = xbe_cstring(disc.xbe, record_path);
        }
        record.size = xbe_u32(disc.xbe, at + 4);
        record.kind = xbe_u32(disc.xbe, at + 8);
        const std::uint32_t descriptor = xbe_u32(disc.xbe, at + 12);
        if (descriptor) {
            const int words = record.kind == 3 ? 2 : 3;
            for (int w = 0; w < words; ++w) {
                record.descriptor.push_back(xbe_i32(disc.xbe, descriptor + w * 4));
            }
        }
        record.cabinet = xbe_i32(disc.xbe, at + 16);
        record.offset = xbe_u32(disc.xbe, at + 20);
        disc.assets.push_back(record);
    }
    // the disc's files are there: every cabinet
    for (const std::string& name : disc.cabinet_names) {
        if (!disc_exists(disc.source, "Media/binlink/" + name + ".bin")) {
            problem = DiscProblem::damaged;
            error = path + " is missing Media/binlink/" + name + ".bin";
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> read_asset(const Disc& disc, int id) {
    if (id < 0 || id >= static_cast<int>(disc.assets.size())) {
        return {};
    }
    const AssetRecord& record = disc.assets[id];
    const bool loose = record.cabinet < 0;
    const std::string path = loose ? disc_relative(record.path)
                                   : "Media/binlink/" + disc.cabinet_names[record.cabinet] + ".bin";
    std::vector<std::uint8_t> bytes;
    if (!disc_read(disc.source, path, loose ? 0 : record.offset, record.size, bytes)) {
        return {};
    }
    return bytes;
}

std::string asset_source(const Disc& disc, int id) {
    const AssetRecord& record = disc.assets[id];
    if (record.cabinet < 0) {
        return disc_relative(record.path);
    }
    char text[96];
    std::snprintf(text, sizeof text, "%s+0x%x", disc.cabinet_names[record.cabinet].c_str(),
                  record.offset);
    return text;
}
