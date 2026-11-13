#version 450
#extension GL_GOOGLE_include_directive : require
// blur.xvu (row 9), dof.xvu (rows 8 and 0x1C, two loaded copies of the same file) and shimmer.xvu (row 0xD): the
// screen-effect quads of the pass 11 composite. With DOF_XVU = true this is dof.xvu, which does not write oT3.
//
// No constants. Inputs (the quads at 0x25B440 and 0x25B500, 0x30 a vertex): v0 = a clip-space position (x from
// -1.003125, one 640-wide pixel past the left edge, to 1; y from -1 to 1.0041667, one 480-high pixel past the top),
// v1..v4 = four texture coordinates in the texels of the texture each stage samples (linear targets) or 0..1 (the
// heat-haze wave texture). Drawn as quad lists of one quad; the port draws two triangles.
//   +0x000 (0x25B500) dofblur: uv0 wave 0..1, uv1 blur160 0..160 x 0..120, uv2 scene 0..640 x 0..480, uv3 the same
//   +0x0C0           640 -> 320 downsample: four taps at (+1, +1), (-1, -1), (-1, +1), (+1, -1) texels around 0..640
//   +0x180           320 -> 160 downsample: the same taps around 0..320 x 0..240
//   +0x240           shimmer (dead)
//   0x25B440         dof: uv0 blur160, uv1 scene, uv2 depth, uv3 zero
// Outputs: oPos = v0, oT0..oT3 = v1..v4 (dof.xvu: oT0..oT2 only).
// Changed: the viewport epilogue is dropped; dof.xvu's unwritten oT3 is the assumed default (dofblur.frag reads it).

#include "include/d3d.glsl"

layout(constant_id = 0) const bool DOF_XVU = false;

layout(location = 0) in vec4 in_pos; // v0
layout(location = 1) in vec2 in_uv0; // v1
layout(location = 2) in vec2 in_uv1; // v2
layout(location = 3) in vec2 in_uv2; // v3
layout(location = 4) in vec2 in_uv3; // v4

layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;

void main()
{
    gl_Position = in_pos;
    v_t0 = vec4(in_uv0, 0.0, 1.0);
    v_t1 = vec4(in_uv1, 0.0, 1.0);
    v_t2 = vec4(in_uv2, 0.0, 1.0);
    v_t3 = DOF_XVU ? NV2A_DEFAULT_TEXCOORD : vec4(in_uv3, 0.0, 1.0);
}
