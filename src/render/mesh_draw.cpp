#include "render/mesh_draw.hpp"

#include "render/mesh_constants.hpp"
#include "render/mesh_flags.hpp"

namespace {

// the texture slot of the reflection map: pass_draw_meshes forces TEXTABLE[0x18] on stage 1 for
// mesh 0x1000000, and the fighter handlers bind texture_slots_base + 0xC0, the same slot (the
// table's entries are 8 bytes: texture, palette)
constexpr int reflection_slot = 0x18;

// ff_mesh.frag's stage ops
enum : std::uint32_t {
    colour_disable = 0,
    colour_modulate = 1,
    colour_add = 2,
    colour_factor = 3,
    alpha_current = 0,
    alpha_texture = 1,
    alpha_modulate = 2,
    alpha_factor = 3,
};

// U: layer 0x10 wrap, 4 mirror, else clamp; V the same with 0x20 and 8
Address address_u(std::uint32_t layer) {
    return layer & layer_wrap_u     ? Address::Wrap
           : layer & layer_mirror_u ? Address::Mirror
                                    : Address::Clamp;
}
Address address_v(std::uint32_t layer) {
    return layer & layer_wrap_v     ? Address::Wrap
           : layer & layer_mirror_v ? Address::Mirror
                                    : Address::Clamp;
}

int slot_of(const DrawContext& context, int slot) {
    const int texture = slot_texture(*context.renderer, *context.cache, *context.slots, slot);
    return texture >= 0 ? texture : texture_white;
}

// a textured stage: tex[s] + Mesh.base from the table; the fighters' paint on stage 0. Mesh
// 0x80000 (the big screens) puts the scene on stage 0: retail samples 0x223 while drawing into
// it, the port the last frame's (vk_record_scene_last), addressed as the layer says (clamped)
StageTexture stage_texture(const DrawContext& context, const MeshRecord& record,
                           const ModelSection& section, int s) {
    StageTexture stage;
    if (s == 0 && record.flags & mesh_scene_texture) {
        stage.texture = texture_scene_last;
    } else if (s == 0 && record.paint >= 0) {
        stage.texture = record.paint;
    } else {
        stage.texture = slot_of(context, section.tex[s] + record.texture_base);
    }
    stage.u = address_u(section.layer[s]);
    stage.v = address_v(section.layer[s]);
    return stage;
}

StageTexture wrapped(int texture) {
    StageTexture stage;
    stage.texture = texture >= 0 ? texture : texture_white;
    return stage;
}

// pass_draw_meshes' stage loop: the ops ff_mesh.frag runs, the textures, and whether the last
// textured stage left the alpha alone (its "opaque", which picks pointalpha)
bool stage_setup(const DrawContext& context, const MeshRecord& record,
                 const ModelSection& section, DrawItem& item) {
    bool opaque = true;
    bool factor_pending = record.flags & mesh_factor;
    for (int s = 0; s < 4; ++s) {
        const int tex = section.tex[s];
        if (tex >= texture_stage_end) {
            break; // the original leaves the stage as the last draw set it
        }
        std::uint32_t op = 0;
        if (tex >= 0) {
            item.stages[s] = stage_texture(context, record, section, s);
            op = section.layer[s] & layer_add ? colour_add : colour_modulate;
            if (section.layer[s] & layer_texture_alpha && !(record.flags & mesh_opaque)) {
                opaque = false;
                op |= (record.flags & mesh_vertex_alpha ? alpha_current
                       : s == 0                         ? alpha_texture
                                                        : alpha_modulate)
                      << 2;
            } else {
                opaque = true;
            }
        } else if (factor_pending) {
            // the first untextured stage of a mesh with a texture factor multiplies by it, alpha
            // too unless sub 0x200
            op = colour_factor | (section.flags & sub_additive ? alpha_current : alpha_factor) << 2;
            factor_pending = false;
        }
        item.state.stages |= op << (8 * s);
    }
    return opaque;
}

void set_alpha_test(DrawItem& item, std::uint32_t reference) {
    item.state.flags = (item.state.flags & ~0xFF00u) | ds_alpha_test | reference << 8;
}

} // namespace

