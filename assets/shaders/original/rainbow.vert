#version 450
#extension GL_GOOGLE_include_directive : require
// rainbow.xvu, bind row 0x19 (shader_bind_rainbow), with rainbow.frag.
// Meshes with flag 0x100000: the shield bubble (immunity_bubble_draw, pass 9, fog off, no z write).
//
// Constants (shader_constants_rainbow 0xC432E):
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c100       light             -Light.Direction, normalized, world space (dotted with the object-space normal: kept)
//   c101       diffuse           Light.Diffuse, w 1
//   c102       ambient           Light.Ambient, w 1
//   c103       k                 (1, 0, scroll, 0.5); scroll = D_00281740, -0.01 per draw call, wrapped into 0..1
//   c104       phase             (p - 0.6666, p, p + 0.6666, 2); p = D_00A69B24, -0.03 per draw call, reset to 1 below -1
//   c105       strength          (rainbow_strength_set value 0..1 from D_002805D8.f_30, 0, 0, 0)
// The handler also binds muteki01.tga (D_00A13E48) on stage 0 with V wrap. Both phases advance per draw call, so per
// view and per bubble: the port must advance them per update (decomp render.md, "Rendering at the display rate").
// Inputs: format A, v0 position, v1 normal. Outputs: oPos, oD0, oT0. No oFog (pass 9 has fog off).
// Computes: three triangle waves of the object-space height, one per channel and a third of a period apart, band(a) =
// |(|a| >= 1 ? 2 : 0) - |a||, each |x| computed as rcp(rsq(x * x)); colour = 0.75 x the lit colour + 0.5 x the bands,
// alpha = strength; uv = (P.x + 0.5, P.y + 0.5 + scroll).
// Changed: the viewport epilogue is dropped.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1

layout(set = 1, binding = 0, std140) uniform RainbowDraw
{
    mat4 world_view_proj; // c96..c99
    vec4 light;           // c100
    vec4 diffuse;         // c101
    vec4 ambient;         // c102
    vec4 k;               // c103
    vec4 phase;           // c104
    vec4 strength;        // c105
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 6) out float v_fog;

// One band: the triangle wave the program builds from sge and absolute values.
float band(float a)
{
    float mag = abs(a);
    float fold = (mag >= draw.k.x) ? draw.phase.w : 0.0;
    return abs(fold - mag);
}

void main()
{
    gl_Position = draw.world_view_proj * in_pos;
    v_fog = d3d_fog_factor(NV2A_DEFAULT_FOG_COORD);

    // lit colour, light and normal from different spaces as written
    float n_dot_l = max(dot(draw.light.xyz, in_normal), draw.k.y);
    vec3 lit = draw.diffuse.rgb * n_dot_l + draw.ambient.rgb;

    // the three bands of the height
    float y = in_pos.y;
    vec3 bands = vec3(band(draw.phase.x + y), band(draw.phase.y + y), band(draw.phase.z + y));

    // 0.25 lit + (0.5 lit + 0.5 bands), alpha the strength
    vec3 color = lit * draw.k.w * draw.k.w + (lit * draw.k.w + bands * draw.k.w);
    v_d0 = clamp(vec4(color, draw.strength.x), 0.0, 1.0);

    // scrolling uv from object x and y; z and w unwritten
    v_t0 = vec4(in_pos.x + draw.k.w, in_pos.y + draw.k.w + draw.k.z, NV2A_DEFAULT_TEXCOORD.zw);
}
