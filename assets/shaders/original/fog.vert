#version 450
#extension GL_GOOGLE_include_directive : require
// fog.xvu, bind row 0x17 (shader_bind_fog), pixel side fixed function (ff_mesh.frag).
// The default for unlit meshes: pass_draw_meshes binds it when nothing earlier in its list matched, the sub-mesh is unlit
// and the mesh lacks flag 0x200.
//
// Constants (shader_constants_fog 0xC3E12; hardware register = API register + 96):
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c100..c103 world             World^T (only its y row, c101, is read)
//   c104       k                 (1 / D_00280604, 0, 1, 0.5): D_00280604 is 100 per render.md, so 1/100
//   c105..c107 light direction, diffuse, ambient: uploaded, never read
// Inputs: format A, v0 position, v2 colour, v3 uv. Outputs: oPos, oD0 = colour, oT0 = uv, oFog.
// Computes: height fog, oFog = clip z x (1 - clamp01(world y x k.x)): fog thins with height and is gone above 100 units.
// Changed: the viewport epilogue is dropped; oFog goes through the fog table here (frame.glsl). Unwritten oD1 and oT1..3
// are written as the assumed NV2A defaults so ff_mesh.frag can read them.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;   // v0
layout(location = 2) in vec4 in_color; // v2
layout(location = 3) in vec2 in_uv;    // v3

layout(set = 1, binding = 0, std140) uniform FogDraw
{
    mat4 world_view_proj; // c96..c99
    mat4 world;           // c100..c103
    vec4 k;               // c104
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
    // clip position, colour and uv straight through
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;
    v_d0 = clamp(in_color, 0.0, 1.0);
    v_t0 = vec4(in_uv, 0.0, 1.0);

    // fog falls off with world height
    float world_y = (draw.world * in_pos).y;
    float height = min(max(world_y * draw.k.x, draw.k.y), draw.k.z);
    v_fog = d3d_fog_factor(clip.z * (draw.k.z - height));

    // registers the program never writes
    v_d1 = vec4(0.0);
    v_t1 = NV2A_DEFAULT_TEXCOORD;
    v_t2 = NV2A_DEFAULT_TEXCOORD;
    v_t3 = NV2A_DEFAULT_TEXCOORD;
}
