#include "effects/effect_data.hpp"

#include "assets/pack_sexp.hpp"
#include "cook/sexp_read.hpp"

namespace {

void floats(const gsexp::FormView& form, const char* name, float* out, int count) {
    const gsexp::Node node = form.find(name);
    for (int i = 0; i < count; ++i) {
        out[i] = atom_float(node.child_at(static_cast<std::size_t>(1 + i)));
    }
}

float one_float(const gsexp::FormView& form, const char* name) {
    return atom_float(form.find(name).child_at(1));
}

int one_int(const gsexp::FormView& form, const char* name) {
    return atom_int(form.find(name).child_at(1));
}

// (name (time value) ...) or (name nil); the time -1 key is kept, curves are walked up to it
void float_curve(const gsexp::FormView& form, const char* name, std::vector<FloatKey>& out) {
    for (gsexp::Node key : form.find(name).children()) {
        if (key.is_list()) {
            out.push_back({atom_float(key.child_at(0)), atom_float(key.child_at(1))});
        }
    }
    if (!out.empty() && out.back().time >= 0) {
        out.push_back({-1, 0});
    }
}

void int_curve(const gsexp::FormView& form, const char* name, std::vector<IntKey>& out) {
    for (gsexp::Node key : form.find(name).children()) {
        if (key.is_list()) {
            out.push_back({atom_float(key.child_at(0)), atom_int(key.child_at(1))});
        }
    }
    if (!out.empty() && out.back().time >= 0) {
        out.push_back({-1, 0});
    }
}

ParticleRecord read_record(const gsexp::FormView& form) {
    ParticleRecord r;
    floats(form, "forces", r.forces, 6);
    r.draw_flag = one_int(form, "draw_flag");
    r.gravity_off = one_int(form, "gravity_off") != 0;
    floats(form, "gravity", r.gravity, 4);
    r.force_a_off = one_int(form, "force_a_off") != 0;
    r.force_a = one_float(form, "force_a");
    floats(form, "force_a_rot", r.force_a_rot, 3);
    r.force_b_off = one_int(form, "force_b_off") != 0;
    r.force_b = one_float(form, "force_b");
    r.force_b_radial = one_int(form, "force_b_radial") != 0;
    r.force_b_range = one_float(form, "force_b_range");
    floats(form, "force_b_rot", r.force_b_rot, 3);
    floats(form, "offset", r.offset, 3);
    r.color_time = one_int(form, "color_time");
    r.alpha_time = one_int(form, "alpha_time");
    r.life = one_float(form, "life");
    r.life_jitter = one_float(form, "life_jitter");
    r.mass = one_float(form, "mass");
    r.mass_time = one_int(form, "mass_time");
    r.scale = one_float(form, "scale");
    r.scale_time = one_int(form, "scale_time");
    r.scale_jitter = one_float(form, "scale_jitter");
    r.speed_min = one_float(form, "speed_min");
    r.speed_min_time = one_int(form, "speed_min_time");
    r.speed_max = one_float(form, "speed_max");
    r.speed_max_time = one_int(form, "speed_max_time");
    floats(form, "color", r.color, 4);
    floats(form, "hsv_spread", r.hsv_spread, 3);
    r.alpha_bias = one_float(form, "alpha_bias");
    r.shape = one_int(form, "shape");
    r.shape_kind = one_int(form, "shape_kind");
    r.shape_axis = one_int(form, "shape_axis");
    floats(form, "emitter_setup", r.emitter_setup, 11);
    r.spread_deg = one_float(form, "spread_deg");
    r.spread_jitter = one_float(form, "spread_jitter");
    r.speed = one_float(form, "speed");
    r.speed_jitter = one_float(form, "speed_jitter");
    r.burst_radius = one_float(form, "burst_radius");
    r.texture = one_int(form, "texture");
    r.frame_count = one_int(form, "frame_count");
    const char* colour_names[4] = {"color_r", "color_g", "color_b", "alpha"};
    for (int c = 0; c < 4; ++c) {
        int_curve(form, colour_names[c], r.color_curves[c]);
    }
    float_curve(form, "scale_curve", r.scale_curve);
    float_curve(form, "force_b_curve", r.force_b_curve);
    float_curve(form, "force_a_curve", r.force_a_curve);
    float_curve(form, "rate_curve", r.rate_curve);
    float_curve(form, "mass_curve", r.mass_curve);
    float_curve(form, "speed_curve", r.speed_curve);
    float_curve(form, "speed_min_curve", r.speed_min_curve);
    float_curve(form, "speed_max_curve", r.speed_max_curve);
    return r;
}

template <typename T> T& row(std::vector<T>& rows, int id) {
    if (static_cast<int>(rows.size()) <= id) {
        rows.resize(static_cast<std::size_t>(id) + 1);
    }
    return rows[static_cast<std::size_t>(id)];
}

} // namespace

