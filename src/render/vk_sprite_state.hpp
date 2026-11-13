#pragma once
// The sprite layer's state inside the renderer (vk_sprites.cpp): the vertex the corners are built
// into, the quads handed in, the runs they draw in, and the pipelines and pass
// (vk_sprite_pipeline.cpp makes those).
#include "render/vk_internal.hpp"
#include "render/vk_sprites.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

struct SpriteVertex {
    float position[2]; // clip space
    float uv[2];
    float colour[4];
    float k;    // 0 makes the sprite additive (flag 1)
    float blur; // the port's blurred backdrop copies: the radius in uv, 0 sharp
    float period[2]; // the blur's line periods in uv (u columns, v rows), 0 none
};

constexpr std::size_t sprite_max_quads = 16384;

struct SpriteItem {
    Quad2D quad;
    SlotTexture texture;
};

struct SpriteRun {
    int pass = 0;
    int view = -1;     // a split-screen view's rectangle, or the pass's own
    bool wide = false; // quad_full_window: the whole 640x480 stretched over the target
    std::uint8_t anchor = anchor_centre; // the clip (QuadAnchor)
    int texture = -1;
    bool linear = false;
    std::uint32_t first = 0; // vertex
    std::uint32_t count = 0;
};

struct SpriteState {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline scene_pipeline = VK_NULL_HANDLE; // pass 0, in the scene pass
    VkPipeline final_pipeline = VK_NULL_HANDLE; // passes 12, 14, 15, on the final target
    VkRenderPass load_pass = VK_NULL_HANDLE;    // the screen pass, loading what's there
    GpuBuffer vertices;
    std::vector<SpriteItem> quads;
    std::vector<SpriteRun> runs;
    float views[4][4] = {}; // the split-screen views (renderer_set_sprite_views)
    float drawn[4][4] = {}; // their 3D's rectangles
    int view_count = 0;
    bool failed = false;
};

// the two pipelines (the scene pass's, the screen passes'), the final target's loading pass and
// the vertex buffer; false on failure
bool vk_sprite_pipelines_create(RendererState& vk, SpriteState& s);
