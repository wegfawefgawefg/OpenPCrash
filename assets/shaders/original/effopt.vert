#version 450
#extension GL_GOOGLE_include_directive : require
// effopt.xvu, bind row 0 (shader_bind_effopt, bound by pass_open_distortion), with effopt.frag.
// Pass 6, the contact decals (contact_decal_draw 0x717E9: a 30-frame shockwave model sequence), drawn over the copy of
// the backdrop, which they distort.
//
// Constants (shader_constants_effopt 0xC0542):
//   c96..c99    world_view_proj   (World x View x Projection)^T
//   c100        k                 (0.5, 0.2, 0, 1)
//   c101        view_size         (viewport width / 2, -height / 2, height / 2, 0), 640x480 pixels
//   c102        material          SubMesh.color as rgb, w 1
//   c103        light             -Light.Direction through World^-1, normalized, times Light.Diffuse.r (w 0)
//   c104        sphere            0xA94520[Mesh.texture_slot] with its centre through World^-1, w = radius R
//   c105        fog_k             (R + 4, 1, 0.25, 0)
//   c106        view_origin       (viewport x, viewport y, 0, 0)
//   c107        (4, 1/3, 3, 0.5): uploaded, never read
//   c108        eye               the camera position in world space
//   c109..c112  inverse_world     World^-1, uploaded untransposed
//   c113..c116  world             World, uploaded untransposed
//   c117        glow              (0, 0, 0, effopt_glow_time(draw index)); also PS c0
// The handler binds bump_light.tga (D_00A7B8D4) on stage 3 (clamp, linear) and counts the draw index up per call
// (reset per update by effopt_draw_index_reset): the glow is a per-draw-call animation the port must tie to updates.
// Inputs: format A, v0 position, v1 normal. Outputs: oPos, oFog, oD0, oT0 (PASSTHRU: the packed tangent normal),
// oT1.xy (backdrop texel), oT2 = (half vector, s), oT3 = light, both in the tangent frame.
// Computes: as multi_optical's refraction, with the minimum push 0.2 instead of k and the push scaled by the glow;
// "world" positions and normals come from dotting with World's rows (untransposed upload, kept: no translation, the
// transpose's rotation), the eye vector and light go through World^-1's rows the same way. The tangent frame is
// bump's (T = cross(up, N), B = cross(N, T), lengths swapped). Colour = material x max(N' . light, 0) + (1 - s).
// Changed: the viewport epilogue is dropped; unwritten oT1.zw are the assumed defaults.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1

layout(set = 1, binding = 0, std140) uniform EffoptDraw
{
    mat4 world_view_proj; // c96..c99
    mat4 inverse_world;   // c109..c112, untransposed
    mat4 world;           // c113..c116, untransposed
    vec4 k;               // c100
    vec4 view_size;       // c101
    vec4 material;        // c102
    vec4 light;           // c103
    vec4 sphere;          // c104
    vec4 fog_k;           // c105
    vec4 view_origin;     // c106
    vec4 eye;             // c108
    vec4 glow;            // c117, PS c0
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    vec3 n = in_normal;
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;

    // the decal's "world" position: rows of the untransposed World
    vec3 pos_rows = (in_pos * draw.world).xyz;

    // reveal sphere terms
    float dist = length(draw.sphere.xyz - pos_rows);
    float s = min(max((draw.sphere.w - dist) * draw.fog_k.y, draw.k.z), draw.k.w);
    float t = min(max((draw.fog_k.x - dist) * draw.fog_k.z, draw.k.y), draw.k.w);
    v_fog = d3d_fog_factor(clip.z * s);

    // backdrop texel: clip position pushed along the normal's clip x, y by t x glow
    vec2 normal_clip = (mat3(draw.world_view_proj) * n).xy;
    vec2 pushed = normal_clip * (t * draw.glow.w) + clip.xy;
    vec2 ndc = pushed * (1.0 / clip.w);
    vec2 texel = ndc * draw.view_size.xy + draw.view_size.xz + draw.view_origin.xy;
    v_t1 = vec4(texel, NV2A_DEFAULT_TEXCOORD.zw);

    // colour: material lit by the object-space light against the row-transformed normal, plus 1 - s
    vec3 normal_rows = n * mat3(draw.world);
    float n_dot_l = max(dot(normal_rows, draw.light.xyz), draw.k.z);
    vec3 color = draw.material.rgb * n_dot_l + draw.light.w + (draw.k.w - s);
    v_d0 = clamp(vec4(color, draw.material.w), 0.0, 1.0);

    // tangent frame from up = (k.z, k.w, k.z), lengths swapped as written
    vec3 up = vec3(draw.k.z, draw.k.w, draw.k.z);
    vec3 t_raw = cross(up, n);
    vec3 b_raw = cross(n, t_raw);
    vec3 bitangent = b_raw * inversesqrt(dot(t_raw, t_raw));
    vec3 tangent = t_raw * inversesqrt(dot(b_raw, b_raw));

    // the normal in the tangent frame, packed 0..1 for the PASSTHRU stage
    vec3 normal_t = vec3(dot(n, tangent), dot(n, bitangent), dot(n, n));
    vec3 packed = normal_t * draw.k.x + draw.k.x;
    v_t0 = vec4(packed, packed.z);

    // light in the tangent frame
    vec3 light_obj = normalize(draw.light.xyz * mat3(draw.inverse_world));
    vec3 light_t = normalize(vec3(dot(light_obj, tangent), dot(light_obj, bitangent), dot(light_obj, n)));
    v_t3 = vec4(light_t, light_t.z);

    // eye vector in the tangent frame, half vector
    vec3 view_obj = normalize((draw.eye.xyz - pos_rows) * mat3(draw.inverse_world));
    vec3 view_t = normalize(vec3(dot(view_obj, tangent), dot(view_obj, bitangent), dot(view_obj, n)));
    vec3 half_t = normalize((light_t + view_t) * draw.k.x);
    v_t2 = vec4(half_t, s);
}
