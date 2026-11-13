#include "render/stage_lighting.hpp"

#include "assets/pack_sexp.hpp"
#include "cook/sexp_read.hpp"

#include <cmath>

namespace {

void read_floats(gsexp::Node form, float* out, int count, int first = 1) {
    for (int k = 0; k < count; ++k) {
        out[k] = atom_float(form.child_at(first + k));
    }
}

void read_ints(gsexp::Node form, int* out, int count, int first = 1) {
    for (int k = 0; k < count; ++k) {
        out[k] = atom_int(form.child_at(first + k));
    }
}

void read_stage(const gsexp::FormView& form, StageLighting& stage) {
    const gsexp::FormView fog(form.find("fog"));
    stage.fog_kind = fog.get_int("kind").value_or(0);
    stage.fog_colour = atom_hex(fog.find("colour").child_at(1));
    stage.fog_density = fog.get_float("density").value_or(0);
    stage.fog_start = fog.get_float("start").value_or(0);
    stage.fog_end = fog.get_float("end").value_or(1);
    const gsexp::FormView light(form.find("light"));
    read_floats(light.find("direction"), stage.light_direction, 3);
    stage.light_colour = atom_hex(light.find("colour").child_at(1));
    stage.diffuse_scale = light.get_float("diffuse").value_or(1);
    stage.ambient_scale = light.get_float("ambient").value_or(0.25f);
    read_ints(form.find("tint"), stage.tint, 4);
    const gsexp::FormView gamma(form.find("gamma"));
    read_ints(gamma.find("low"), stage.gamma_low, 3);
    read_ints(gamma.find("high"), stage.gamma_high, 3);
    stage.shadow_colour = atom_hex(form.find("shadow-colour").child_at(1));
    stage.dof_level = form.get_float("dof-level").value_or(80);
    stage.heat_wave = form.get_int("heat-wave").value_or(0) != 0;
    const gsexp::FormView lamps(form.find("lamps"));
    stage.lamp_count = lamps.get_int("count").value_or(0);
    int zone = 0;
    for (gsexp::Node range : lamps.find("ranges").children()) {
        if (range.is_list() && zone < 5) {
            stage.lamp_ranges[zone][0] = atom_int(range.child_at(0));
            stage.lamp_ranges[zone][1] = atom_int(range.child_at(1));
            ++zone;
        }
    }
    for (gsexp::Node row : lamps.node().children()) {
        if (!row.is_list() || !row.head().is_atom("lamp")) {
            continue;
        }
        const gsexp::FormView lamp(row);
        StageLamp out;
        read_floats(lamp.find("position"), out.position, 3);
        read_floats(lamp.find("direction"), out.direction, 3);
        read_floats(lamp.find("colour"), out.colour, 3);
        stage.lamps.push_back(out);
    }
}

} // namespace

bool load_stage_lighting(const Pack& core, StageLightingTable& table, std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/stage_lighting.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("point-light-default")) {
            read_floats(form.find("position"), table.point_default.position, 3);
            table.point_default.colour = atom_hex(form.find("colour").child_at(1));
            table.point_default.range = form.get_float("range").value_or(0.01f);
        } else if (form.head().is_atom("point-height-scale")) {
            table.height_scale = atom_float(form.node().child_at(1));
        } else if (form.head().is_atom("lamp-default")) {
            read_floats(form.find("position"), table.lamp_default, 3);
        } else if (form.head().is_atom("lighting")) {
            const int selector = form.get_int("selector").value_or(-1);
            if (selector >= 0 && selector < 8) {
                read_stage(form, table.stages[selector]);
            }
        }
    }
    return true;
}

