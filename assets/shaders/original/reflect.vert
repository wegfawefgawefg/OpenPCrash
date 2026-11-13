#version 450
#extension GL_GOOGLE_include_directive : require
// reflect.xvu, bind row 0x1A (shader_bind_reflect), with reflect.frag.
// Meshes with flag 0x1000000 (texture slot 0x18 forced on stage 1, wrap) or with SubMesh.layer[1] bit 0x80 (its own
// stage 1 texture): a reflection lookup into a small blurred map.
//
// Constants (shader_constants_reflect 0xC4625):
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c100       light             -Light.Direction, normalized, world space
//   c101       diffuse           Light.Diffuse, w 1
//   c102       ambient           Light.Ambient, w 1
//   c103       k                 (1, 0, 5, 0.5)
//   c104..c107 world_view        (World x View)^T
//   c108..c111 View^T: uploaded, never read
// Inputs: format A, v0 position, v1 normal, v3 uv. Outputs: oPos, oD0, oT0 = uv, oT1.xy. No oFog.
// Computes: colour = diffuse x max(Nv . light, 0) + ambient, with the view-space normal dotted with a world-space light
// (kept); oT1 = the object normal dotted with World x View's x and z columns, each normalized (the view-space x and z
// of the normal with the matrix's scale divided out), the reflection coordinate.
// Changed: the viewport epilogue is dropped; oFog is the assumed default (fog_factor of 0, no fog) although the mesh
// may be drawn with fog on; unwritten oT1.zw written as the default.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 3) in vec2 in_uv;     // v3

layout(set = 1, binding = 0, std140) uniform ReflectDraw
{
    mat4 world_view_proj; // c96..c99
    mat4 world_view;      // c104..c107
    vec4 light;           // c100
    vec4 diffuse;         // c101
    vec4 ambient;         // c102
    vec4 k;               // c103
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 6) out float v_fog;

void main()
{
    gl_Position = draw.world_view_proj * in_pos;
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_fog = d3d_fog_factor(NV2A_DEFAULT_FOG_COORD);

    // lit colour from the view-space normal
    vec3 normal_view = mat3(draw.world_view) * in_normal;
    float n_dot_l = max(dot(normal_view, draw.light.xyz), draw.k.y);
    v_d0 = clamp(vec4(draw.diffuse.rgb * n_dot_l + draw.ambient.rgb, draw.k.x), 0.0, 1.0);

    // reflection coordinate: N against World x View's normalized x and z columns
    mat3 columns = transpose(mat3(draw.world_view));
    vec3 x_axis = columns[0] * inversesqrt(dot(columns[0], columns[0]));
    vec3 z_axis = columns[2] * inversesqrt(dot(columns[2], columns[2]));
    v_t1 = vec4(dot(in_normal, x_axis), dot(in_normal, z_axis), NV2A_DEFAULT_TEXCOORD.zw);
}
