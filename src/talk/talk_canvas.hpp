#pragma once
// Draw2D on the CPU: a 640x480 RGBA frame composited the way the original's overlay passes draw
// (point sampling, whole-pixel positions, alpha blending, passes 0 and 12 clipped to the 576x448
// title-safe viewport at 32,16), for the talk debug player and its headless captures. Stands in
// until the Vulkan renderer draws Draw2D itself.
#include "talk/draw2d.hpp"
#include "talk/sprite_registry.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CanvasTexture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct TalkCanvas {
    int width = 640;
    int height = 480;
    std::vector<std::uint8_t> rgba;              // the frame, R G B A bytes
    std::map<std::string, CanvasTexture> loaded; // PNGs by path
};

// clears to clear (ARGB), sorts the list and draws it; slots resolve through bindings
void canvas_draw(TalkCanvas& canvas, const SlotBindings& bindings, Draw2D& draw,
                 std::uint32_t clear);
bool canvas_save_png(const TalkCanvas& canvas, const std::string& path, int scale = 1);
