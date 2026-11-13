// Every 2D sprite the game draws, cut from its texture set: the render registry (3763 rows of a
// rectangle on a texture slot, docs/port/data.md section 16) resolved to the set loaded into that
// slot, written as data/ui/images/<set>/<id>.png, plus the standard images (section 13), and
// data/ui/images.sexp indexing them all for the screens and a later vector redraw.
#include "addresses.hpp"
#include "disc.hpp"
#include "manifest.hpp"
#include "png.hpp"
#include "sexp_out.hpp"
#include "textures.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace {

struct Source {
    int asset = -1;
    int texture = 0;
};

// "D:\Media\scroll\sonic_tex.bin" -> "sonic", a cabinet row -> its cabinet's name
std::string set_stem(const Disc& disc, int asset) {
    const AssetRecord& record = disc.assets[asset];
    if (record.cabinet >= 0) {
        return disc.cabinet_names[record.cabinet];
    }
    std::string stem = record.path.substr(record.path.find_last_of('\\') + 1);
    stem = stem.substr(0, stem.rfind('.'));
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_tex") == 0) {
        stem.resize(stem.size() - 4);
    }
    return stem;
}

// the set a non-portrait handle shows: the type 3 set whose descriptor {slot, count} covers it.
// Below the portrait slots every UI handle has exactly one such set.
std::map<int, std::vector<Source>> handle_sources(const Disc& disc) {
    std::map<int, std::vector<Source>> sources;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        const AssetRecord& record = disc.assets[id];
        if (record.kind != 3 || record.descriptor.size() != 2 ||
            record.descriptor[0] >= xbe_at::portrait_slot) {
            continue;
        }
        for (int i = 0; i < record.descriptor[1]; ++i) {
            sources[record.descriptor[0] + i].push_back({id, i});
        }
    }
    return sources;
}

// portrait sprites: each actor's expressions name PortraitSprite rows, whose base, phase and
// overlay are registry rows on handles 534/535 of that actor's set
std::map<int, int> portrait_assets(const Xbe& xbe) {
    std::map<int, int> assets;
    for (int actor = 0; actor < xbe_at::portrait_actor_count; ++actor) {
        const std::uint32_t row = xbe_at::portrait_actor_table + actor * 0x28;
        const int asset = xbe_i32(xbe, row);
        for (int e = 0; e < 9; ++e) {
            const int sprite_row = xbe_i32(xbe, row + 4 + e * 4);
            if (sprite_row < 0 || sprite_row >= xbe_at::portrait_sprite_count) {
                continue;
            }
            const std::uint32_t sprite = xbe_at::portrait_sprite_table + sprite_row * 0x1C;
            for (int k = 0; k < 5; ++k) {
                const int id = xbe_i32(xbe, sprite + k * 4);
                if (id >= 0 && !assets.count(id)) {
                    assets[id] = asset;
                }
            }
        }
    }
    return assets;
}

// a guess at what the picture is, for the redraw: large many-coloured cells are pre-rendered 3D
// or photographic, the rest flat art (text, panels, icons); some sets are known by their content
std::string art_guess(int handle, int width, int height, const std::vector<std::uint8_t>& rgba) {
    if (handle == 445 || handle == 446 || handle >= xbe_at::portrait_slot) {
        return "portrait";
    }
    if (handle == 475 || handle == 477) {
        return "jacket";
    }
    if (handle == 515) {
        return "paint";
    }
    // shaded pictures (the screens' pre-rendered backdrops, processed photos) have hundreds of
    // colours over a large opaque area, photos thousands; gradient buttons and logos stay under
    // both (measured on the sheets; a guess, not a rule)
    (void)width;
    (void)height;
    std::set<std::uint32_t> seen;
    int opaque = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i + 3] < 128) {
            continue;
        }
        ++opaque;
        seen.insert(static_cast<std::uint32_t>(rgba[i] << 16 | rgba[i + 1] << 8 | rgba[i + 2]));
    }
    const bool backdrop = opaque >= 100000 && seen.size() >= 200;
    return backdrop || seen.size() >= 3000 ? "render" : "flat";
}

