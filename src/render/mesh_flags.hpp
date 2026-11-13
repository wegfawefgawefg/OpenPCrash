#pragma once
// The flag words pass_draw_meshes reads (decomp docs/port/render.md "Per mesh", "Per sub-mesh"):
// a draw's mesh flags (Mesh.flags, the queued record's), a sub-mesh's flags (SubMesh.flags) and
// its texture stages' layer bits (SubMesh.layer).
#include <cstdint>

// Mesh.flags
inline constexpr std::uint32_t mesh_blend_a = 0x2;       // with 0x80: alpha blended, no z write
inline constexpr std::uint32_t mesh_blend = 0x10;        // alpha blended, no z write
inline constexpr std::uint32_t mesh_factor = 0x20;       // the texture factor (Mesh.factor)
inline constexpr std::uint32_t mesh_vertex_alpha = 0x40; // the vertex alpha, not the texture's
inline constexpr std::uint32_t mesh_blend_b = 0x80;
inline constexpr std::uint32_t mesh_blends = mesh_blend_a | mesh_blend_b; // 0x82
inline constexpr std::uint32_t mesh_billboard = 0x200; // pass 9: faces the camera; fixed function
inline constexpr std::uint32_t mesh_cutout = 0x400;    // alpha test >= 0x80, no blending
inline constexpr std::uint32_t mesh_wire = 0x2000;     // fill line, cull none
inline constexpr std::uint32_t mesh_additive = 0x8000; // SRC_ALPHA, ONE, no z write
inline constexpr std::uint32_t mesh_lit_billboard = 0x10000; // lit in pass 9
inline constexpr std::uint32_t mesh_opaque = 0x40000;        // no blending, no alpha test or kill
inline constexpr std::uint32_t mesh_scene_texture = 0x80000; // stage 0 samples the scene (0x223)
inline constexpr std::uint32_t mesh_rainbow = 0x100000;      // the rainbow shader (shield bubble)
inline constexpr std::uint32_t mesh_vision = 0x200000;       // vision_eff, no constants
inline constexpr std::uint32_t mesh_water_bump = 0x400000;   // yurayura and wave02, turning
inline constexpr std::uint32_t mesh_uv_quad = 0x800000;      // a quad from uv_override_next
inline constexpr std::uint32_t mesh_reflect = 0x1000000; // the reflect shader, slot 0x18 on stage 1
inline constexpr std::uint32_t mesh_no_fog = 0x2000000;  // no fog in passes 2 and 8
inline constexpr std::uint32_t mesh_no_specular = 0x8000000;

// SubMesh.flags
inline constexpr std::uint32_t sub_indexed = 0x2; // an indexed list
inline constexpr std::uint32_t sub_list = 0x4;    // a triangle list (else strips)
inline constexpr std::uint32_t sub_specular = 0x40;
inline constexpr std::uint32_t sub_unlit = 0x100;
inline constexpr std::uint32_t sub_additive = 0x200; // additive; the factor leaves alpha alone
inline constexpr std::uint32_t sub_two_sided = 0x800;
inline constexpr std::uint32_t sub_cutout = 0x4000;

// SubMesh.layer[stage] bits
inline constexpr std::uint32_t layer_add = 0x1;  // texture + current, not x
inline constexpr std::uint32_t layer_bump = 0x2; // layer[0] with two textures: the bump shader
inline constexpr std::uint32_t layer_mirror_u = 0x4;
inline constexpr std::uint32_t layer_mirror_v = 0x8;
inline constexpr std::uint32_t layer_wrap_u = 0x10;
inline constexpr std::uint32_t layer_wrap_v = 0x20;
inline constexpr std::uint32_t layer_texture_alpha = 0x40; // the texture's alpha counts
inline constexpr std::uint32_t layer_reflect = 0x80;       // layer[1]: the reflect shader
inline constexpr std::uint32_t layer_seabump = 0x800;      // layer[0]: the seabump shader

// texture indices from 0x226 up end a sub-mesh's stages (the render targets sit below)
inline constexpr int texture_stage_end = 0x226;
