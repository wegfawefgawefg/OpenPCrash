#version 450
#extension GL_GOOGLE_include_directive : require
// seabump.xvu, bind row 0x14 (shader_bind_seabump), with seabump.frag.
// Meshes whose SubMesh.layer[0] has bit 0x800: the water. With mesh flag 0x400000 pass_draw_meshes puts
// yurayuraTest.bmp on stage 0 and wave02.bmp on stage 1 (both wrap) and sets the bump matrix (below); without it the
// stages hold the sub-mesh's own textures and whatever bump matrix the last water draw left.
//
// Constants (shader_constants_seabump 0xC3C2B):
//   c96..c99   world_view_proj   (World x View x Proj)^T
// Bump matrix (texture stage states 22..25 of stages 0 and 1, set in pass_draw_meshes, read by stage 1):
//   a = water_bump_angle x 3, M00 = cos a x 0.05, M01 = -sin a x 0.05, M10 = sin a x 0.05, M11 = cos a x 0.05.
//   water_bump_angle += 1/30 per water sub-mesh drawn, per view: the port must advance it per update instead.
// Inputs: format A, v0 position, v2 colour, v3 uv. Outputs: oPos, oFog = clip z, oD0 = colour, oT0 = oT1 = uv.
// Computes: a pass-through; the pixel shader does the work.
// Changed: the viewport epilogue is dropped.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;   // v0
layout(location = 2) in vec4 in_color; // v2
layout(location = 3) in vec2 in_uv;    // v3

layout(set = 1, binding = 0, std140) uniform SeabumpDraw
{
    mat4 world_view_proj; // c96..c99
    vec4 bump_env;        // stage 1's (M00, M01, M10, M11)
    uvec4 dudv_signed;    // x: stage 0 is a signed du/dv format (0 for yurayuraTest.bmp)
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 6) out float v_fog;

void main()
{
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;
    v_fog = d3d_fog_factor(clip.z);
    v_d0 = clamp(in_color, 0.0, 1.0);
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_t1 = vec4(in_uv, 0.0, 1.0);
}
