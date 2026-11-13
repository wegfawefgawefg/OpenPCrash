#include "gltf_write.hpp"

#include "sexp_out.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

namespace {

std::string num(float value) {
    return std::isfinite(value) ? sx_float(value) : "0";
}

std::string vec(const float* v, int n) {
    std::string out = "[";
    for (int i = 0; i < n; ++i) {
        out += (i ? "," : "") + num(v[i]);
    }
    return out + "]";
}

// the port frame: z negated (positions, normals, translations, bounds centres)
void mirror_z(const float in[3], float out[3]) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = -in[2];
}

// node rotation in the port frame: the original's x, then y, then z rotation (D3DX matrices,
// which are the standard ones for column vectors) mirrored in z become (-x, -y, z), still applied
// x first; as a quaternion that is qz * qy * qx
void port_euler(const float rotate[3], float out[3]) {
    out[0] = -rotate[0];
    out[1] = -rotate[1];
    out[2] = rotate[2];
}

void euler_quaternion(const float e[3], float q[4]) {
    const double cx = std::cos(e[0] / 2), sx = std::sin(e[0] / 2);
    const double cy = std::cos(e[1] / 2), sy = std::sin(e[1] / 2);
    const double cz = std::cos(e[2] / 2), sz = std::sin(e[2] / 2);
    // qz * qy
    const double yzw = cz * cy, yzx = -sz * sy, yzy = cz * sy, yzz = sz * cy;
    // (qz * qy) * qx
    q[3] = static_cast<float>(yzw * cx - yzx * sx);
    q[0] = static_cast<float>(yzw * sx + yzx * cx);
    q[1] = static_cast<float>(yzy * cx + yzz * sx);
    q[2] = static_cast<float>(yzz * cx - yzy * sx);
}

// glTF wrap modes from the original's layer word (u: 0x10 wrap, 4 mirror, else clamp; v: 0x20, 8)
int wrap_mode(std::uint32_t layer, std::uint32_t wrap_bit, std::uint32_t mirror_bit) {
    return layer & wrap_bit ? 10497 : layer & mirror_bit ? 33648 : 33071;
}

void append(std::vector<std::uint8_t>& bin, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    bin.insert(bin.end(), bytes, bytes + size);
    while (bin.size() % 4) {
        bin.push_back(0);
    }
}

struct Json {
    std::vector<std::string> buffer_views, accessors, meshes, materials, nodes, images, samplers,
        textures;
};

int add_view(Json& json, std::size_t offset, std::size_t length, int target) {
    json.buffer_views.push_back("{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
                                ",\"byteLength\":" + std::to_string(length) +
                                ",\"target\":" + std::to_string(target) + "}");
    return static_cast<int>(json.buffer_views.size()) - 1;
}

int add_accessor(Json& json, int view, std::size_t offset, int component, std::size_t count,
                 const char* type, bool normalized, const std::string& bounds) {
    std::string text = "{\"bufferView\":" + std::to_string(view) +
                       ",\"byteOffset\":" + std::to_string(offset) +
                       ",\"componentType\":" + std::to_string(component) +
                       ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"";
    if (normalized) {
        text += ",\"normalized\":true";
    }
    json.accessors.push_back(text + bounds + "}");
    return static_cast<int>(json.accessors.size()) - 1;
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += (i ? ",\n" : "\n") + items[i];
    }
    return out;
}

} // namespace