std::string uses_text(const UiSpriteUses& uses, int id) {
    const auto found = uses.sprites.find(id);
    if (found == uses.sprites.end()) {
        return {};
    }
    std::set<std::string> screens;
    std::set<std::pair<std::string, std::string>> tables;
    for (const UiSpriteUse& use : found->second) {
        screens.insert(use.screen);
        tables.insert({use.screen, use.table});
    }
    std::string text = "\n  (screens";
    for (const std::string& screen : screens) {
        text += " " + screen;
    }
    text += ") (used-by";
    for (const auto& [screen, table] : tables) {
        text += " (" + screen + " " + table + ")";
    }
    return text + ")";
}

std::uint32_t le(const std::vector<std::uint8_t>& b, std::size_t at, int n) {
    std::uint32_t value = 0;
    for (int i = n - 1; i >= 0; --i) {
        value = value << 8 | (at + i < b.size() ? b[at + i] : 0);
    }
    return value;
}

// the standard images' two formats (SDL_image is built without them): uncompressed BMP, 24-bit or
// 8-bit with a palette, and uncompressed 32-bit TGA, either origin
bool decode_bmp_tga(const std::vector<std::uint8_t>& b, bool tga, int& width, int& height,
                    std::vector<std::uint8_t>& rgba) {
    int bits = 0;
    std::size_t data = 0, palette = 0;
    bool bottom_up = true;
    if (tga) {
        if (b.size() < 18 || b[2] != 2) {
            return false;
        }
        width = static_cast<int>(le(b, 12, 2));
        height = static_cast<int>(le(b, 14, 2));
        bits = b[16];
        bottom_up = !(b[17] & 0x20);
        data = 18 + b[0];
    } else {
        if (b.size() < 54 || b[0] != 'B' || b[1] != 'M' || le(b, 30, 4) != 0) {
            return false;
        }
        data = le(b, 10, 4);
        width = static_cast<int>(le(b, 18, 4));
        height = static_cast<int>(le(b, 22, 4));
        bits = static_cast<int>(le(b, 28, 2));
        palette = 14 + le(b, 14, 4);
    }
    if (bits != 8 && bits != 24 && bits != 32) {
        return false;
    }
    const std::size_t stride =
        tga ? static_cast<std::size_t>(width) * bits / 8
            : (static_cast<std::size_t>(width) * bits / 8 + 3) & ~std::size_t(3);
    if (data + stride * height > b.size()) {
        return false;
    }
    rgba.assign(static_cast<std::size_t>(width) * height * 4, 255);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = &b[data + stride * (bottom_up ? height - 1 - y : y)];
        for (int x = 0; x < width; ++x) {
            std::uint8_t* out = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            const std::uint8_t* in = bits == 8 ? &b[palette + row[x] * 4] : row + x * (bits / 8);
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            out[3] = bits == 32 ? in[3] : 255;
        }
    }
    return true;
}

