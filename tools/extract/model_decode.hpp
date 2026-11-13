#pragma once
// Model sets (kind 0, *_mdl.bin), parsed as the loaders walk them (docs/port/data.md section 6),
// in the original's frame: D3D left-handed, row vectors.
#include <cstdint>
#include <string>
#include <vector>

struct DiscVertex {
    float position[3];
    float normal[3];
    std::uint32_t argb;
    float uv[2];
};

// SubMesh, 0x5C (include/render.h)
struct DiscSection {
    std::uint32_t flags = 0; // 2 indexed, 4 list, else strips; 0x40 specular, 0x100 unlit, 0x200
                             // additive, 0x800 two-sided, 0x4000 cutout
    std::uint32_t layer[4] = {};
    std::int32_t tex[4] = {};
    std::uint32_t argb = 0; // ambient
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t strip = 0;
    std::uint32_t first_vertex = 0;
    std::uint32_t first_triangle = 0;
    float power = 0;
    float center[3] = {};
    float half[3] = {};
};

// ModelNode, 0x5C
struct DiscNode {
    std::uint32_t flags = 0;
    std::int32_t section_count = 0;
    std::int32_t opaque_count = 0;
    std::int32_t middle_count = 0;
    std::int32_t first_section = 0;
    std::int32_t parent = -1;
    std::int32_t child = -1;
    std::int32_t sibling = -1;
    float translate[3] = {};
    float rotate[3] = {}; // radians, applied x, then y, then z (model_node_local_matrix 0xAA186)
    float scale[3] = {};
    float center[3] = {};
    float half[3] = {};
};

struct DiscObject {
    std::uint32_t flags = 0;
    std::uint32_t header_1c[3] =
        {}; // floats: the centre stage_render_lods measures from (slot +0x14)
    std::vector<DiscVertex> vertices;
    std::vector<std::uint16_t> indices; // 3 a triangle, absolute within the object, padding dropped
    std::vector<DiscSection> sections;
    std::vector<DiscNode> nodes;
};

std::vector<DiscObject> decode_model_set(const std::vector<std::uint8_t>& bytes,
                                         std::string& error);
