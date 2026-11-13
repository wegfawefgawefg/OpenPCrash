#include "textures.hpp"

#include <cstring>

namespace {

std::uint32_t u32_at(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size()) {
        std::memcpy(&value, bytes.data() + at, 4);
    }
    return value;
}

// 5:6:5 to 8 bits a channel
void unpack_565(std::uint16_t c, int out[3]) {
    out[0] = ((c >> 11) & 31) * 255 / 31;
    out[1] = ((c >> 5) & 63) * 255 / 63;
    out[2] = (c & 31) * 255 / 31;
}

// one 8-byte colour block into a 4x4 of RGBA; dxt1 allows the 3-colour mode with transparent black
void decode_colour_block(const std::uint8_t* block, bool dxt1, std::uint8_t out[16][4]) {
    const std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | block[1] << 8);
    const std::uint16_t c1 = static_cast<std::uint16_t>(block[2] | block[3] << 8);
    int colours[4][4];
    unpack_565(c0, colours[0]);
    unpack_565(c1, colours[1]);
    colours[0][3] = colours[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; ++k) {
            colours[2][k] = (2 * colours[0][k] + colours[1][k]) / 3;
            colours[3][k] = (colours[0][k] + 2 * colours[1][k]) / 3;
        }
        colours[2][3] = colours[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            colours[2][k] = (colours[0][k] + colours[1][k]) / 2;
            colours[3][k] = 0;
        }
        colours[2][3] = 255;
        colours[3][3] = 0;
    }
    const std::uint32_t bits =
        static_cast<std::uint32_t>(block[4] | block[5] << 8 | block[6] << 16 | block[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const int pick = (bits >> (i * 2)) & 3;
        for (int k = 0; k < 4; ++k) {
            out[i][k] = static_cast<std::uint8_t>(colours[pick][k]);
        }
    }
}

// DXT5's interpolated alpha block
void decode_alpha_block(const std::uint8_t* block, std::uint8_t out[16][4]) {
    int alphas[8];
    alphas[0] = block[0];
    alphas[1] = block[1];
    if (alphas[0] > alphas[1]) {
        for (int k = 1; k < 7; ++k) {
            alphas[k + 1] = ((7 - k) * alphas[0] + k * alphas[1]) / 7;
        }
    } else {
        for (int k = 1; k < 5; ++k) {
            alphas[k + 1] = ((5 - k) * alphas[0] + k * alphas[1]) / 5;
        }
        alphas[6] = 0;
        alphas[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int b = 0; b < 6; ++b) {
        bits |= static_cast<std::uint64_t>(block[2 + b]) << (8 * b);
    }
    for (int i = 0; i < 16; ++i) {
        out[i][3] = static_cast<std::uint8_t>(alphas[(bits >> (i * 3)) & 7]);
    }
}

void decode_dxt(const std::uint8_t* data, int width, int height, bool dxt5,
                std::vector<std::uint8_t>& rgba) {
    rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const int blocks_x = (width + 3) / 4;
    const int blocks_y = (height + 3) / 4;
    const int block_bytes = dxt5 ? 16 : 8;
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            const std::uint8_t* block = data + (by * blocks_x + bx) * block_bytes;
            std::uint8_t texels[16][4];
            decode_colour_block(dxt5 ? block + 8 : block, !dxt5, texels);
            if (dxt5) {
                decode_alpha_block(block, texels);
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + i % 4;
                const int y = by * 4 + i / 4;
                if (x < width && y < height) {
                    std::memcpy(&rgba[(static_cast<std::size_t>(y) * width + x) * 4], texels[i], 4);
                }
            }
        }
    }
}

// Morton (swizzled) order: the bits of x and y interleaved, x first, while each still has room
void swizzle_masks(int width, int height, std::uint32_t& mask_x, std::uint32_t& mask_y) {
    mask_x = mask_y = 0;
    std::uint32_t bit = 1;
    for (int at = 1; at < width || at < height; at <<= 1) {
        if (at < width) {
            mask_x |= bit;
            bit <<= 1;
        }
        if (at < height) {
            mask_y |= bit;
            bit <<= 1;
        }
    }
}

// the swizzled texel index of every linear (x, y), walked with the masks' carry trick
template <typename Visit> void walk_swizzled(int width, int height, Visit visit) {
    std::uint32_t mask_x = 0, mask_y = 0;
    swizzle_masks(width, height, mask_x, mask_y);
    std::uint32_t off_y = 0;
    for (int y = 0; y < height; ++y) {
        std::uint32_t off_x = 0;
        for (int x = 0; x < width; ++x) {
            visit(x, y, off_x + off_y);
            off_x = (off_x - mask_x) & mask_x;
        }
        off_y = (off_y - mask_y) & mask_y;
    }
}

} // namespace

