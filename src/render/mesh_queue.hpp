#pragma once
// The original's draw queues (decomp docs/port/render.md, "Draw queues and sorting"): a model's
// node walk queueing each node's section groups with their sort keys, the sort, and the per-view
// box test every mesh and sub-mesh is culled by.
#include "render/scene_draw.hpp"

#include <vector>

// model_render_root and model_walk_nodes: the object's nodes under world, each
// node's sections queued as up to three records (opaque, middle, the rest), keyed by group with
// their distance from view 0's eye; mesh flag 1 queues without depth
void queue_object(const ModelObject& object, int mesh, int texture_base, const Mat4& world,
                  int pass, std::uint32_t flags, const Mat4& view, std::vector<PassItem>& out);

// records12_sort's order: opaque groups front to back, then the last groups and then the
// middle ones back to front; by whole distances and stable, where the original's 16-bit keys
// wrapped every 6.55 units and its quicksort left ties unordered (docs/bugs.md)
void sort_queue(std::vector<const PassItem*>& queue);

// box_on_screen: the box's eight corners through world x view x projection, kept unless
// all are past one clip plane (x, y and z in -1..1 after the divide); a zero box always passes
bool box_on_screen(const float center[3], const float half[3], const Mat4& world,
                   const Mat4& view_projection);

// point_in_box, open on every side
bool point_in_box(const Vec3f& point, const float center[3], const float half[3]);
