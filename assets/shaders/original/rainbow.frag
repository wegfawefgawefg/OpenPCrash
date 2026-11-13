#version 450
#extension GL_GOOGLE_include_directive : require
// rainbow.xpu, bind row 0x19, with rainbow.vert. No PS constants.
//
// Texture modes: t0 project2d (muteki01.tga, bound by the constant handler), t1..t3 none.
// Combiners:
//   stage 0  r0.rgb = v0.rgb
//   stage 1  r0.a   = t0.a * v0.a
// So: the vertex's band colour, alpha = texture alpha x strength. Pass 9 blends SRC_ALPHA, INV_SRC_ALPHA (or additive
// with mesh 0x8000), no z write, fog off.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);

    vec4 r0;
    r0.rgb = v_d0.rgb;
    r0.a = nv_clamp(t0.a * v_d0.a);
    out_color = d3d_finish(r0, v_fog);
}
