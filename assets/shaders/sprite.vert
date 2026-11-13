#version 450
// The overlay sprites (src/render/vk_sprites.cpp): corners arrive in clip space, already through
// spritemat.xvu's pixel formula on the CPU. Colour clamped as the original's oD0 is. in_blur is
// the port's: a widescreen backdrop's blurred copy (its radius in uv), 0 for every original sprite;
// in_period the line periods (uv) its taps keep to.

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_colour;
layout(location = 3) in float in_k;
layout(location = 4) in float in_blur;
layout(location = 5) in vec2 in_period;

layout(location = 0) out vec4 out_colour;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out float out_k;
layout(location = 3) out float out_blur;
layout(location = 4) out vec2 out_period;

void main() {
    gl_Position = vec4(in_position, 0.5, 1.0);
    out_colour = clamp(in_colour, 0.0, 1.0);
    out_uv = in_uv;
    out_k = clamp(in_k, 0.0, 1.0);
    out_blur = max(in_blur, 0.0);
    out_period = max(in_period, vec2(0.0));
}