bool load_effect_data(const Pack& core, EffectData& data, std::string& error) {
    data = EffectData{};
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/effects.sexp", parsed, error)) {
        error += " (run pc_extract --only effects)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("lamp-glow-models")) {
            for (int i = 0; i < 14; ++i) {
                data.lamp_glow_models[i / 2][i % 2] =
                    atom_int(form.node().child_at(static_cast<std::size_t>(1 + i)));
            }
            continue;
        }
        if (form.head().is_atom("lamp-glow-columns")) {
            for (int i = 0; i < 4; ++i) {
                data.lamp_glow_columns[i] =
                    atom_int(form.node().child_at(static_cast<std::size_t>(1 + i)));
            }
            continue;
        }
        if (form.head().is_atom("arc-delays")) {
            for (int i = 0; i < 16; ++i) {
                data.arc_delays[i / 4][i % 4] =
                    atom_float(form.node().child_at(static_cast<std::size_t>(1 + i)));
            }
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0 || id > 255) {
            continue;
        }
        if (form.head().is_atom("particle")) {
            row(data.records, id) = read_record(form);
        } else if (form.head().is_atom("effect")) {
            EffectList& list = row(data.lists, id);
            list.name = form.get_string("name").value_or("");
            for (gsexp::Node emitter : form.node().children()) {
                if (emitter.is_list() && emitter.head().is_atom("emitter")) {
                    list.rows.push_back({static_cast<std::uint32_t>(atom_hex(emitter.child_at(1))),
                                         atom_int(emitter.child_at(2))});
                }
            }
        } else if (form.head().is_atom("effect-set")) {
            EffectSet& set = row(data.sets, id);
            for (gsexp::Node value : form.find("effects").children()) {
                if (!value.is_list() && !value.is_atom("effects")) {
                    set.effects.push_back(atom_int(value));
                }
            }
            for (gsexp::Node value : form.find("sprites").children()) {
                if (!value.is_list() && !value.is_atom("sprites")) {
                    set.sprites.push_back(atom_int(value));
                }
            }
        } else if (form.head().is_atom("sprite-kind")) {
            SpriteKind& kind = row(data.sprite_kinds, id);
            kind.model = form.get_int("model").value_or(-1);
            kind.frames = form.get_int("frames").value_or(1);
            kind.step = form.get_int("step").value_or(1);
            kind.scale_from_kind = form.get_int("scale_from_kind").value_or(0) != 0;
            kind.no_billboard = form.get_int("no_billboard").value_or(0) != 0;
            kind.flags = static_cast<std::uint32_t>(atom_hex(form.find("flags").child_at(1)));
        } else if (form.head().is_atom("flare-cell") && id < 12) {
            floats(form, "rect", data.flare_cells[id], 4);
        } else if (form.head().is_atom("impact-fx")) {
            ImpactRow& impact = row(data.impacts, id);
            impact.scale = one_float(form, "scale");
            impact.set = form.get_int("effect_set").value_or(-1);
            impact.mark = form.get_int("mark").value_or(0) != 0;
        } else if (form.head().is_atom("foot-landing") && id < 15) {
            const gsexp::Node sets = form.find("sets");
            for (int g = 0; g < 3; ++g) {
                data.foot_landing[id][g] = atom_int(sets.child_at(static_cast<std::size_t>(1 + g)));
            }
        } else if (form.head().is_atom("shot-look")) {
            ShotLook& look = row(data.shot_looks, id);
            look.billboard = form.get_int("billboard").value_or(0) != 0;
            look.model = form.get_int("model").value_or(-1);
            look.quad = form.get_int("quad").value_or(-1);
            look.flame = form.get_int("flame").value_or(0) == 1;
            look.scale = one_float(form, "scale");
            look.flame_z = one_float(form, "flame_z");
            floats(form, "flame_scale", look.flame_scale, 3);
        } else if (form.head().is_atom("flare-piece")) {
            FlarePiece& piece = row(data.flare_pieces, id);
            piece.cell = form.get_int("cell").value_or(0);
            piece.along = one_float(form, "along");
            piece.scale = one_float(form, "scale");
            piece.color = static_cast<std::uint32_t>(atom_hex(form.find("color").child_at(1)));
        }
    }
    if (data.records.empty() || data.lists.size() < 62 || data.sets.size() < 79) {
        error = "tables/effects.sexp is incomplete (run pc_extract --only effects)";
        return false;
    }
    return true;
}
