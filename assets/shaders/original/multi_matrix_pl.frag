#version 450
#extension GL_GOOGLE_include_directive : require
// multi_matrixPL.xpu, bind row 4, with multi_matrix_pl.vert. PS c1 = point colour, PS c2 = lamp colour (both a = 0);
// c0 keeps the file's 0xFFFFFFFF.
//
// Texture modes: t0 project2d (colour map), t1 project2d (reflection map, slot 0x18), t2 passthru, t3 passthru.
// Combiners:
//   stage 0                                                 t1.a = t1.a * t0.a
//   stage 1  r0.rgb = t1.rgb * t1.a + t0.rgb
//   stage 2  r1.rgb = t2.rgb * c1.rgb + t3.rgb * c2.rgb       r1.a = t2.a * c1.a + t3.a * c2.a
//   stage 3                                                 r1.a = c2.a (0)
//   stage 4  r1.rgb = r1.rgb + v0.rgb                         r1.a = r1.a + v0.a
//   stage 5  r0.rgb = r0.rgb * r1.rgb                         r0.a = r0.a * r1.a
//   stage 6                                                 r0.a = c0.a (1)
// So: (colour map + reflection) x clamp(point x point colour + lamp x lamp colour + lit colour), alpha 1.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 1, binding = 0, std140) uniform MultiMatrixPlDraw
{
    mat4 proj;
    mat4 bone_view[19];
    mat4 view;
    mat4 heading_inverse;
    vec4 light;
    vec4 k;
    vec4 diffuse;
    vec4 ambient;
    vec4 point_pos;
    vec4 lamp_pos;
    vec4 lamp_dir;
    vec4 point_color; // PS c1
    vec4 lamp_color;  // PS c2
}
draw;

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;

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
    vec4 t1 = texture(tex1, stage_uv(1, v_t1.xy));
    vec4 t2 = nv_passthru(v_t2);
    vec4 t3 = nv_passthru(v_t3);
    vec4 r0 = t0; // r0.a starts as t0.a

    // stages 0, 1: colour map plus reflection
    t1.a = nv_clamp(t1.a * t0.a);
    r0.rgb = nv_clamp(t1.rgb * t1.a + t0.rgb);

    // stages 2..4: the two light terms plus the lit colour
    vec4 r1;
    r1.rgb = nv_clamp(t2.rgb * draw.point_color.rgb + t3.rgb * draw.lamp_color.rgb);
    r1.a = draw.lamp_color.a;
    r1 = nv_clamp(r1 + v_d0);

    // stages 5, 6
    r0 = nv_clamp(r0 * r1);
    r0.a = C0_ALPHA;

    out_color = d3d_finish(r0, v_fog);
}
