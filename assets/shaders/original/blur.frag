#version 450
#extension GL_GOOGLE_include_directive : require
// blur.xpu, bind row 9 (shader_bind_blur), with screen_quad.vert: the composite's downsample, 640x480 scene ->
// 320x240 (quad +0x0C0), then 320x240 -> 160x120 (quad +0x180). The same texture on all four stages, linear filter,
// clamp. No PS constants are set; the four stage constants are the file's 0x40404040.
//
// Texture modes: t0..t3 project2d, one tap each.
// Combiners:
//   stage 0  r1 = t0 * c0
//   stage 1  r1 = t1 * c0 + r1
//   stage 2  r1 = t2 * c0 + r1
//   stage 3  r0 = t3 * c0 + r1          c0 = 0x40 / 255 in every channel
// So: a 4-tap box at plus and minus one texel diagonally, each tap bilinear, the weights summing to 256/255 (a 0.4%
// gain, then clamped). Fog is off for the composite. Stage 0's alpha kill is whatever the last mesh pass left (DrawState).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"
#include "include/screen_draw.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;
layout(set = 2, binding = 2) uniform sampler2D tex2;
layout(set = 2, binding = 3) uniform sampler2D tex3;

layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;

layout(location = 0) out vec4 out_color;

const vec4 TAP_WEIGHT = vec4(64.0 / 255.0); // 0x40404040 in the file

void main()
{
    vec4 t0 = texture(tex0, screen_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = texture(tex1, screen_uv(1, v_t1.xy));
    vec4 t2 = texture(tex2, screen_uv(2, v_t2.xy));
    vec4 t3 = texture(tex3, screen_uv(3, v_t3.xy));

    vec4 r1 = nv_clamp(t0 * TAP_WEIGHT);
    r1 = nv_clamp(t1 * TAP_WEIGHT + r1);
    r1 = nv_clamp(t2 * TAP_WEIGHT + r1);
    vec4 r0 = nv_clamp(t3 * TAP_WEIGHT + r1);

    out_color = d3d_finish(r0, 1.0);
}