// Media/systemdata: plain BMP and TGA files; four are loaded by the game, four never
bool write_standard_images(const Disc& disc, const std::string& out_dir, std::string& index) {
    struct Standard {
        const char* file;
        const char* use;
    };
    const Standard standards[] = {
        {"bump_light.tga", "shaders_init: the bump light texture"},
        {"muteki01.tga", "shaders_init: the shield bubble (rainbow shader, material 25)"},
        {"wave02.bmp",
         "device setup 0xB2AF0, bound only for model flag 0x400000, likely never shown"},
        {"yurayuraTest.bmp",
         "device setup 0xB2AF0, bound only for model flag 0x400000, likely never shown"},
        {"env0.bmp", "never loaded (name not in the XBE)"},
        {"kagero_a01.bmp", "never loaded (name not in the XBE)"},
        {"kagero_a02.bmp", "never loaded (name not in the XBE)"},
        {"hishakai_test1.tga", "never loaded (name not in the XBE)"},
    };
    std::error_code made;
    std::filesystem::create_directories(out_dir + "/ui/images/standard", made);
    for (const Standard& standard : standards) {
        const std::string source = "Media/systemdata/" + std::string(standard.file);
        const std::vector<std::uint8_t> bytes = disc_file(disc.source, source);
        std::string name = standard.file;
        const bool tga = name.substr(name.rfind('.')) == ".tga";
        int width = 0, height = 0;
        std::vector<std::uint8_t> pixels;
        if (!decode_bmp_tga(bytes, tga, width, height, pixels)) {
            std::fprintf(stderr, "ui: can't read %s\n", source.c_str());
            continue;
        }
        name = name.substr(0, name.rfind('.')) + ".png";
        if (!write_png(out_dir + "/ui/images/standard/" + name, width, height, pixels)) {
            return false;
        }
        index += "(standard-image (file " + sx_string("standard/" + name) + ") (source " +
                 sx_string(source) + ") (size " + sx_int(width) + " " + sx_int(height) + ") (use " +
                 sx_string(standard.use) + "))\n";
    }
    return true;
}

} // namespace

