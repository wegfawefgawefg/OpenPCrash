#version 450
#extension GL_GOOGLE_include_directive : require
// seabump.xpu, bind row 0x14, with seabump.vert. No PS constants.
//
// Texture modes:
//   t0 project2d    the du/dv map (yurayuraTest.bmp for mesh 0x400000) at oT0
//   t1 bumpenvmap   (input t0) wave02.bmp at oT1 + M x du/dv, M = stage 1's bump matrix
// Combiners:
//   stage 0  r0.rgb = t1.rgb   r0.a = t1.a
// So: the water texture, rippled; the vertex colour is not used. Fog by D3D's final combiner.
// Not checkable until the renderer runs: how the NV2A reads du/dv from yurayuraTest.bmp, an unsigned texture
// (nv2a.glsl, nv_dudv).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 1, binding = 0, std140) uniform SeabumpDraw
{
    mat4 world_view_proj;
    vec4 bump_env;
    uvec4 dudv_signed;
}
draw;

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;

layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    // stage 0 du/dv, stage 1 offset by it
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec2 dudv = nv_dudv(t0, draw.dudv_signed.x != 0u);
    vec4 t1 = texture(tex1, nv_bumpenv(stage_uv(1, v_t1.xy), dudv, draw.bump_env));

    out_color = d3d_finish(t1, v_fog);
}
