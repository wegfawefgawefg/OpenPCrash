#version 450
#extension GL_GOOGLE_include_directive : require
// multi_shadow.xvu, bind row 6 (shader_bind_multi_shadow), pixel side none: no fragment stage is needed.
// Pass 3, the fighters' shadow volume batches (part slots 9 and 10), skinned, never culled.
//
// Constants (shader_constants_multi_shadow 0xC25B7):
//   c96..c99    view_proj         (View x Projection)^T
//   c100        k                 (50, 0.5, 0, 1)
//   c101        light_dir         Light.Direction (not negated: the way the light travels), normalized, w 0
//   c102..c177  bones[19]         bone^T per bone: bone to world
// Inputs: format B, v0 position, v1 normal, v4.x bone x 4 (v2 and v3 are copied to oD0 and oT0, which nothing reads).
// Outputs: oPos.
// Computes: skins position and normal into world space; a vertex whose normal faces along the light direction
// (N . dir >= 0, the side away from the light) is pushed 50 units along it. The volume parts are authored as closed
// hulls; this turns them into shadow volumes without a silhouette search.
// Pipeline (shadow_volumes_draw 0xB71B0): colour writes off (the original blends ZERO, ONE), z test on, no z write,
// flat shading, stencil always passing, front faces INCR_WRAP and back faces DECR_WRAP, then the same pair again with z
// off and the operations swapped (always, for pass 3). Stencil ref 0x80 is set but no test reads it.
// Changed: the viewport epilogue is dropped; the unused oD0/oT0 copies are not written.

#include "include/d3d.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 4) in float in_bone;  // v4.x

layout(set = 1, binding = 0, std140) uniform MultiShadowDraw
{
    mat4 view_proj; // c96..c99
    mat4 bones[19]; // c102..c177
    vec4 k;         // c100
    vec4 light_dir; // c101
}
draw;

void main()
{
    // skin into world space
    mat4 bone = draw.bones[bone_index(in_bone)];
    vec3 normal_world = mat3(bone) * in_normal;
    vec3 pos_world = (bone * in_pos).xyz;

    // push the far side along the light
    float away = (dot(normal_world, draw.light_dir.xyz) >= draw.k.z) ? 1.0 : 0.0;
    vec3 pushed = draw.light_dir.xyz * (away * draw.k.x) + pos_world;

    gl_Position = draw.view_proj * vec4(pushed, in_pos.w);
}