void draw_meshes(const DrawContext& context, const std::vector<const PassItem*>& queue,
                 FrameDraws& out) {
    const int pass = context.pass;
    for (const PassItem* entry : queue) {
        const MeshRecord& record = entry->item;
        const bool quad = record.flags & mesh_uv_quad; // uv-override quads, not ported
        if (quad || !box_on_screen(record.center, record.half, record.world,
                                   context.view_projection)) {
            continue;
        }
        const bool wire = record.flags & mesh_wire;
        // fog per mesh in passes 2 and 8 (mesh 0x2000000 off), off in the others drawn here
        const bool fog = (pass == 2 || pass == 8) && !(record.flags & mesh_no_fog);
        for (int j = 0; j < record.section_count; ++j) {
            const ModelSection& section = record.sections[j];
            if (!box_on_screen(section.center, section.half, record.world,
                               context.view_projection)) {
                continue;
            }
            DrawItem item;
            item.mesh = record.mesh;
            item.first_index = section.first_triangle * 3;
            item.index_count = section.triangle_count * 3;
            item.depth_test = pass != 11;
            item.cull = wire || section.flags & sub_two_sided ? Cull::none : Cull::back;
            item.state.factor = record.flags & mesh_factor ? record.factor : 0xFFFFFFFF;

            // lighting and specular
            bool lit = !(section.flags & sub_unlit) && pass != 11 && !wire;
            if (pass == 9) {
                lit = record.flags & mesh_lit_billboard;
            }
            item.state.flags = 0;
            if (fog) {
                item.state.flags |= ds_fog;
            }
            if (lit) {
                item.state.flags |= ds_lighting;
            }
            if (!(record.flags & mesh_no_specular) && section.flags & sub_specular) {
                item.state.flags |= ds_specular;
            }

            // alpha: a cutout tests >= 0x80 unblended; mesh 0x40000 is opaque: no blending, no
            // test, no kill; the rest blend with the test at 0 and the kill on
            bool blend = false;
            if (section.flags & sub_cutout || record.flags & mesh_cutout) {
                set_alpha_test(item, 0x80);
                item.state.flags |= ds_alpha_kill;
            } else if (!(record.flags & mesh_opaque)) {
                blend = true;
                set_alpha_test(item, 0);
                item.state.flags |= ds_alpha_kill;
            }
            // blend factors and z writes: "opaque" world meshes blend with z writes
            Blend factors = Blend::alpha;
            item.depth_write = false;
            if (record.flags & mesh_blends) {
            } else if (record.flags & mesh_additive || section.flags & sub_additive) {
                factors = Blend::add;
            } else if (!(record.flags & mesh_blend) && pass != 1 && pass != 9) {
                item.depth_write = true;
            }
            item.blend = blend ? factors : Blend::none;
            if (pass == 11) {
                // pass_open_effects: z off, after the composite on the final target
                item.depth_write = false;
                item.cockpit = true;
            }

            // the texture stages, then the overrides
            const bool opaque = stage_setup(context, record, section, item);
            if (record.flags & mesh_water_bump) {
                item.stages[0] = wrapped(context.yurayura);
                item.stages[1] = wrapped(context.wave02);
            }

            // the program, first match (docs/shaders.md, "Which mesh gets which"); vision_eff
            // (0x200000) isn't drawn by this yet and takes the fixed function
            const bool textured = section.tex[0] >= 0;
            if (pass == 11 && textured) {
                item.program = Program::cockpit_uv;
                cockpit_constants(context, record, out, item);
            } else if (pass == 11) {
                item.program = Program::fixed;
                fixed_constants(context, record, section, out, item);
            } else if (section.layer[0] & layer_bump && textured && section.tex[1] >= 0) {
                item.program = Program::bump;
                item.stages[3] = wrapped(context.bump_light);
                item.stages[3].u = item.stages[3].v = Address::Clamp;
                bump_constants(context, record, out, item);
            } else if (record.flags & mesh_rainbow) {
                // the shield bubble: muteki01.tga on stage 0, V wrapped
                item.program = Program::rainbow;
                item.stages[0] = wrapped(context.muteki);
                item.stages[0].u = address_u(section.layer[0]);
                rainbow_constants(context, record, out, item);
            } else if (record.flags & mesh_reflect || section.layer[1] & layer_reflect) {
                if (record.flags & mesh_reflect) {
                    item.stages[1] = wrapped(slot_of(context, reflection_slot));
                }
                item.program = Program::reflect;
                reflect_constants(context, record, out, item);
            } else if (section.layer[0] & layer_seabump) {
                item.program = Program::seabump;
                seabump_constants(context, record, record.flags & mesh_water_bump, out, item);
            } else if (record.flags & mesh_vision) {
                item.program = Program::fixed;
                fixed_constants(context, record, section, out, item);
            } else if (context.battle && pass == 2 && textured && !(record.flags & mesh_factor)) {
                item.program = lit ? (opaque ? Program::point_lit_opaque : Program::point_lit)
                                   : (opaque ? Program::point_unlit_opaque : Program::point_unlit);
                point_constants(context, record, out, item);
            } else if (lit || record.flags & mesh_billboard) {
                item.program = Program::fixed;
                fixed_constants(context, record, section, out, item);
            } else {
                item.program = Program::fog;
                fog_constants(context, record, out, item);
            }
            out.items.push_back(item);
        }
    }
}