bool write_ui_images(const Disc& disc, const std::string& out_dir, const UiSpriteUses& uses,
                     Manifest& manifest) {
    const Xbe& xbe = disc.xbe;
    const std::map<int, std::vector<Source>> sources = handle_sources(disc);
    const std::map<int, int> portraits = portrait_assets(xbe);
    std::map<int, std::vector<DecodedTexture>> sets;
    struct SetSpan {
        int count = 0, first = 0, last = 0;
    };
    std::map<int, SetSpan> per_set;
    std::string index = ";; Every 2D image the original draws (pc_extract ui family, "
                        "docs/ui-assets.md): render registry sprites by id, the texture sets "
                        "they come from,\n;; then the standard images. set/asset/texture: where "
                        "the pixels come from; slot: the "
                        "texture handle the registry names; rect: texels on that sheet;\n;; art: a "
                        "guess (render = pre-rendered 3D or photographic, flat = text, panels, "
                        "icons; portrait, jacket, paint by set).\n";
    int written = 0, unresolved = 0;
    for (int id = 0; id < xbe_at::render_registry_count; ++id) {
        const std::uint32_t entry = xbe_at::render_registry + id * 8;
        const std::uint32_t rect = xbe_u32(xbe, entry);
        const int handle = xbe_i32(xbe, entry + 4);
        float r[6];
        for (int k = 0; k < 6; ++k) {
            r[k] = xbe_f32(xbe, rect + k * 4);
        }

        // which set and texture the handle shows
        Source source;
        const bool portrait = handle >= xbe_at::portrait_slot;
        if (portrait) {
            const auto actor = portraits.find(id);
            if (actor != portraits.end()) {
                source = {actor->second, handle - xbe_at::portrait_slot};
            }
        } else {
            const auto found = sources.find(handle);
            if (found != sources.end()) {
                source = found->second.front();
            }
        }
        std::string text = "(image (id " + sx_int(id) + ") (slot " + sx_int(handle) + ") (rect " +
                           sx_float(r[0]) + " " + sx_float(r[1]) + " " + sx_float(r[2]) + " " +
                           sx_float(r[3]) + ") (sheet " + sx_float(r[4]) + " " + sx_float(r[5]) +
                           ")";
        if (source.asset < 0) {
            index += text + " (unresolved \"no actor's portrait names it\"))\n";
            ++unresolved;
            continue;
        }
        if (!sets.count(source.asset)) {
            std::string error;
            sets[source.asset] = decode_texture_set(read_asset(disc, source.asset), error);
        }
        const std::vector<DecodedTexture>& textures = sets[source.asset];
        if (source.texture >= static_cast<int>(textures.size())) {
            index += text + " (unresolved \"texture missing from its set\"))\n";
            ++unresolved;
            continue;
        }

        // cut the rectangle (texels on the logical sheet, which is the stored size here)
        const DecodedTexture& sheet = textures[source.texture];
        const int left = std::max(0, static_cast<int>(std::lround(r[0])));
        const int top = std::max(0, static_cast<int>(std::lround(r[1])));
        const int right = std::min(sheet.width, static_cast<int>(std::lround(r[2])));
        const int bottom = std::min(sheet.height, static_cast<int>(std::lround(r[3])));
        const int width = right - left, height = bottom - top;
        if (width <= 0 || height <= 0) {
            index += text + " (unresolved \"empty rectangle\"))\n";
            ++unresolved;
            continue;
        }
        std::vector<std::uint8_t> cell(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            std::memcpy(&cell[static_cast<std::size_t>(y) * width * 4],
                        &sheet.rgba[(static_cast<std::size_t>(top + y) * sheet.width + left) * 4],
                        static_cast<std::size_t>(width) * 4);
        }
        const std::string stem = set_stem(disc, source.asset);
        const std::string folder = portrait ? "portraits/" + stem : stem;
        const std::string file = folder + "/" + sx_int(id) + ".png";
        std::error_code made;
        std::filesystem::create_directories(out_dir + "/ui/images/" + folder, made);
        if (!write_png(out_dir + "/ui/images/" + file, width, height, cell)) {
            std::fprintf(stderr, "ui: can't write images/%s\n", file.c_str());
            return false;
        }
        ++written;
        SetSpan& span = per_set[source.asset];
        span.first = span.count++ ? std::min(span.first, id) : id;
        span.last = std::max(span.last, id);
        index += text + "\n  (file " + sx_string(file) + ") (size " + sx_int(width) + " " +
                 sx_int(height) + ") (set " + sx_string(stem) + ") (asset " + sx_int(source.asset) +
                 ") (texture " + sx_int(source.texture) + ") (art " +
                 art_guess(handle, width, height, cell) + ")" + uses_text(uses, id) + ")\n";
    }
    // the sheets: where each comes from, its id range, and the code that loads it
    for (const auto& [asset, span] : per_set) {
        const AssetRecord& record = disc.assets[asset];
        std::string text =
            "(image-set (asset " + sx_int(asset) + ") (set " + sx_string(set_stem(disc, asset)) +
            ") (source " + sx_string(asset_source(disc, asset)) + ") (slots " +
            sx_int(record.descriptor[0]) + " " + sx_int(record.descriptor[1]) + ") (images " +
            sx_int(span.count) + ") (ids " + sx_int(span.first) + " " + sx_int(span.last) + ")";
        const auto loads = uses.sets.find(asset);
        if (loads != uses.sets.end()) {
            text += "\n  (loaded-by";
            for (const UiSpriteUse& load : loads->second) {
                text += " (" + (load.screen.empty() ? std::string("-") : load.screen) + " " +
                        load.table + ")";
            }
            text += ")";
        }
        index += text + ")\n";
        manifest.entries.push_back("(ui-image-set (asset " + sx_int(asset) + ") (source " +
                                   sx_string(asset_source(disc, asset)) + ") (images " +
                                   sx_int(span.count) + "))");
    }
    if (!write_standard_images(disc, out_dir, index)) {
        return false;
    }

    std::ofstream out(out_dir + "/ui/images.sexp", std::ios::binary);
    out << index;
    if (!out) {
        std::fprintf(stderr, "ui: can't write ui/images.sexp\n");
        return false;
    }
    manifest.entries.push_back("(ui-images (index \"ui/images.sexp\") (sprites " + sx_int(written) +
                               ") (unresolved " + sx_int(unresolved) + "))");
    std::printf("ui: %d sprite images from %zu texture sets, %d registry rows unresolved\n",
                written, per_set.size(), unresolved);
    return true;
}
