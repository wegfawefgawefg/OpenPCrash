#pragma once
// The billboard queue as one view draws it (billboard_push, billboards_prepare,
// billboards_sort, billboards_draw; decomp src/hand/u_0bee60.c): this update's
// pushes at the frame's point between the last two updates, into view space with their modes
// decoded (pulled toward the camera, streak copies, domes kept round), back to front, those
// behind the camera dropped; then pass 10's draw with particle.vert and premultiplied.frag.
#include "effects/particles.hpp"
#include "render/d3dx_math.hpp"
#include "render/renderer.hpp"

#include <vector>

// particle.vert's Billboard (std430, 48 bytes)
struct Billboard {
    float pos[4];   // view space; w the flag, 0 additive
    float color[4]; // RGBA
    float cell[4];  // atlas cell u, v; cos x size, sin x size
};

// at most 0x988 a frame, streak copies included
inline constexpr int billboard_max = 0x988;

// view is the port's view matrix (port frame, z negated); t how far between the two updates
void billboards_prepare(const std::vector<BillboardPush>& pushes, float t, const Mat4& view,
                        std::vector<Billboard>& out);
// pass 10: one draw of every billboard over the atlas (texture slot 0x190's renderer texture)
void billboards_draw(const std::vector<Billboard>& billboards, int atlas, const Mat4& projection,
                     FrameDraws& draws);
