#pragma once
// Each program's per-draw block filled the way its handler fills the constants (the rows of
// material_transform_dispatch, src/hand/u_0bee60.c), and the fixed-function pipeline's
// transforms, light and material. Matrices go in as the C code builds them (docs/shaders.md).
#include "render/mesh_draw.hpp"

// D3D's fixed function: WORLD, VIEW, PROJECTION, light 0, the material (ambient from the section
// colour, power)
void fixed_constants(const DrawContext& context, const MeshRecord& record,
                     const ModelSection& section, FrameDraws& out, DrawItem& item);
// shader_constants_fog
void fog_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                   DrawItem& item);
// shader_constants_cockpit_uv: the instrument's uv offset (cockpit_uv_set's row), carried
// in the record's effect[0], effect[1]
void cockpit_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item);
// shader_constants_point: the three point lights nearest the mesh centre
void point_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                     DrawItem& item);
// shader_constants_bump
void bump_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                    DrawItem& item);
// shader_constants_reflect
void reflect_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item);
// shader_constants_seabump and the water's bump matrix from pass_draw_meshes
void seabump_constants(const DrawContext& context, const MeshRecord& record, bool water,
                       FrameDraws& out, DrawItem& item);
// shader_constants_rainbow: the record's strength, scroll and phase (src/effects advances
// them per update)
void rainbow_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item);
// shader_constants_effopt: the record's reveal sphere and glow
void effopt_constants(const DrawContext& context, const MeshRecord& record,
                      const ModelSection& section, FrameDraws& out, DrawItem& item);
// shader_constants_multi_matrix_pl for one section, its world matrix as bone 0
void fighter_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item);
// shader_constants_multi_shadow, the same way
void fighter_shadow_constants(const DrawContext& context, const MeshRecord& record,
                              FrameDraws& out, DrawItem& item);
