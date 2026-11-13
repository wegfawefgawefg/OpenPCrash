#include "render/mesh_queue.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// ModelRoot.pass: model_root_defaults sets 0x15 for every loaded model and nothing
// changes it, so every key carries it
constexpr std::uint32_t root_pass = 0x15;

struct Walk {
    const ModelObject& object;
    int mesh, texture_base, pass;
    std::uint32_t flags;
    const Mat4& view;
    std::vector<PassItem>& out;
};

// one record of count sections from the node's cursor
// decomp: model_render_section_range 0xD3489
void queue_range(Walk& walk, int node_index, const Mat4& world, int& cursor, int count,
                 float depth, int order) {
    const ModelNode& node = walk.object.nodes[node_index];
    const int first = node.first_section + cursor;
    cursor += count;
    if (first < 0 || first + count > static_cast<int>(walk.object.sections.size())) {
        return;
    }
    PassItem item;
    item.pass = walk.pass;
    item.distance = depth;
    MeshRecord& record = item.item;
    record.sections = &walk.object.sections[first];
    record.section_count = count;
    record.mesh = walk.mesh;
    record.texture_base = walk.texture_base;
    record.world = world;
    record.flags = walk.flags;
    for (int k = 0; k < 3; ++k) {
        record.center[k] = node.center[k];
        record.half[k] = node.half[k];
    }
    // the key's high bits order the groups: opaque, then the last group, then the middle one;
    // mesh flag 1 queues with the last group at no depth, ahead of every depth in it. The depth
    // itself is kept whole (sort_queue), not as the original's 16-bit key (docs/bugs.md)
    item.key = root_pass << 16;
    if (walk.flags & 1) {
        item.key |= 0x400000;
        item.distance = INFINITY;
    } else if (order) {
        item.key |= order == 1 ? 0x800000 : 0x400000;
    }
    walk.out.push_back(item);
}

void queue_groups(Walk& walk, int node_index, const Mat4& world, float depth) {
    const ModelNode& node = walk.object.nodes[node_index];
    int cursor = 0;
    if (node.opaque_count) {
        queue_range(walk, node_index, world, cursor, node.opaque_count, depth, 0);
    }
    if (node.middle_count) {
        queue_range(walk, node_index, world, cursor, node.middle_count, depth, 1);
    }
    const int rest = node.section_count - node.opaque_count - node.middle_count;
    if (rest > 0) {
        queue_range(walk, node_index, world, cursor, rest, depth, 2);
    }
}

// the node's first child and next sibling, by the loader's order (children in index order)
int first_child(const ModelObject& object, int node) {
    for (int n = node + 1; n < static_cast<int>(object.nodes.size()); ++n) {
        if (object.nodes[n].parent == node) {
            return n;
        }
    }
    return -1;
}

int next_sibling(const ModelObject& object, int node) {
    for (int n = node + 1; n < static_cast<int>(object.nodes.size()); ++n) {
        if (object.nodes[n].parent == object.nodes[node].parent) {
            return n;
        }
    }
    return -1;
}

// model_walk_nodes: each node keyed by its box centre's distance from the eye (the original took
// the length of the centre in world space plus the view's translation row; docs/bugs.md)
float view_distance(const Walk& walk, const ModelNode& node, const Mat4& world) {
    const Vec3f centre =
        transform_coord(Vec3f{node.center[0], node.center[1], node.center[2]}, world);
    return vec_length(transform_coord(centre, walk.view));
}

void walk_node(Walk& walk, int node_index, const Mat4& parent) {
    const ModelNode& node = walk.object.nodes[node_index];
    const Mat4 world = mat_multiply(node_local_matrix(node), parent);
    if (node.section_count) {
        queue_groups(walk, node_index, world, view_distance(walk, node, world));
    }
    const int child = first_child(walk.object, node_index);
    if (child >= 0) {
        walk_node(walk, child, world);
    }
    const int sibling = next_sibling(walk.object, node_index);
    if (sibling >= 0) {
        walk_node(walk, sibling, parent);
    }
}

} // namespace

// decomp: model_render_root 0xD26B3, model_walk_nodes 0xD32C5
void queue_object(const ModelObject& object, int mesh, int texture_base, const Mat4& world,
                  int pass, std::uint32_t flags, const Mat4& view, std::vector<PassItem>& out) {
    if (object.nodes.empty()) {
        return;
    }
    Walk walk{object, mesh, texture_base, pass, flags, view, out};
    // model_render_root: node 0 the same way (the original keyed it by its origin's view z)
    const Mat4 root = mat_multiply(node_local_matrix(object.nodes[0]), world);
    queue_groups(walk, 0, root, view_distance(walk, object.nodes[0], root));
    const int child = first_child(object, 0);
    if (child >= 0) {
        walk_node(walk, child, root);
    }
    const int sibling = next_sibling(object, 0);
    if (sibling >= 0) {
        walk_node(walk, sibling, world);
    }
}

// decomp: records12_sort 0xD2337
void sort_queue(std::vector<const PassItem*>& queue) {
    // by group, then opaque front to back and the other groups back to front, ties in queue order
    std::stable_sort(queue.begin(), queue.end(), [](const PassItem* a, const PassItem* b) {
        if (a->key != b->key) {
            return a->key < b->key;
        }
        return a->key & 0xC00000 ? a->distance > b->distance : a->distance < b->distance;
    });
}

// decomp: box_on_screen 0xD2D6C
bool box_on_screen(const float center[3], const float half[3], const Mat4& world,
                   const Mat4& view_projection) {
    if (half[0] == 0 && half[1] == 0 && half[2] == 0) {
        return true;
    }
    const Mat4 m = mat_multiply(world, view_projection);
    const float signs[8][3] = {{1, 1, 1},  {-1, 1, 1},  {1, -1, 1},  {1, 1, -1},
                               {-1, -1, 1}, {-1, 1, -1}, {1, -1, -1}, {-1, -1, -1}};
    float corners[8][3];
    for (int i = 0; i < 8; ++i) {
        const float x = center[0] + signs[i][0] * half[0];
        const float y = center[1] + signs[i][1] * half[1];
        const float z = center[2] + signs[i][2] * half[2];
        float clip[4];
        for (int c = 0; c < 4; ++c) {
            clip[c] = x * m.m[0][c] + y * m.m[1][c] + z * m.m[2][c] + m.m[3][c];
        }
        // |w|, and a zero w sends the corner to +-2 on each axis by sign
        const float w = std::fabs(clip[3]);
        for (int c = 0; c < 3; ++c) {
            corners[i][c] = w == 0 ? (clip[c] < 0 ? -2.0f : 2.0f) : clip[c] / w;
        }
    }
    // box_corners_in_depth: some corner on the inside of each plane
    auto some = [&](int axis, bool below_one) {
        for (const float* corner : corners) {
            if (below_one ? corner[axis] <= 1.0f : corner[axis] >= -1.0f) {
                return true;
            }
        }
        return false;
    };
    return some(2, false) && some(2, true) && some(0, true) && some(0, false) && some(1, true) &&
           some(1, false);
}

// decomp: point_in_box 0xD2CC0
bool point_in_box(const Vec3f& point, const float center[3], const float half[3]) {
    const float d[3] = {center[0] - point.x, center[1] - point.y, center[2] - point.z};
    for (int k = 0; k < 3; ++k) {
        if (!(d[k] < half[k] && -half[k] < d[k])) {
            return false;
        }
    }
    return true;
}
