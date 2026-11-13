// The per-frame block (set 0, binding 0) and D3D's fog, which the original gets from render states rather than from
// any program.
//
// Fog. fog_params_set 0xA1510 sets FOGTABLEMODE (stage data kind 0 none, 1 linear, 2 exp, 3 exp2), FOGCOLOR,
// FOGSTART, FOGEND and FOGDENSITY; battle_lighting_update 0x44E1F fades colour and end on night stages. Fog is switched
// per pass and per mesh (FOGENABLE: on in passes 2, 4, 5, 6 and 8, off elsewhere; mesh flag 0x2000000 turns it off), which
// is DS_FOG in draw_state.glsl. No .xpu has a final combiner, so D3D's own one blends lerp(fog colour, r0, factor).
//
// The factor is computed per vertex from the fog coordinate and interpolated, as xemu models NV2A (assumed; per-pixel
// evaluation would differ only for exp and exp2). The coordinate is oFog.x for the programs (mostly clip z, some scaled
// by height or by light falloff) and view-space depth (clip w) for the fixed-function path, also assumed.

#ifndef FRAME_GLSL
#define FRAME_GLSL

layout(set = 0, binding = 0, std140) uniform Frame
{
    vec4 fog_color;  // FOGCOLOR, rgb 0..1 (alpha unused)
    vec4 fog_params; // x FOGSTART, y FOGEND, z FOGDENSITY, w FOGTABLEMODE as D3DFOGMODE: 0 none, 1 exp, 2 exp2, 3 linear
    vec4 linear_size; // the original's full-size linear targets in texels: (640, 480, 1/640, 1/480)
    vec4 pixel;       // x: offset from a D3D pixel coordinate to Vulkan's pixel centre (0.5 assumed), yzw unused
}
frame;

// D3D's fog table on a fog coordinate. FOGTABLEMODE none with fog on takes the coordinate as the factor itself.
float d3d_fog_factor(float coord)
{
    int mode = int(frame.fog_params.w);
    if (mode == 3) {
        return (frame.fog_params.y - coord) / (frame.fog_params.y - frame.fog_params.x);
    }
    if (mode == 1) {
        return exp(-frame.fog_params.z * coord);
    }
    if (mode == 2) {
        float x = frame.fog_params.z * coord;
        return exp(-x * x);
    }
    return coord;
}

#endif
