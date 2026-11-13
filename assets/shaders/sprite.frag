#version 450
// spritemat.xpu's combiners (assets/shaders/original/premultiplied.frag has the full reading):
// r0 = v0 * t0, rgb premultiplied by alpha, alpha times k (0 for flag 1 sprites, which then add).
// Stage 0's alpha kill: texels of alpha 0 are dropped. Blended ONE, INV_SRC_ALPHA.
// The port's blurred copies (in_blur > 0, the radius in uv: docs/screens.md "Widescreen") average
// a disc of taps on a golden-angle spiral, opaque, with no alpha kill; with in_period the taps
// step by whole line periods that way, so the picture's own scanlines survive the blur.

layout(set = 0, binding = 0) uniform sampler2D picture;

layout(location = 0) in vec4 in_colour;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in float in_k;
layout(location = 3) in float in_blur;
layout(location = 4) in vec2 in_period;
layout(location = 0) out vec4 out_colour;

vec4 blurred(vec2 uv, float radius, vec2 period) {
    const int taps = 32;
    vec2 aspect = vec2(1.0, float(textureSize(picture, 0).x) / float(textureSize(picture, 0).y));
    vec4 sum = vec4(0.0);
    for (int i = 0; i < taps; ++i) {
        float r = sqrt((float(i) + 0.5) / float(taps)) * radius;
        float a = float(i) * 2.39996323;
        vec2 d = vec2(cos(a), sin(a)) * r * aspect;
        // whole line periods only, so the picture's scanlines (grid) stay in phase and sharp
        d = mix(d, round(d / max(period, vec2(1e-6))) * period, step(vec2(1e-6), period));
        sum += texture(picture, uv + d);
    }
    return vec4((sum / float(taps)).rgb, 1.0);
}

void main() {
    vec4 texel = in_blur > 0.0 ? blurred(in_uv, in_blur, in_period) : texture(picture, in_uv);
    if (texel.a == 0.0) {
        discard;
    }
    vec4 r0 = clamp(in_colour * texel, 0.0, 1.0);
    r0.rgb = clamp(r0.rgb * r0.a, 0.0, 1.0);
    r0.a = clamp(r0.a * in_k, 0.0, 1.0);
    out_colour = r0;
}