std::vector<DecodedTexture> decode_texture_set(const std::vector<std::uint8_t>& bytes,
                                               std::string& error) {
    std::vector<DecodedTexture> textures;
    const std::uint32_t magic = u32_at(bytes, 0);
    if (magic != 0x80000101 && magic != 0x80000102) {
        error = "not a texture set";
        return textures;
    }
    const std::uint32_t count = u32_at(bytes, 4);
    if (count == 0 || 8 + count * 4 > bytes.size()) {
        error = "bad texture count";
        return textures;
    }
    // a palette table follows the header table when the first header doesn't start right after it
    const std::uint32_t table_end = 8 + count * 4;
    const bool palettes = u32_at(bytes, 8) != table_end;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t header = u32_at(bytes, 8 + i * 4);
        const std::uint32_t data = u32_at(bytes, header + 4) & 0x0FFFFFFF;
        const std::uint32_t format_word = u32_at(bytes, header + 12);
        const std::uint32_t size_word = u32_at(bytes, header + 16);
        DecodedTexture texture;
        const int format = (format_word >> 8) & 0xFF;
        texture.levels = static_cast<int>((format_word >> 16) & 0xF);
        if (size_word) {
            texture.width = static_cast<int>(size_word & 0xFFF) + 1;
            texture.height = static_cast<int>((size_word >> 12) & 0xFFF) + 1;
        } else {
            texture.width = 1 << ((format_word >> 20) & 0xF);
            texture.height = 1 << ((format_word >> 24) & 0xF);
        }
        const std::size_t texels = static_cast<std::size_t>(texture.width) * texture.height;
        if (format == 0x0C || format == 0x0F) {
            const bool dxt5 = format == 0x0F;
            texture.format = dxt5 ? "DXT5" : "DXT1";
            const std::size_t need = static_cast<std::size_t>((texture.width + 3) / 4) *
                                     ((texture.height + 3) / 4) * (dxt5 ? 16 : 8);
            if (data + need > bytes.size()) {
                error = "texture " + std::to_string(i) + " runs past the file";
                return {};
            }
            decode_dxt(bytes.data() + data, texture.width, texture.height, dxt5, texture.rgba);
        } else if (format == 0x06) {
            // A8R8G8B8, stored B G R A in memory
            texture.format = "A8R8G8B8";
            if (data + texels * 4 > bytes.size()) {
                error = "texture " + std::to_string(i) + " runs past the file";
                return {};
            }
            texture.rgba.assign(texels * 4, 0);
            walk_swizzled(texture.width, texture.height, [&](int x, int y, std::uint32_t from) {
                const std::uint8_t* src = bytes.data() + data + from * 4;
                std::uint8_t* dst =
                    &texture.rgba[(static_cast<std::size_t>(y) * texture.width + x) * 4];
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            });
        } else if (format == 0x0B) {
            // P8 with the set's palette: descriptor -> {Common, Data, Lock}, Data -> {count,
            // argb[]}
            texture.format = "P8";
            const std::uint32_t descriptor = palettes ? u32_at(bytes, table_end + i * 4) : 0;
            const std::uint32_t record = descriptor ? u32_at(bytes, descriptor + 4) : 0;
            const std::uint32_t entries = record ? u32_at(bytes, record) : 0;
            if (!entries || data + texels > bytes.size()) {
                error = "P8 texture " + std::to_string(i) + " has no palette";
                return {};
            }
            texture.rgba.assign(texels * 4, 0);
            walk_swizzled(texture.width, texture.height, [&](int x, int y, std::uint32_t from) {
                const std::uint32_t index = bytes[data + from];
                const std::uint32_t argb =
                    index < entries ? u32_at(bytes, record + 4 + index * 4) : 0;
                std::uint8_t* dst =
                    &texture.rgba[(static_cast<std::size_t>(y) * texture.width + x) * 4];
                dst[0] = static_cast<std::uint8_t>(argb >> 16);
                dst[1] = static_cast<std::uint8_t>(argb >> 8);
                dst[2] = static_cast<std::uint8_t>(argb);
                dst[3] = static_cast<std::uint8_t>(argb >> 24);
            });
        } else {
            error = "texture " + std::to_string(i) + " has format 0x" + std::to_string(format) +
                    ", not read";
            return {};
        }
        textures.push_back(std::move(texture));
    }
    return textures;
}

#include "disc.hpp"
#include "extract.hpp"
#include "png.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <filesystem>

bool extract_textures(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    int sets = 0, written = 0, failed = 0;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        const AssetRecord& record = disc.assets[id];
        if (record.kind != 3 || !asset_wanted(options, id)) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = read_asset(disc, id);
        std::string error;
        const std::vector<DecodedTexture> textures =
            bytes.empty() ? std::vector<DecodedTexture>{} : decode_texture_set(bytes, error);
        if (textures.empty()) {
            std::fprintf(stderr, "textures: asset %d: %s\n", id,
                         bytes.empty() ? "unreadable" : error.c_str());
            ++failed;
            continue;
        }
        const std::string folder = "textures/" + asset_folder_name(disc, id);
        std::error_code made;
        std::filesystem::create_directories(options.out_dir + "/" + folder, made);
        // (texture-set (asset id) (source ...) (slots first count) (dir ...) (texture ...)...)
        std::string entry = "(texture-set (asset " + sx_int(id) + ") (source " +
                            sx_string(asset_source(disc, id)) + ")";
        if (record.descriptor.size() == 2) {
            entry += " (slots " + sx_int(record.descriptor[0]) + " " +
                     sx_int(record.descriptor[1]) + ")";
        }
        entry += " (dir " + sx_string(folder) + ")";
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const DecodedTexture& texture = textures[i];
            char name[16];
            std::snprintf(name, sizeof name, "%02zu.png", i);
            if (!write_png(options.out_dir + "/" + folder + "/" + name, texture.width,
                           texture.height, texture.rgba)) {
                std::fprintf(stderr, "textures: can't write %s/%s\n", folder.c_str(), name);
                return false;
            }
            ++written;
            entry += "\n  (texture (index " + sx_int(static_cast<long long>(i)) + ") (file " +
                     sx_string(name) + ") (size " + sx_int(texture.width) + " " +
                     sx_int(texture.height) + ") (format " + texture.format + ") (disc-levels " +
                     sx_int(texture.levels) + "))";
        }
        entry += ")";
        manifest.entries.push_back(entry);
        ++sets;
    }
    std::printf("textures: %d sets, %d pictures, %d sets unread\n", sets, written, failed);
    return true;
}
