#version 450
#extension GL_GOOGLE_include_directive : require
// The 3D's post pass (port-only, with screen_ff.vert over the whole target, like gamma.frag): the original has no
// program for this. It runs between the pass 11 composite (dof, dofblur or the tinted copy, and the cockpit's meshes)
// and the 2D of pass 12, only when one of the PC's picture settings asks for it (docs/settings.md), so the menus and
// the HUD are drawn over its result untouched:
//   ANTI-ALIASING FXAA: edges found by luma contrast, blended along them (FXAA's console form, Lottes 2009-2011);
//   BRIGHTNESS: x^(100/b) per channel.
// The composite's output is read with a linear sampler; uv 0..1 over the whole target.
// ScreenDraw c0: x the brightness power, y 1 for FXAA.

#include "include/screen_draw.glsl"

layout(set = 2, binding = 0) uniform sampler2D frame_tex;

layout(location = 2) in vec4 v_t0;

layout(location = 0) out vec4 out_color;

const vec3 LUMA = vec3(0.299, 0.587, 0.114);
const float SPAN_MAX = 8.0;
const float REDUCE_MUL = 1.0 / 8.0;
const float REDUCE_MIN = 1.0 / 128.0;
// contrast under which a pixel is left alone: absolute, and relative to its brightest neighbour
const float EDGE_MIN = 1.0 / 32.0;
const float EDGE_RELATIVE = 1.0 / 8.0;

vec3 fxaa(vec2 uv, vec2 texel)
{
    vec3 rgb_m = texture(frame_tex, uv).rgb;
    float nw = dot(texture(frame_tex, uv + vec2(-1.0, -1.0) * texel).rgb, LUMA);
    float ne = dot(texture(frame_tex, uv + vec2(1.0, -1.0) * texel).rgb, LUMA);
    float sw = dot(texture(frame_tex, uv + vec2(-1.0, 1.0) * texel).rgb, LUMA);
    float se = dot(texture(frame_tex, uv + vec2(1.0, 1.0) * texel).rgb, LUMA);
    float m = dot(rgb_m, LUMA);
    float luma_min = min(m, min(min(nw, ne), min(sw, se)));
    float luma_max = max(m, max(max(nw, ne), max(sw, se)));
    if (luma_max - luma_min < max(EDGE_MIN, luma_max * EDGE_RELATIVE)) {
        return rgb_m;
    }

    // the edge's direction, across the luma gradient
    vec2 dir = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * (0.25 * REDUCE_MUL), REDUCE_MIN);
    float scale = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * scale, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * texel;

    // two taps along it near the pixel, then two further out; the far pair only when it stays in
    // the neighbourhood's range (else it crossed another edge)
    vec3 near = 0.5 * (texture(frame_tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(frame_tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 far = near * 0.5 + 0.25 * (texture(frame_tex, uv - dir * 0.5).rgb +
                                    texture(frame_tex, uv + dir * 0.5).rgb);
    float luma_far = dot(far, LUMA);
    return luma_far < luma_min || luma_far > luma_max ? near : far;
}

void main()
{
    // the alpha kept as the composite left it (the capture 0x225 copies this target)
    vec4 here = texture(frame_tex, v_t0.xy);
    vec2 texel = 1.0 / vec2(textureSize(frame_tex, 0));
    vec3 c = draw.c0.y > 0.5 ? fxaa(v_t0.xy, texel) : here.rgb;
    c = pow(clamp(c, 0.0, 1.0), vec3(draw.c0.x));
    out_color = vec4(c, here.a);
}
