#pragma once
// A model set in the original's shape: per object (one model slot) its vertices, indices, sections
// and node tree. Positions are in the port frame; node angles are the exact ones the extractor
// kept in the glTF node extras. Cooked from data/models/*.gltf (src/cook/read_gltf.cpp) into
// packs, loaded from there.
#include "render/renderer.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct Pack;

struct ModelSection {
    std::uint32_t flags = 0; // the original's SubMesh flags (docs/port/render.md)
    std::uint32_t layer[4] = {};
    std::int32_t tex[4] = {};
    std::uint32_t ambient = 0; // ARGB
    float power = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    std::string picture; // texture stage 0's PNG, relative to data/ (the pack's texture key), or ""
    float center[3] = {}; // the box pass_draw_meshes culls the section by (model space)
    float half[3] = {};
};

struct ModelNode {
    int parent = -1;
    float translate[3] = {};
    float euler[3] = {}; // port frame, applied x, then y, then z
    float scale[3] = {1, 1, 1};
    int first_section = 0;
    int section_count = 0;
    int opaque_count = 0; // the draw groups model_render_section_range queues apart
    int middle_count = 0;
    std::uint32_t flags = 0;
    float center[3] = {}; // the box the node's queue entries cull by, and its depth key's point
    float half[3] = {};
};

struct ModelObject {
    int slot = -1;
    float centre[3] = {}; // what stage LODs measure distance from (object header +0x1C)
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<ModelSection> sections;
    std::vector<ModelNode> nodes; // in the original's order
};

struct ModelSet {
    int asset = -1;
    int first_slot = -1;
    std::vector<ModelObject> objects;
};

// the model set `asset` from a pack
bool load_model_set(const Pack& pack, int asset, ModelSet& out, std::string& error);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, ModelSection& s) {
    a(s.flags);
    a(s.layer);
    a(s.tex);
    a(s.ambient);
    a(s.power);
    a(s.first_triangle);
    a(s.triangle_count);
    a(s.picture);
    a(s.center);
    a(s.half);
}
template <class A> void io(A& a, ModelObject& o) {
    a(o.slot);
    a(o.centre);
    a(o.vertices);
    a(o.indices);
    a(o.sections);
    a(o.nodes);
}
template <class A> void io(A& a, ModelSet& s) {
    a(s.asset);
    a(s.first_slot);
    a(s.objects);
}