void draw_fighters(const DrawContext& context, const std::vector<const PassItem*>& queue,
                   FrameDraws& out) {
    // pass_open_fighters: z write, fog, stencil pass ZERO (fighters clear the volume count
    // under them), no blending, alpha test on, no alpha kill
    for (const PassItem* entry : queue) {
        const MeshRecord& record = entry->item;
        for (int j = 0; j < record.section_count; ++j) {
            const ModelSection& section = record.sections[j];
            DrawItem item;
            item.program = Program::fighter;
            item.mesh = record.mesh;
            item.first_index = section.first_triangle * 3;
            item.index_count = section.triangle_count * 3;
            item.blend = Blend::none;
            item.stencil = Stencil::zero_on_pass;
            item.cull = section.flags & sub_two_sided ? Cull::none : Cull::back;
            item.state.flags = ds_fog;
            set_alpha_test(item, 0);
            if (section.tex[0] >= 0 && section.tex[0] < texture_stage_end) {
                item.stages[0] = stage_texture(context, record, section, 0);
            }
            // the reflection map on stage 1, wrapped (shader_constants_multi_matrix_pl)
            item.stages[1] = wrapped(slot_of(context, reflection_slot));
            fighter_constants(context, record, out, item);
            out.items.push_back(item);
        }
    }
}

// decomp: pass_draw_distortion 0xB4D20
void draw_distortion(const DrawContext& context, const std::vector<const PassItem*>& queue,
                     bool copy, FrameDraws& out) {
    // pass_open_distortion: fog, z test without z write, no blending, stencil pass ZERO;
    // the backdrop on stage 1 (point, clamp), bump_light on stage 3 (linear, clamp), effopt
    for (const PassItem* entry : queue) {
        const MeshRecord& record = entry->item;
        if (!box_on_screen(record.center, record.half, record.world, context.view_projection)) {
            continue;
        }
        for (int j = 0; j < record.section_count; ++j) {
            const ModelSection& section = record.sections[j];
            if (!box_on_screen(section.center, section.half, record.world,
                               context.view_projection)) {
                continue;
            }
            DrawItem item;
            item.program = Program::effopt;
            item.mesh = record.mesh;
            item.first_index = section.first_triangle * 3;
            item.index_count = section.triangle_count * 3;
            item.blend = Blend::none;
            item.depth_write = false;
            item.stencil = Stencil::zero_on_pass;
            item.cull = section.flags & sub_two_sided ? Cull::none : Cull::back;
            item.state.flags = ds_fog;
            item.backdrop = copy;
            copy = false;
            if (section.tex[0] >= 0 && section.tex[0] < texture_stage_end) {
                item.stages[0] = stage_texture(context, record, section, 0);
            }
            item.stages[1].texture = texture_backdrop;
            item.stages[1].u = item.stages[1].v = Address::Clamp;
            item.stages[1].point = true;
            item.stages[3] = wrapped(context.bump_light);
            item.stages[3].u = item.stages[3].v = Address::Clamp;
            effopt_constants(context, record, section, out, item);
            out.items.push_back(item);
        }
    }
}

void draw_shadow_volumes(const DrawContext& context, const std::vector<const PassItem*>& queue,
                         FrameDraws& out) {
    const bool fighters = context.pass == 3;
    for (const PassItem* entry : queue) {
        const MeshRecord& record = entry->item;
        if (!fighters &&
            !box_on_screen(record.center, record.half, record.world, context.view_projection)) {
            continue;
        }
        for (int j = 0; j < record.section_count; ++j) {
            const ModelSection& section = record.sections[j];
            // the extra pair with z off when the eye is inside the box (always for fighters); the
            // eye is taken into the box's model space (the original compared the world eye with
            // the model-space box, right only for identity worlds; docs/bugs.md)
            bool inside = true;
            if (!fighters) {
                if (!box_on_screen(section.center, section.half, record.world,
                                   context.view_projection)) {
                    continue;
                }
                inside = point_in_box(transform_coord(context.eye, mat_inverse(record.world)),
                                      section.center, section.half);
            }
            // front faces count up, back faces down, colour writes off, no z write; one two-sided
            // draw for each of the original's pairs
            DrawItem item;
            item.program = fighters ? Program::fighter_shadow : Program::shadow_volume;
            item.mesh = record.mesh;
            item.first_index = section.first_triangle * 3;
            item.index_count = section.triangle_count * 3;
            item.blend = Blend::no_colour;
            item.depth_write = false;
            item.cull = Cull::none;
            item.stencil = Stencil::volume;
            if (fighters) {
                fighter_shadow_constants(context, record, out, item);
            } else {
                fixed_constants(context, record, section, out, item);
            }
            out.items.push_back(item);
            if (inside) {
                item.depth_test = false;
                item.stencil = Stencil::volume_inside;
                out.items.push_back(item);
            }
        }
    }
    if (!fighters && !queue.empty()) {
        // shadow_screen_quad: shadow_color blended where some volume counted the pixel,
        // the count set to 1 as it goes
        DrawItem quad;
        quad.program = Program::shadow_quad;
        quad.blend = Blend::alpha;
        quad.depth_test = false;
        quad.depth_write = false;
        quad.cull = Cull::none;
        quad.stencil = Stencil::quad;
        out.items.push_back(quad);
    }
}
