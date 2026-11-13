// data/models/*.gltf read back into a ModelSet (the layout of tools/extract/gltf_write.cpp), for
// the cook.
#include "cook/cook_read.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

// the number after "key": in an extras object (the extractor writes plain JSON numbers)
double extras_number(const std::string& json, const char* key, double fallback = 0) {
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t at = json.find(needle);
    return at == std::string::npos ? fallback
                                   : std::strtod(json.c_str() + at + needle.size(), nullptr);
}

// "key":[a,b,c...]
void extras_numbers(const std::string& json, const char* key, double* out, int count) {
    const std::string needle = std::string("\"") + key + "\":[";
    const std::size_t at = json.find(needle);
    if (at == std::string::npos) {
        return;
    }
    const char* cursor = json.c_str() + at + needle.size();
    for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        out[i] = std::strtod(cursor, &end);
        cursor = end + 1;
    }
}

std::string extras_of(const cgltf_data* data, const cgltf_extras& extras) {
    cgltf_size size = 0;
    cgltf_copy_extras_json(data, &extras, nullptr, &size);
    std::string json(size, '\0');
    if (size) {
        cgltf_copy_extras_json(data, &extras, json.data(), &size);
    }
    return json;
}

const cgltf_accessor* attribute(const cgltf_primitive& primitive, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == type) {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

void read_vertices(const cgltf_primitive& primitive, std::vector<MeshVertex>& out) {
    const cgltf_accessor* position = attribute(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normal = attribute(primitive, cgltf_attribute_type_normal);
    const cgltf_accessor* colour = attribute(primitive, cgltf_attribute_type_color);
    const cgltf_accessor* uv = attribute(primitive, cgltf_attribute_type_texcoord);
    if (!position) {
        return;
    }
    out.resize(position->count);
    for (cgltf_size i = 0; i < position->count; ++i) {
        MeshVertex& vertex = out[i];
        cgltf_accessor_read_float(position, i, vertex.position, 3);
        if (normal) {
            cgltf_accessor_read_float(normal, i, vertex.normal, 3);
        }
        float rgba[4] = {1, 1, 1, 1};
        if (colour) {
            cgltf_accessor_read_float(colour, i, rgba, 4);
        }
        for (int k = 0; k < 4; ++k) {
            vertex.colour[k] = static_cast<std::uint8_t>(rgba[k] * 255.0f + 0.5f);
        }
        if (uv) {
            cgltf_accessor_read_float(uv, i, vertex.uv, 2);
        }
    }
}

void read_section(const cgltf_data* data, const cgltf_primitive& primitive,
                  const std::filesystem::path& folder, ModelSection& section) {
    if (primitive.indices) {
        section.first_triangle = static_cast<std::uint32_t>(primitive.indices->offset / 6);
        section.triangle_count = static_cast<std::uint32_t>(primitive.indices->count / 3);
    }
    const cgltf_material* material = primitive.material;
    if (!material) {
        return;
    }
    const std::string json = extras_of(data, material->extras);
    double layer[4] = {}, tex[4] = {-1, -1, -1, -1};
    extras_numbers(json, "layer", layer, 4);
    extras_numbers(json, "tex", tex, 4);
    section.flags = static_cast<std::uint32_t>(extras_number(json, "flags"));
    for (int k = 0; k < 4; ++k) {
        section.layer[k] = static_cast<std::uint32_t>(layer[k]);
        section.tex[k] = static_cast<std::int32_t>(tex[k]);
    }
    section.ambient = static_cast<std::uint32_t>(extras_number(json, "ambient"));
    section.power = static_cast<float>(extras_number(json, "power"));
    double center[3] = {}, half[3] = {};
    extras_numbers(json, "center", center, 3);
    extras_numbers(json, "half", half, 3);
    for (int k = 0; k < 3; ++k) {
        section.center[k] = static_cast<float>(center[k]);
        section.half[k] = static_cast<float>(half[k]);
    }
    const cgltf_texture* texture = material->pbr_metallic_roughness.base_color_texture.texture;
    if (texture && texture->image && texture->image->uri) {
        section.picture = (folder / texture->image->uri).lexically_normal().generic_string();
    }
}

} // namespace

bool read_model_set(const std::string& data_dir, const std::string& relative, ModelSet& out,
                    std::string& error) {
    const std::string path = data_dir + "/" + relative;
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        error = "can't read " + path;
        cgltf_free(data);
        return false;
    }
    // pictures relative to data/, as the packs name their textures
    const std::filesystem::path folder = std::filesystem::path(relative).parent_path();
    const std::string set_extras = extras_of(data, data->extras);
    out.asset = static_cast<int>(extras_number(set_extras, "asset", -1));
    out.first_slot = static_cast<int>(extras_number(set_extras, "first_slot", -1));
    out.objects.clear();

    // each scene root is an object; its node tree comes after it, in the original's order
    const cgltf_scene& scene = data->scenes[0];
    for (cgltf_size r = 0; r < scene.nodes_count; ++r) {
        const cgltf_node* root = scene.nodes[r];
        ModelObject object;
        const std::string root_json = extras_of(data, root->extras);
        object.slot = static_cast<int>(extras_number(root_json, "slot", -1));
        double centre[3] = {};
        extras_numbers(root_json, "centre", centre, 3);
        for (int k = 0; k < 3; ++k) {
            object.centre[k] = static_cast<float>(centre[k]);
        }
        const cgltf_size root_index = static_cast<cgltf_size>(root - data->nodes);
        const cgltf_size end = r + 1 < scene.nodes_count
                                   ? static_cast<cgltf_size>(scene.nodes[r + 1] - data->nodes)
                                   : data->nodes_count;
        for (cgltf_size n = root_index + 1; n < end; ++n) {
            const cgltf_node& gnode = data->nodes[n];
            const std::string json = extras_of(data, gnode.extras);
            ModelNode node;
            node.parent = gnode.parent && gnode.parent != root
                              ? static_cast<int>(gnode.parent - data->nodes - root_index - 1)
                              : -1;
            for (int k = 0; k < 3; ++k) {
                node.translate[k] = gnode.translation[k];
                node.scale[k] = gnode.scale[k];
            }
            double euler[3] = {};
            extras_numbers(json, "euler", euler, 3);
            for (int k = 0; k < 3; ++k) {
                node.euler[k] = static_cast<float>(euler[k]);
            }
            node.flags = static_cast<std::uint32_t>(extras_number(json, "flags"));
            node.first_section = static_cast<int>(extras_number(json, "first_section"));
            node.opaque_count = static_cast<int>(extras_number(json, "opaque_count"));
            node.middle_count = static_cast<int>(extras_number(json, "middle_count"));
            double center[3] = {}, half[3] = {};
            extras_numbers(json, "center", center, 3);
            extras_numbers(json, "half", half, 3);
            for (int k = 0; k < 3; ++k) {
                node.center[k] = static_cast<float>(center[k]);
                node.half[k] = static_cast<float>(half[k]);
            }
            node.section_count = gnode.mesh ? static_cast<int>(gnode.mesh->primitives_count) : 0;
            if (gnode.mesh) {
                const int needed = node.first_section + node.section_count;
                if (static_cast<int>(object.sections.size()) < needed) {
                    object.sections.resize(needed);
                }
                for (cgltf_size p = 0; p < gnode.mesh->primitives_count; ++p) {
                    const cgltf_primitive& primitive = gnode.mesh->primitives[p];
                    read_section(data, primitive, folder, object.sections[node.first_section + p]);
                    if (object.vertices.empty()) {
                        read_vertices(primitive, object.vertices);
                    }
                    // the object's whole index list: the view every section's accessor points into
                    if (object.indices.empty() && primitive.indices &&
                        primitive.indices->buffer_view) {
                        const cgltf_buffer_view* view = primitive.indices->buffer_view;
                        object.indices.resize(view->size / 2);
                        std::memcpy(object.indices.data(),
                                    static_cast<const std::uint8_t*>(view->buffer->data) +
                                        view->offset,
                                    view->size);
                    }
                }
            }
            object.nodes.push_back(node);
        }
        out.objects.push_back(std::move(object));
    }
    cgltf_free(data);
    return true;
}