void battle_lighting(const StageLightingTable& table, int selector, int day, SceneLights& lights,
                     FrameSetup& setup) {
    const StageLighting& stage = table.stages[selector & 7];
    lights.stage = &stage;
    lights.table = &table;
    lights.selector = selector & 7;
    for (PointLight& point : lights.points) {
        point = table.point_default;
    }

    // light 0: the stage's direction normalized, its colour by the diffuse and ambient scales,
    // specular 1 (battle_lighting_update; light_set leaves diffuse alpha 0)
    lights.direction = vec_normalize(Vec3f{stage.light_direction[0], stage.light_direction[1],
                                           stage.light_direction[2]});
    const float rgb[3] = {static_cast<float>((stage.light_colour >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((stage.light_colour >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(stage.light_colour & 0xFF) / 255.0f};
    for (int c = 0; c < 3; ++c) {
        lights.diffuse[c] = rgb[c] * stage.diffuse_scale;
        lights.ambient[c] = rgb[c] * stage.ambient_scale;
    }

    // fog: night stages (odd selectors) fade its colour and end over a 30-day cycle
    std::uint32_t fog_colour = stage.fog_colour;
    float fog_end = stage.fog_end;
    if (selector % 2) {
        const float cycle =
            std::fabs((30.0f - static_cast<float>(static_cast<unsigned>(day) % 30)) / 15.0f - 1.0f);
        fog_end = (stage.fog_end - 70.0f) * cycle + 70.0f;
        const int r = static_cast<int>(static_cast<float>((fog_colour >> 16) & 0xFF) * cycle);
        const int g = static_cast<int>(static_cast<float>((fog_colour >> 8) & 0xFF) * cycle);
        const int b = static_cast<int>(static_cast<float>(fog_colour & 0xFF) * cycle);
        fog_colour = static_cast<std::uint32_t>(r << 16 | g << 8 | b);
    }
    setup.fog_colour[0] = static_cast<float>((fog_colour >> 16) & 0xFF) / 255.0f;
    setup.fog_colour[1] = static_cast<float>((fog_colour >> 8) & 0xFF) / 255.0f;
    setup.fog_colour[2] = static_cast<float>(fog_colour & 0xFF) / 255.0f;
    setup.fog_colour[3] = 1;
    // fog_params_set: the record's kind to D3DFOGMODE (none, linear 3, exp 1, exp2 2)
    const int modes[4] = {0, 3, 1, 2};
    setup.fog_params[0] = stage.fog_start;
    setup.fog_params[1] = fog_end;
    setup.fog_params[2] = stage.fog_density;
    setup.fog_params[3] =
        static_cast<float>(stage.fog_kind >= 0 && stage.fog_kind < 4 ? modes[stage.fog_kind] : 3);

    // scene_tint_set: a, r, g, b
    setup.tint = static_cast<std::uint32_t>(stage.tint[0] & 0xFF) << 24 |
                 static_cast<std::uint32_t>(stage.tint[1] & 0xFF) << 16 |
                 static_cast<std::uint32_t>(stage.tint[2] & 0xFF) << 8 |
                 static_cast<std::uint32_t>(stage.tint[3] & 0xFF);
    // gamma_levels_set: 0 below the low, 255 above the high, i between; entry 255 is never written
    // and keeps the identity ramp's
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < 255; ++i) {
            setup.gamma[c][i] = static_cast<std::uint8_t>(i < stage.gamma_low[c]    ? 0
                                                          : i > stage.gamma_high[c] ? 255
                                                                                    : i);
        }
        setup.gamma[c][255] = 255;
    }
    setup.shadow_colour = stage.shadow_colour;

    // battle_task_begin: dof on, its level less 10 x a 10-day cycle, the heat haze's bump
    // matrix from the same cycle
    const float cycle =
        std::fabs(std::fabs(static_cast<float>(static_cast<unsigned>(day) % 10) - 5.0f) - 5.0f) /
        5.0f;
    const float dof_level = stage.dof_level - cycle * 10.0f;
    setup.dof = true;
    setup.dof_threshold = dof_level * 0.0005f + 0.95f;
    setup.heat_wave = stage.heat_wave;
    setup.bump_scale = cycle + 1.0f;
    setup.bump_shear = cycle * 3.0f;
}