bool write_model_gltf(const std::string& path, const std::vector<DiscObject>& objects,
                      const GltfModelInfo& info, std::string& error) {
    Json json;
    std::vector<std::uint8_t> bin;
    std::map<std::pair<int, int>, int> textures; // (picture, sampler) -> texture
    std::map<int, int> samplers;                 // wrap pair code -> sampler
    std::vector<int> scene_roots;
    bool unlit_used = false;

    for (std::size_t o = 0; o < objects.size(); ++o) {
        const DiscObject& object = objects[o];
        // vertex streams in the port frame
        std::vector<float> positions, normals, uvs;
        std::vector<std::uint8_t> colours;
        float low[3] = {1e30f, 1e30f, 1e30f}, high[3] = {-1e30f, -1e30f, -1e30f};
        for (const DiscVertex& vertex : object.vertices) {
            float p[3], n[3];
            mirror_z(vertex.position, p);
            mirror_z(vertex.normal, n);
            for (int k = 0; k < 3; ++k) {
                positions.push_back(p[k]);
                normals.push_back(n[k]);
                low[k] = std::fmin(low[k], p[k]);
                high[k] = std::fmax(high[k], p[k]);
            }
            uvs.push_back(vertex.uv[0]);
            uvs.push_back(vertex.uv[1]);
            colours.push_back(static_cast<std::uint8_t>(vertex.argb >> 16));
            colours.push_back(static_cast<std::uint8_t>(vertex.argb >> 8));
            colours.push_back(static_cast<std::uint8_t>(vertex.argb));
            colours.push_back(static_cast<std::uint8_t>(vertex.argb >> 24));
        }
        int position_accessor = -1, normal_accessor = -1, colour_accessor = -1, uv_accessor = -1,
            index_view = -1;
        const std::size_t vertex_count = object.vertices.size();
        if (vertex_count) {
            std::size_t at = bin.size();
            append(bin, positions.data(), positions.size() * 4);
            position_accessor = add_accessor(
                json, add_view(json, at, positions.size() * 4, 34962), 0, 5126, vertex_count,
                "VEC3", false, ",\"min\":" + vec(low, 3) + ",\"max\":" + vec(high, 3));
            at = bin.size();
            append(bin, normals.data(), normals.size() * 4);
            normal_accessor = add_accessor(json, add_view(json, at, normals.size() * 4, 34962), 0,
                                           5126, vertex_count, "VEC3", false, "");
            at = bin.size();
            append(bin, colours.data(), colours.size());
            colour_accessor = add_accessor(json, add_view(json, at, colours.size(), 34962), 0, 5121,
                                           vertex_count, "VEC4", true, "");
            at = bin.size();
            append(bin, uvs.data(), uvs.size() * 4);
            uv_accessor = add_accessor(json, add_view(json, at, uvs.size() * 4, 34962), 0, 5126,
                                       vertex_count, "VEC2", false, "");
        }
        if (!object.indices.empty()) {
            const std::size_t at = bin.size();
            append(bin, object.indices.data(), object.indices.size() * 2);
            index_view = add_view(json, at, object.indices.size() * 2, 34963);
        }

        // one material and one primitive per section
        std::vector<std::string> primitives(object.sections.size());
        for (std::size_t s = 0; s < object.sections.size(); ++s) {
            const DiscSection& section = object.sections[s];
            std::string material =
                "{\"name\":\"object" + std::to_string(o) + "_section" + std::to_string(s) + "\"";
            int picture = info.textures.part_colour ? 0 : section.tex[0];
            if (info.textures.dir_uri.empty() || picture < 0 || picture >= info.textures.count) {
                picture = -1;
            }
            std::string pbr =
                "\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1";
            if (picture >= 0) {
                const int wrap_code = wrap_mode(section.layer[0], 0x10, 4) * 100000 +
                                      wrap_mode(section.layer[0], 0x20, 8);
                if (!samplers.count(wrap_code)) {
                    json.samplers.push_back("{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":" +
                                            std::to_string(wrap_code / 100000) + ",\"wrapT\":" +
                                            std::to_string(wrap_code % 100000) + "}");
                    samplers[wrap_code] = static_cast<int>(json.samplers.size()) - 1;
                }
                const std::pair<int, int> key{picture, samplers[wrap_code]};
                if (!textures.count(key)) {
                    char uri[256];
                    std::snprintf(uri, sizeof uri, "%s/%02d.png", info.textures.dir_uri.c_str(),
                                  picture);
                    json.images.push_back(std::string("{\"uri\":\"") + uri + "\"}");
                    json.textures.push_back(
                        "{\"source\":" + std::to_string(json.images.size() - 1) +
                        ",\"sampler\":" + std::to_string(key.second) + "}");
                    textures[key] = static_cast<int>(json.textures.size()) - 1;
                }
                pbr += ",\"baseColorTexture\":{\"index\":" + std::to_string(textures[key]) + "}";
            }
            material += "," + pbr + "}";
            if (section.flags & 0x800) {
                material += ",\"doubleSided\":true";
            }
            if (section.flags & 0x4000) {
                material += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5";
            } else if (section.layer[0] & 0x40 || section.flags & 0x200) {
                material += ",\"alphaMode\":\"BLEND\"";
            }
            if (section.flags & 0x100) {
                material += ",\"extensions\":{\"KHR_materials_unlit\":{}}";
                unlit_used = true;
            }
            float centre[3];
            mirror_z(section.center, centre);
            material +=
                ",\"extras\":{\"flags\":" + std::to_string(section.flags) + ",\"layer\":[" +
                std::to_string(section.layer[0]) + "," + std::to_string(section.layer[1]) + "," +
                std::to_string(section.layer[2]) + "," + std::to_string(section.layer[3]) +
                "],\"tex\":[" + std::to_string(section.tex[0]) + "," +
                std::to_string(section.tex[1]) + "," + std::to_string(section.tex[2]) + "," +
                std::to_string(section.tex[3]) + "],\"ambient\":" + std::to_string(section.argb) +
                ",\"power\":" + num(section.power) + ",\"center\":" + vec(centre, 3) +
                ",\"half\":" + vec(section.half, 3) +
                (info.textures.part_colour ? ",\"part_colour\":true" : "") + "}}";
            json.materials.push_back(material);
            const int material_index = static_cast<int>(json.materials.size()) - 1;

            std::string primitive =
                "{\"attributes\":{\"POSITION\":" + std::to_string(position_accessor) +
                ",\"NORMAL\":" + std::to_string(normal_accessor) +
                ",\"COLOR_0\":" + std::to_string(colour_accessor) +
                ",\"TEXCOORD_0\":" + std::to_string(uv_accessor) + "}";
            if (index_view >= 0 && section.triangle_count) {
                const int indices = add_accessor(json, index_view, section.first_triangle * 6, 5123,
                                                 section.triangle_count * 3, "SCALAR", false, "");
                primitive += ",\"indices\":" + std::to_string(indices);
            }
            primitives[s] =
                primitive + ",\"material\":" + std::to_string(material_index) + ",\"mode\":4}";
        }

        // the object's root, then its node tree (node i of the object is glTF node base + 1 + i)
        const int base = static_cast<int>(json.nodes.size());
        std::string root = "{\"name\":\"object" + std::to_string(o);
        if (info.first_slot >= 0) {
            root += " slot" + std::to_string(info.first_slot + static_cast<int>(o));
        }
        // header +0x1C: the object's centre, which stage_render_lods 0x6E127 measures LOD distance
        // from
        float header_raw[3], header_centre[3];
        std::memcpy(header_raw, object.header_1c, sizeof header_raw);
        mirror_z(header_raw, header_centre);
        root += "\",\"extras\":{\"object\":" + std::to_string(o) +
                ",\"centre\":" + vec(header_centre, 3) +
                ",\"flags\":" + std::to_string(object.flags) + ",\"slot\":" +
                std::to_string(info.first_slot >= 0 ? info.first_slot + static_cast<int>(o) : -1) +
                "}";
        std::string root_children;
        for (std::size_t n = 0; n < object.nodes.size(); ++n) {
            if (object.nodes[n].parent < 0) {
                root_children += (root_children.empty() ? "" : ",") + std::to_string(base + 1 + n);
            }
        }
        if (!root_children.empty()) {
            root += ",\"children\":[" + root_children + "]";
        }
        json.nodes.push_back(root + "}");
        scene_roots.push_back(base);
        for (std::size_t n = 0; n < object.nodes.size(); ++n) {
            const DiscNode& node = object.nodes[n];
            float t[3], e[3], q[4], centre[3];
            mirror_z(node.translate, t);
            port_euler(node.rotate, e);
            euler_quaternion(e, q);
            mirror_z(node.center, centre);
            std::string text = "{\"name\":\"node" + std::to_string(n) +
                               "\",\"translation\":" + vec(t, 3) + ",\"rotation\":" + vec(q, 4) +
                               ",\"scale\":" + vec(node.scale, 3);
            std::string children;
            for (std::size_t c = 0; c < object.nodes.size(); ++c) {
                if (object.nodes[c].parent == static_cast<int>(n)) {
                    children += (children.empty() ? "" : ",") + std::to_string(base + 1 + c);
                }
            }
            if (!children.empty()) {
                text += ",\"children\":[" + children + "]";
            }
            if (node.section_count > 0) {
                std::string mesh = "{\"name\":\"object" + std::to_string(o) + "_node" +
                                   std::to_string(n) + "\",\"primitives\":[";
                for (int s = 0; s < node.section_count; ++s) {
                    const std::size_t section = static_cast<std::size_t>(node.first_section + s);
                    if (section < primitives.size()) {
                        mesh += (s ? "," : "") + primitives[section];
                    }
                }
                json.meshes.push_back(mesh + "]}");
                text += ",\"mesh\":" + std::to_string(json.meshes.size() - 1);
            }
            // the exact angles (port frame, applied x, y, z) and the original's draw groups
            text += ",\"extras\":{\"index\":" + std::to_string(n) +
                    ",\"flags\":" + std::to_string(node.flags) + ",\"euler\":" + vec(e, 3) +
                    ",\"first_section\":" + std::to_string(node.first_section) +
                    ",\"section_count\":" + std::to_string(node.section_count) +
                    ",\"opaque_count\":" + std::to_string(node.opaque_count) +
                    ",\"middle_count\":" + std::to_string(node.middle_count) +
                    ",\"center\":" + vec(centre, 3) + ",\"half\":" + vec(node.half, 3) + "}}";
            json.nodes.push_back(text);
        }
    }

    // the JSON
    const std::filesystem::path gltf_path(path);
    std::filesystem::path bin_path = gltf_path;
    bin_path.replace_extension(".bin");
    std::string roots;
    for (std::size_t i = 0; i < scene_roots.size(); ++i) {
        roots += (i ? "," : "") + std::to_string(scene_roots[i]);
    }
    std::string text = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"pc_extract (phantomcrash "
                       "tools/extract)\"},\n";
    if (unlit_used) {
        text += "\"extensionsUsed\":[\"KHR_materials_unlit\"],\n";
    }
    text += "\"extras\":{\"asset\":" + std::to_string(info.asset) +
            ",\"first_slot\":" + std::to_string(info.first_slot) +
            ",\"frame\":\"original D3D with z negated\"},\n";
    text += "\"scene\":0,\"scenes\":[{\"nodes\":[" + roots + "]}],\n";
    text += "\"buffers\":[{\"uri\":\"" + bin_path.filename().string() +
            "\",\"byteLength\":" + std::to_string(bin.size()) + "}],\n";
    if (!json.buffer_views.empty()) {
        text += "\"bufferViews\":[" + join(json.buffer_views) + "],\n";
        text += "\"accessors\":[" + join(json.accessors) + "],\n";
    }
    if (!json.meshes.empty()) {
        text += "\"meshes\":[" + join(json.meshes) + "],\n";
        text += "\"materials\":[" + join(json.materials) + "],\n";
    }
    if (!json.images.empty()) {
        text += "\"images\":[" + join(json.images) + "],\n";
        text += "\"samplers\":[" + join(json.samplers) + "],\n";
        text += "\"textures\":[" + join(json.textures) + "],\n";
    }
    text += "\"nodes\":[" + join(json.nodes) + "]}\n";

    std::ofstream out_bin(bin_path, std::ios::binary);
    out_bin.write(reinterpret_cast<const char*>(bin.data()),
                  static_cast<std::streamsize>(bin.size()));
    std::ofstream out_gltf(gltf_path, std::ios::binary);
    out_gltf << text;
    if (!out_bin || !out_gltf) {
        error = "can't write " + path;
        return false;
    }
    return true;
}
