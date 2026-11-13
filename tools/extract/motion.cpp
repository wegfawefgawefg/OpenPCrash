#include "motion.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

struct Stream {
    const std::vector<std::uint8_t>& bytes;
    std::uint32_t keys = 0;   // byte offset of the next key header, 0 for a null track
    std::uint32_t values = 0; // byte offset of the next value
    bool ok = true;

    std::uint16_t u16(std::uint32_t at) {
        if (at + 2 > bytes.size()) {
            ok = false;
            return 0;
        }
        return static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8);
    }
    float f32(std::uint32_t at) {
        float value = 0;
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0;
        }
        std::memcpy(&value, bytes.data() + at, 4);
        return value;
    }
};

// one curve as motion_curve_eval walks it: a u16 header (count & 0x1FFF, mode & 0xE000), count
// u16 times (none for mode 0), and the mode's run of floats; sign multiplies every float
std::string read_curve(Stream& in, float sign) {
    if (!in.keys) {
        return "(none)";
    }
    const std::uint16_t header = in.u16(in.keys);
    const int count = header & 0x1FFF;
    const int mode = header & 0xE000;
    int floats = 0;
    switch (mode) {
    case 0:
        floats = count ? 1 : 0;
        break;
    case 0x2000:
    case 0x4000:
    case 0x6000:
        floats = count + 1;
        break;
    case 0x8000:
    case 0xA000:
        floats = 2 * count + 1;
        break;
    case 0xC000:
        floats = 3 * count + 1;
        break;
    default: // 0xE000
        floats = 2 * count + 2;
        break;
    }
    std::string text = "(curve " + sx_hex(static_cast<std::uint32_t>(mode)) + " (times";
    in.keys += 2;
    if (mode != 0) {
        for (int i = 0; i < count; ++i) {
            text += " " + sx_int(in.u16(in.keys + i * 2));
        }
        in.keys += count * 2;
    }
    text += ") (values";
    for (int i = 0; i < floats; ++i) {
        const float value = in.f32(in.values + i * 4) * sign;
        text += " " + sx_float(value == 0.0f ? 0.0f : value);
    }
    in.values += floats * 4;
    return text + "))";
}

std::string read_group(Stream& in, const char* name, const float signs[3]) {
    std::string text = "\n      (" + std::string(name);
    for (int axis = 0; axis < 3; ++axis) {
        text += "\n        " + read_curve(in, signs[axis]);
    }
    return text + ")";
}

} // namespace

std::string motion_curve_text(const std::vector<std::uint8_t>& bytes, std::uint32_t& keys,
                              std::uint32_t& values, float sign, bool& ok) {
    Stream in{bytes, keys, values};
    std::string text = read_curve(in, sign);
    keys = in.keys;
    values = in.values;
    ok = ok && in.ok;
    return text;
}

namespace {

bool looks_like_motion(const std::vector<std::uint8_t>& bytes) {
    // the attachment tables are 392 bytes of 14 x 28-byte records; motion sets start with a track
    // count
    return bytes.size() != 392 && bytes.size() >= 12;
}

} // namespace

bool extract_motion(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    int sets = 0, warnings = 0;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        if (disc.assets[id].kind != 4 || !asset_wanted(options, id)) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = read_asset(disc, id);
        if (!looks_like_motion(bytes)) {
            continue;
        }
        auto u32 = [&](std::uint32_t at) {
            std::uint32_t value = 0;
            if (at + 4 <= bytes.size()) {
                std::memcpy(&value, bytes.data() + at, 4);
            }
            return value;
        };
        const std::uint32_t tracks = u32(0);
        // slots aren't stored: the slot table ends where the lowest clip starts (data.md section 7
        // says "the first nonzero offset", but the offsets aren't in order: 1727's first is
        // 0x13602)
        std::uint32_t first = 0;
        for (std::uint32_t at = 4; at < bytes.size() && (!first || at < first); at += 4) {
            const std::uint32_t offset = u32(at);
            if (offset && (!first || offset < first)) {
                first = offset;
            }
        }
        const int slots = first ? static_cast<int>((first - 4) / 4) : 0;

        std::string text = ";; motion set " + std::to_string(id) + " (" + asset_source(disc, id) +
                           "), port frame, see tools/extract/motion.hpp\n";
        text += "(motion-set (asset " + sx_int(id) + ") (slots " + sx_int(slots) + ") (tracks " +
                sx_int(tracks) + ")";
        // slot -> clip, clips shared by offset
        std::map<std::uint32_t, int> clips;
        std::string slot_list;
        for (int slot = 0; slot < slots; ++slot) {
            const std::uint32_t offset = u32(4 + slot * 4);
            if (offset && !clips.count(offset)) {
                const int clip = static_cast<int>(clips.size());
                clips[offset] = clip;
            }
            slot_list += " " + sx_int(offset ? clips[offset] : -1);
        }
        text += "\n  (slot-clips" + slot_list + ")";
        for (const auto& [offset, clip] : clips) {
            text += "\n  (clip " + sx_int(clip);
            for (std::uint32_t t = 0; t < tracks; ++t) {
                // MotionTrack: reserved, flags, keys, values
                const std::uint32_t at = offset + t * 16;
                const std::uint32_t flags = u32(at + 4);
                Stream in{bytes, u32(at + 8), u32(at + 12)};
                text += "\n    (track " + sx_int(t) + " (flags " + sx_hex(flags) + ")" +
                        (in.keys ? "" : " (null)");
                const float rotate_signs[3] = {1, -1, -1};
                const float translate_signs[3] = {1, -1, -1};
                const float step_signs[3] = {1, 1, 1};
                if (flags & 0x700) {
                    text += read_group(in, "rotate", rotate_signs);
                }
                if (flags & 0xE0) {
                    text += read_group(in, "translate", translate_signs);
                }
                if (flags & 0x3800) {
                    text += read_group(in, "steps", step_signs);
                    if (flags & 0xE0) {
                        ++warnings; // no reader reads both groups after rotation; order unknown
                    }
                }
                text += ")";
                if (!in.ok) {
                    std::fprintf(stderr, "motion: asset %d clip %d track %u runs past the file\n",
                                 id, clip, t);
                    ++warnings;
                }
            }
            text += ")";
        }
        text += ")\n";

        const std::string file = "motion/" + asset_folder_name(disc, id) + ".sexp";
        std::error_code made;
        std::filesystem::create_directories(options.out_dir + "/motion", made);
        std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
        out << text;
        if (!out) {
            std::fprintf(stderr, "motion: can't write %s\n", file.c_str());
            return false;
        }
        manifest.entries.push_back("(motion-set (asset " + sx_int(id) + ") (source " +
                                   sx_string(asset_source(disc, id)) + ") (slots " + sx_int(slots) +
                                   ") (clips " + sx_int(static_cast<long long>(clips.size())) +
                                   ") (file " + sx_string(file) + "))");
        ++sets;
    }
    std::printf("motion: %d sets, %d warnings\n", sets, warnings);
    return true;
}
