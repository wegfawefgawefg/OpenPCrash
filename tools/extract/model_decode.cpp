#include "model_decode.hpp"

#include <cstring>

namespace {

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    bool ok = true;

    std::uint32_t u32(std::size_t at) {
        std::uint32_t value = 0;
        if (at + 4 > bytes.size()) {
            ok = false;
            return 0;
        }
        std::memcpy(&value, bytes.data() + at, 4);
        return value;
    }
    std::int32_t i32(std::size_t at) {
        return static_cast<std::int32_t>(u32(at));
    }
    float f32(std::size_t at) {
        const std::uint32_t bits = u32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    void vec3(std::size_t at, float out[3]) {
        for (int i = 0; i < 3; ++i) {
            out[i] = f32(at + i * 4);
        }
    }
};

} // namespace

std::vector<DiscObject> decode_model_set(const std::vector<std::uint8_t>& bytes,
                                         std::string& error) {
    Reader in{bytes};
    std::vector<DiscObject> objects;
    const std::uint32_t version = in.u32(0);
    const std::uint32_t count = in.u32(4);
    if (version != 0x80000101 && version != 0x80000102) {
        error = "not a model set";
        return {};
    }
    std::size_t at = 8;
    for (std::uint32_t o = 0; o < count && in.ok; ++o) {
        DiscObject object;
        object.flags = in.u32(at);
        const std::uint32_t vertex_count = in.u32(at + 4);
        const std::uint32_t triangle_count = in.u32(at + 0x10);
        const std::uint32_t section_count = in.u32(at + 0x14);
        const std::uint32_t node_count = in.u32(at + 0x18);
        for (int i = 0; i < 3; ++i) {
            object.header_1c[i] = in.u32(at + 0x1C + i * 4);
        }
        if (object.flags & 2) {
            error = "object " + std::to_string(o) + " has the compact header no retail file uses";
            return {};
        }
        // header 0x38, the D3DVertexBuffer, the D3DIndexBuffer when indexed
        std::size_t cursor = at + 0x38 + 12 + (triangle_count ? 12 : 0);
        if (cursor + vertex_count * 0x24 > bytes.size()) {
            error = "object " + std::to_string(o) + " vertices run past the file";
            return {};
        }
        object.vertices.resize(vertex_count);
        for (std::uint32_t v = 0; v < vertex_count; ++v) {
            std::memcpy(&object.vertices[v], bytes.data() + cursor + v * 0x24, 0x24);
        }
        cursor += vertex_count * 0x24;
        if (triangle_count) {
            object.indices.resize(triangle_count * 3);
            if (cursor + triangle_count * 6 > bytes.size()) {
                error = "object " + std::to_string(o) + " indices run past the file";
                return {};
            }
            std::memcpy(object.indices.data(), bytes.data() + cursor, triangle_count * 6);
            cursor += triangle_count * 6;
            // version 0x80000102 pads the index list to a multiple of four triangles
            if (version == 0x80000102 && triangle_count % 4) {
                cursor += (4 - triangle_count % 4) * 2;
            }
        }
        for (std::uint32_t s = 0; s < section_count; ++s, cursor += 0x5C) {
            DiscSection section;
            section.flags = in.u32(cursor);
            for (int k = 0; k < 4; ++k) {
                section.layer[k] = in.u32(cursor + 4 + k * 4);
                section.tex[k] = in.i32(cursor + 0x14 + k * 4);
            }
            section.argb = in.u32(cursor + 0x28);
            section.vertex_count = in.u32(cursor + 0x2C);
            section.triangle_count = in.u32(cursor + 0x30);
            section.strip = in.u32(cursor + 0x34);
            section.first_vertex = in.u32(cursor + 0x38);
            section.first_triangle = in.u32(cursor + 0x3C);
            section.power = in.f32(cursor + 0x40);
            in.vec3(cursor + 0x44, section.center);
            in.vec3(cursor + 0x50, section.half);
            object.sections.push_back(section);
        }
        for (std::uint32_t n = 0; n < node_count; ++n, cursor += 0x5C) {
            DiscNode node;
            node.flags = in.u32(cursor);
            node.section_count = in.i32(cursor + 4);
            node.opaque_count = in.i32(cursor + 8);
            node.middle_count = in.i32(cursor + 0xC);
            node.first_section = in.i32(cursor + 0x10);
            node.parent = in.i32(cursor + 0x14);
            node.child = in.i32(cursor + 0x18);
            node.sibling = in.i32(cursor + 0x1C);
            in.vec3(cursor + 0x20, node.translate);
            in.vec3(cursor + 0x2C, node.rotate);
            in.vec3(cursor + 0x38, node.scale);
            in.vec3(cursor + 0x44, node.center);
            in.vec3(cursor + 0x50, node.half);
            object.nodes.push_back(node);
        }
        if (!in.ok || cursor > bytes.size()) {
            error = "object " + std::to_string(o) + " tables run past the file";
            return {};
        }
        // every index inside the object, every section inside its ranges
        for (std::uint16_t index : object.indices) {
            if (index >= vertex_count) {
                error = "object " + std::to_string(o) + " has an index past its vertices";
                return {};
            }
        }
        for (const DiscSection& section : object.sections) {
            if ((section.first_triangle + section.triangle_count) * 3 > object.indices.size()) {
                error = "object " + std::to_string(o) + " has a section past its indices";
                return {};
            }
        }
        objects.push_back(std::move(object));
        at = cursor;
    }
    if (!in.ok) {
        error = "the set runs past the file";
        return {};
    }
    return objects;
}
