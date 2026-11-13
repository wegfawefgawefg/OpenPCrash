#include "stage_lighting.hpp"

#include "addresses.hpp"
#include "sexp_out.hpp"
#include "xbe.hpp"

#include <algorithm>

namespace {

// three floats at va, z negated for the port frame (docs/assets.md, "The port's frame")
std::string port_vec3(const Xbe& xbe, std::uint32_t va) {
    return sx_float(xbe_f32(xbe, va)) + " " + sx_float(xbe_f32(xbe, va + 4)) + " " +
           sx_float(-xbe_f32(xbe, va + 8));
}

std::string ints(const Xbe& xbe, std::uint32_t va, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) {
        text += (i ? " " : "") + sx_int(xbe_i32(xbe, va + i * 4));
    }
    return text;
}

} // namespace

std::string stage_lighting_text(const Xbe& xbe) {
    std::string text =
        ";; stage_lighting: per stage selector (stage * 2 + night) what battle_lighting_enter 0x44A98\n"
        ";; and battle_lighting_update 0x44E1F set, the dof and heat haze of battle_task_begin, and the\n"
        ";; stage's lamps (StageLights 0x281648). Directions and positions are in the port frame.\n"
        ";; fog kind is the record's: 0 none, 1 linear, 2 exp, 3 exp2 (fog_params_set 0xA1510).\n";
    // the default point light every StageLights row is reset to (point_lights_reset 0xBEFFC), and
    // the height scale of the fog and pointconst shaders (1 / it)
    const std::uint32_t point = xbe_at::point_light_default;
    text += "(point-light-default (position " + port_vec3(xbe, point) + ") (colour " +
            sx_hex(xbe_u32(xbe, point + 12)) + ") (range " + sx_float(xbe_f32(xbe, point + 16)) +
            "))  ; 0x280CE8\n";
    text += "(point-height-scale " + sx_float(xbe_f32(xbe, xbe_at::point_height_scale)) +
            ")  ; D_00280604\n";
    // the lamp shader_constants_multi_matrix_pl uses when a stage has none (direction and colour
    // are .bss: 0)
    const std::uint32_t lamp = xbe_at::lamp_default_position;
    text += "(lamp-default (position " + port_vec3(xbe, lamp) + "))  ; 0x280CFC\n";

    const int variant = xbe_i32(xbe, xbe_at::stage_lighting_variant);
    for (int selector = 0; selector < 8; ++selector) {
        // the 0x54-byte record battle_lighting_enter copies to 0x416858
        const std::uint32_t record =
            xbe_u32(xbe, xbe_at::stage_lighting_records + selector * 4) + variant * 0x54;
        text += "(lighting (selector " + sx_int(selector) + ") (record " + sx_hex(record) +
                ") (name " + sx_string(xbe_cstring(xbe, xbe_u32(xbe, record))) + ")\n";
        text += "  (fog (kind " + sx_int(xbe_i32(xbe, record + 0x14)) + ") (colour " +
                sx_hex(xbe_u32(xbe, record + 4) & 0xFFFFFF) + ") (density " +
                sx_float(xbe_f32(xbe, record + 8)) + ") (start " +
                sx_float(xbe_f32(xbe, record + 0xC)) + ") (end " +
                sx_float(xbe_f32(xbe, record + 0x10)) + "))\n";
        text += "  (light (direction " +
                port_vec3(xbe, xbe_at::stage_light_directions + selector * 12) + ") (colour " +
                sx_hex(xbe_u32(xbe, record + 0x18)) + ") (diffuse " +
                sx_float(xbe_f32(xbe, record + 0x1C)) + ") (ambient " +
                sx_float(xbe_f32(xbe, record + 0x20)) + "))  ; stage_light_directions\n";
        text += "  (tint " + ints(xbe, record + 0x24, 4) + ")  ; a r g b, scene_tint_set\n";
        text += "  (gamma (low " + ints(xbe, record + 0x34, 3) + ") (high " +
                ints(xbe, record + 0x40, 3) + "))  ; gamma_levels_set\n";
        text += "  (record-4c " + sx_int(xbe_u8(xbe, record + 0x4C)) + ") (record-50 " +
                sx_float(xbe_f32(xbe, record + 0x50)) +
                ")  ; to D_004168AC and D_00416850, read by nothing ported\n";
        text += "  (shadow-colour " +
                sx_hex(xbe_u32(xbe, xbe_at::stage_shadow_colors + selector * 4)) +
                ")  ; shadow_color_set\n";
        text += "  (dof-level " + sx_float(xbe_f32(xbe, xbe_at::stage_dof_levels + selector * 4)) +
                ") (heat-wave " + sx_int(xbe_u8(xbe, xbe_at::stage_heat_wave + selector)) +
                ")  ; D_00203740, D_00203760\n";

        // StageLights {bump_count, bump, lamp_count, lamp_ranges, lamp_pos, lamp_colors}
        const std::uint32_t lights = xbe_at::stage_lights + selector * 0x18;
        const int lamp_count = xbe_i32(xbe, lights + 8);
        const std::uint32_t ranges = xbe_u32(xbe, lights + 0xC);
        const std::uint32_t positions = xbe_u32(xbe, lights + 0x10);
        const std::uint32_t colours = xbe_u32(xbe, lights + 0x14);
        text += "  (lamps (count " + sx_int(lamp_count) + ")\n    (ranges";
        int lamps = lamp_count;
        for (int zone = 0; zone < 5; ++zone) {
            const int first = xbe_i32(xbe, ranges + zone * 8);
            const int end = xbe_i32(xbe, ranges + zone * 8 + 4);
            text += " (" + sx_int(first) + " " + sx_int(end) + ")";
            lamps = std::max(lamps, end);
        }
        text += ")  ; first and end lamp by zone (shibuya_zone_at on stages 2 and 3, else 0)\n";
        for (int i = 0; i < lamps; ++i) {
            // LampColors {first: the direction, its length the range; second: the colour}
            text += "    (lamp (position " + port_vec3(xbe, positions + i * 12) +
                    ") (direction " + port_vec3(xbe, colours + i * 24) + ") (colour " +
                    sx_float(xbe_f32(xbe, colours + i * 24 + 12)) + " " +
                    sx_float(xbe_f32(xbe, colours + i * 24 + 16)) + " " +
                    sx_float(xbe_f32(xbe, colours + i * 24 + 20)) + "))\n";
        }
        text += "  ))\n";
    }
    return text;
}
