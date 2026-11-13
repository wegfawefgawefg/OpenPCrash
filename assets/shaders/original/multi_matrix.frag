#version 450
#extension GL_GOOGLE_include_directive : require
// multi_matrix.xpu, bind row 3, with multi_matrix.vert. No PS constants are set; c0 keeps the file's 0xFFFFFFFF.
//
// Texture modes: t0 project2d (the part's colour map), t1 project2d (the reflection map, slot 0x18), t2, t3 none.
// Combiners:
//   stage 0                                     t1.a = t1.a * t0.a
//   stage 1  r0.rgb = t1.rgb * t1.a + t0.rgb
//   stage 2  r0.rgb = r0.rgb * v0.rgb
//   stage 3                                     r0.a = c0.a (1)
// So: (colour map + reflection x reflection alpha x map alpha) x lit colour, alpha 1. Pass 4 draws without blending,
// alpha kill off, fog on, stencil pass ZERO (fighters clear the volume shadow count where they draw).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

const float C0_ALPHA = 1.0; // 0xFF in the file

void main()
{
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = texture(tex1, stage_uv(1, v_t1.xy));

    // combiner stages 0..3
    t1.a = nv_clamp(t1.a * t0.a);
    vec4 r0;
    r0.rgb = nv_clamp(t1.rgb * t1.a + t0.rgb);
    r0.rgb = nv_clamp(r0.rgb * v_d0.rgb);
    r0.a = C0_ALPHA;

    out_color = d3d_finish(r0, v_fog);
}
