#version 450
#extension GL_GOOGLE_include_directive : require
// Fixed-function pre-transformed screen quads (no program in the original): FVF 0x104 (XYZRHW | TEX1) for
// fullscreen_quad 0xB6E80 and FVF 0x44 (XYZRHW | DIFFUSE) for shadow_screen_quad 0xB6B00. Also the port's own final
// gamma pass (gamma.frag).
//
// fullscreen_quad: corners (-1, 480), (-1, -1), (640, 480), (640, -1), rhw 1, z 0, uv in texels (0, 480), (0, 0),
//   (640, 480), (640, 0), drawn as a strip. The quad spans 641 x 481 pixels for 640 x 480 texels, so each pixel samples
//   between two texels (a slight softening, with whatever filter stage 0 has); reproduce the corners as given.
//   Used for: the backdrop copy at pass 5 (0x222 to 0x223), the pass 11 composite outside battle (0x223 times
//   scene_tint), and 0x225 to the back buffer after pass 16.
// shadow_screen_quad: corners (0, 480), (0, 0), (640, 480), (640, 0) in shadow_color.
// Inputs: v0 = (x, y, z, rhw) in the original's 640x480 pixels, colour, uv. Outputs: position, oD0, oT0.
// Computes: D3D screen pixels to clip space for a target that stands for 640x480 (Frame.linear_size), with D3D's pixel
// centres on integer coordinates moved onto Vulkan's half-integer ones (Frame.pixel.x, 0.5 assumed: the Xbox's own
// screen-space offset is not checked).

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;   // x, y, z, rhw
layout(location = 2) in vec4 in_color; // FVF 0x44 only; white for 0x104
layout(location = 3) in vec2 in_uv;    // FVF 0x104 only

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;

void main()
{
    vec2 pixel = in_pos.xy + frame.pixel.x;
    vec2 ndc = vec2(pixel.x * 2.0 * frame.linear_size.z - 1.0, 1.0 - pixel.y * 2.0 * frame.linear_size.w);
    gl_Position = vec4(ndc, in_pos.z, 1.0);
    v_d0 = clamp(in_color, 0.0, 1.0);
    v_t0 = vec4(in_uv, 0.0, 1.0);
}
