#version 450
// The gamma ramp (port-only pass, with screen_ff.vert over the whole frame): the original has no program for this.
// render_frame_begin sends a 256-entry ramp every frame (D3DDevice_SetGammaRamp from gamma_ramp_current 0xE0B30), and
// the video output applies it to everything, HUD included. Stages set it with gamma_levels_set 0x448F4 as a black and a
// white point per channel, a linear stretch between them. The port draws the finished frame through this as its last
// pass, with the ramp as a 256x1 texture (r, g, b ramps in the channels) and the frame's uv 0..1.
// Applying it to an 8-bit frame matches the original exactly; on a higher-precision frame the lookup is linear
// between entries.

layout(set = 2, binding = 0) uniform sampler2D frame_tex;
layout(set = 2, binding = 1) uniform sampler2D ramp;

layout(location = 2) in vec4 v_t0;

layout(location = 0) out vec4 out_color;

float ramp_channel(float c, int channel)
{
    float x = clamp(c, 0.0, 1.0) * 255.0;
    int i0 = int(floor(x));
    int i1 = min(i0 + 1, 255);
    float a = texelFetch(ramp, ivec2(i0, 0), 0)[channel];
    float b = texelFetch(ramp, ivec2(i1, 0), 0)[channel];
    return mix(a, b, x - float(i0));
}

void main()
{
    vec4 c = texture(frame_tex, v_t0.xy);
    out_color = vec4(ramp_channel(c.r, 0), ramp_channel(c.g, 1), ramp_channel(c.b, 2), 1.0);
}
