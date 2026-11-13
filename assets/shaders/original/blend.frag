#version 450
#extension GL_GOOGLE_include_directive : require
// Blend.xpu, bind row 5, with multi_optical.vert: the camouflage. No PS constants are set; c0 keeps 0xFFFFFFFF.
//
// Texture modes:
//   t0 project2d   the part's colour map (pass_draw_camo binds stage 0 only) at oT0
//   t1 project2d   the backdrop 0x222 (linear target, texel coordinates, point filter, clamp) at oT1
//   t2 passthru    clamp01(oT2) = (0, 0, 0, s)
//   t3 project2d   the reflection map, slot 0x18, at oT3
// Combiners:
//   stage 0  r1.rgb = t2.rgb                                  r1.a = t2.a (s)
//   stage 1                                                   t3.a = t0.a * t3.a
//   stage 2  r0.rgb = t3.rgb * t3.a + t0.rgb
//   stage 3  r1.rgb = u(r1.a) * r0.rgb + t1.rgb * inv(r1.a)   r1.a = u(r1.a) * r0.a + t1.a * inv(r1.a)
//   stage 4  r0 = r1 * v0
//   stage 5                                                   r0.a = c0.a (1)
// So: lerp(backdrop, colour map + reflection, s) x vertex colour, alpha 1. Outside the reveal sphere the fighter is a
// refraction of what is behind it; inside it fades back in. Fog on (pass_open_camo), z write on, no blending.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1; // backdrop 0x222
layout(set = 2, binding = 3) uniform sampler2D tex3; // slot 0x18

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

const float C0_ALPHA = 1.0; // 0xFF in the file

void main()
{
    // texture stages
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = texture(tex1, v_t1.xy * frame.linear_size.zw);
    vec4 t2 = nv_passthru(v_t2);
    vec4 t3 = texture(tex3, stage_uv(3, v_t3.xy));
    vec4 r0 = t0; // r0.a starts as t0.a

    // stages 0..2: s, colour map plus reflection
    vec4 r1 = t2;
    t3.a = nv_clamp(t0.a * t3.a);
    r0.rgb = nv_clamp(t3.rgb * t3.a + t0.rgb);

    // stage 3: backdrop where s = 0, the fighter where s = 1
    float s = r1.a;
    r1.rgb = nv_clamp(nv_u(s) * r0.rgb + t1.rgb * nv_inv(s));
    r1.a = nv_clamp(nv_u(s) * r0.a + t1.a * nv_inv(s));

    // stages 4, 5
    r0 = nv_clamp(r1 * v_d0);
    r0.a = C0_ALPHA;

    out_color = d3d_finish(r0, v_fog);
}
