#version 450
#extension GL_GOOGLE_include_directive : require
// cockpitUV.xvu, bind row 0x18 (shader_bind_cockpit_uv), pixel side fixed function (ff_mesh.frag).
// Pass 11, the cockpit instrument models 0x4DD..0x4F1 (hud_draw_meters), when the sub-mesh has a stage 0 texture. Pass
// 11 draws with an identity view, z off, unlit, unsorted, after the scene composite, and fog is off by then.
//
// Constants (shader_constants_cockpit_uv 0xC4051):
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c104       uv                (0, 1, u offset, v offset): cockpit_uv_set's row for Mesh.skin - 0x4DD (row 0 if out
//                                of range), set by hud_update per update
//   c100..c103 World^T, c105..c107 light direction, diffuse, ambient: uploaded, never read
// Inputs: format A, v0 position, v2 colour, v3 uv. Outputs: oPos, oD0 = colour, oT0 = uv + offset, oFog = clip z.
// Changed: the viewport epilogue is dropped; unwritten oD1 and oT1..3 written as the assumed NV2A defaults.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;   // v0
layout(location = 2) in vec4 in_color; // v2
layout(location = 3) in vec2 in_uv;    // v3

layout(set = 1, binding = 0, std140) uniform CockpitDraw
{
    mat4 world_view_proj; // c96..c99
    vec4 uv;              // c104
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 1) out vec4 v_d1;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    // clip position and colour
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;
    v_d0 = clamp(in_color, 0.0, 1.0);
    v_fog = d3d_fog_factor(clip.z);

    // the instrument's scrolled uv (v3 is (u, v, 0, 1) as fetched)
    v_t0 = vec4(in_uv + draw.uv.zw, 0.0, 1.0);

    // registers the program never writes
    v_d1 = vec4(0.0);
    v_t1 = NV2A_DEFAULT_TEXCOORD;
    v_t2 = NV2A_DEFAULT_TEXCOORD;
    v_t3 = NV2A_DEFAULT_TEXCOORD;
}
