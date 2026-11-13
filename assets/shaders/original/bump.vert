#version 450
#extension GL_GOOGLE_include_directive : require
// bump.xvu, bind row 2 (shader_bind_bump), with bump.frag.
// Meshes whose SubMesh.layer[0] has bit 2 and that have textures on stages 0 and 1 (checked before every other row
// except pass 11's cockpitUV).
//
// Constants (shader_constants_bump 0xC0C3E):
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c100       k                 (0.001, 0.5, 0, 1)
//   c101       light             -Light.Direction through World^-1, normalized, w = 0.25: the light in object space
//   c102..c105 inverse_world     (World^-1)^T
//   c106       eye               the camera position in world space (View^-1 row 3)
//   c107..c110 world             World^T
//   c111       zero, never read
// The handler also binds bump_light.tga (D_00A7B8D4) on stage 3 with clamp and linear filters, and advances a wave
// phase (D_00A69B1C) per draw call that no program reads.
// Inputs: format A, v0 position, v1 normal (unnormalized as stored), v3 uv. Outputs: oPos, oFog = clip z, oT0 = oT1 = uv,
// oT2 = the half vector and oT3 = the light, both in the tangent frame, for the texture-shader dot products.
// Computes: a tangent frame from N and up (0, 1, 0): T = cross(up, N), B = cross(N, T), each scaled by the other's
// inverse length (the program pairs the rsq results that way; the same as normalizing while |N| = 1). The light is
// taken through World^-1 a second time although the CPU already put it in object space (kept); the eye vector goes
// from world to object space through World^-1 once.
// Changed: the viewport epilogue is dropped.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 3) in vec2 in_uv;     // v3

layout(set = 1, binding = 0, std140) uniform BumpDraw
{
    mat4 world_view_proj; // c96..c99
    mat4 inverse_world;   // c102..c105
    mat4 world;           // c107..c110
    vec4 k;               // c100
    vec4 light;           // c101
    vec4 eye;             // c106
}
draw;

layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    vec3 n = in_normal;

    // clip position, fog from clip z, the uv on stages 0 and 1
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;
    v_fog = d3d_fog_factor(clip.z);
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_t1 = vec4(in_uv, 0.0, 1.0);

    // tangent frame from up = (k.z, k.w, k.z) and N, the two lengths swapped as written
    vec3 up = vec3(draw.k.z, draw.k.w, draw.k.z);
    vec3 t_raw = cross(up, n);
    vec3 b_raw = cross(n, t_raw);
    vec3 bitangent = b_raw * inversesqrt(dot(t_raw, t_raw));
    vec3 tangent = t_raw * inversesqrt(dot(b_raw, b_raw));

    // light: through World^-1 again, into the tangent frame
    vec3 light_obj = mat3(draw.inverse_world) * draw.light.xyz;
    vec3 light_t = normalize(vec3(dot(light_obj, tangent), dot(light_obj, bitangent), dot(light_obj, n)));
    v_t3 = vec4(light_t, light_t.z);

    // eye vector: world space to object space, into the tangent frame
    vec3 pos_world = (draw.world * in_pos).xyz;
    vec3 view_obj = normalize(mat3(draw.inverse_world) * (draw.eye.xyz - pos_world));
    vec3 view_t = normalize(vec3(dot(view_obj, tangent), dot(view_obj, bitangent), dot(view_obj, n)));

    // half vector
    vec3 half_t = normalize((light_t + view_t) * draw.k.y);
    v_t2 = vec4(half_t, half_t.z);
}
