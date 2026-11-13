// Fragment-only end of the pipeline the original leaves to D3D: stage 0's alpha kill, then D3D's final combiner (no .xpu
// has one of its own), alpha test and the framebuffer clamp. See draw_state.glsl for which passes set which bit.

#ifndef FINISH_GLSL
#define FINISH_GLSL

#include "draw_state.glsl"

// Stage 0's alpha kill, tested on the fetched texel (the filtered value; the Xbox kills alpha == 0).
void d3d_alpha_kill(vec4 texel0)
{
    if (ds_on(DS_ALPHA_KILL) && texel0.a <= 0.0) {
        discard;
    }
}

// r0 out of the last combiner stage (or the fixed-function stages) to the framebuffer: alpha test GEQUAL on the 8-bit
// alpha, the fog lerp, clamp to 0..1.
vec4 d3d_finish(vec4 r0, float fog)
{
    vec4 c = clamp(r0, 0.0, 1.0);
    if (ds_on(DS_ALPHA_TEST)) {
        float ref = float((ds.flags >> 8) & 0xFFu);
        if (floor(c.a * 255.0 + 0.5) < ref) {
            discard;
        }
    }
    if (ds_on(DS_FOG)) {
        c.rgb = mix(frame.fog_color.rgb, c.rgb, clamp(fog, 0.0, 1.0));
    }
    return c;
}

#endif
