#include "battle/scene_curves.hpp"

#include "assets/pack_sexp.hpp"
#include "battle/sexp_fx.hpp"

namespace {

constexpr const char* file_name = "tables/scene_cameras.sexp";

void read_curve(gsexp::Node node, MotionCurve& curve) {
    curve = MotionCurve{};
    if (!node.valid() || !node.is_list() || !node.head().is_atom("curve")) {
        return;
    }
    curve.present = true;
    curve.mode = atom_hex(node.child_at(1));
    const gsexp::FormView form(node);
    for (gsexp::Node time : form.find("times").children()) {
        if (!time.is_list() && !time.is_atom("times")) {
            curve.times.push_back(static_cast<std::uint16_t>(atom_int(time)));
        }
    }
    for (gsexp::Node value : form.find("values").children()) {
        if (!value.is_list() && !value.is_atom("values")) {
            curve.values.push_back(atom_float(value));
        }
    }
}

// the same curve as the world reads it: each value's bits to Q16.16 (fx_bits)
void read_fx_curve(gsexp::Node node, FxCurve& curve) {
    curve = FxCurve{};
    if (!node.valid() || !node.is_list() || !node.head().is_atom("curve")) {
        return;
    }
    curve.present = true;
    curve.mode = atom_hex(node.child_at(1));
    const gsexp::FormView form(node);
    for (gsexp::Node time : form.find("times").children()) {
        if (!time.is_list() && !time.is_atom("times")) {
            curve.times.push_back(static_cast<std::uint16_t>(atom_int(time)));
        }
    }
    for (gsexp::Node value : form.find("values").children()) {
        if (!value.is_list() && !value.is_atom("values")) {
            curve.values.push_back(atom_fx(value));
        }
    }
}

// each (scene STATE ...) form with its state in range
template <class F> bool each_scene(const Pack& core, std::string& error, F&& f) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, file_name, parsed, error)) {
        error += " (run pc_extract --only scenes)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || !form.head().is_atom("scene")) {
            continue;
        }
        const int state = atom_int(form.child_at(1));
        if (state >= 0 && state < scene_state_count) {
            f(state, gsexp::FormView(form));
        }
    }
    return true;
}

} // namespace

bool load_scene_walks(SceneWalks& out, const Pack& core, std::string& error) {
    out = SceneWalks{};
    return each_scene(core, error, [&](int state, const gsexp::FormView& scene) {
        const gsexp::FormView camera(scene.find("camera"));
        if (camera.node().valid()) {
            out.duration[state] = camera.get_int("duration").value_or(0);
        }
        const gsexp::Node root = scene.find("root");
        const gsexp::Node translate = gsexp::FormView(root).find("translate");
        if (root.valid() && translate.valid()) {
            SceneWalks::Root& r = out.roots[state];
            read_fx_curve(translate.child_at(1), r.x);
            read_fx_curve(translate.child_at(2), r.y);
            read_fx_curve(translate.child_at(3), r.z);
            const gsexp::Node rotate = gsexp::FormView(root).find("rotate");
            if (rotate.valid()) {
                read_fx_curve(rotate.child_at(2), r.yaw);
                r.turns = true;
            }
            r.present = true;
        }
    });
}

bool load_scene_cameras(SceneCamera (&out)[scene_state_count], const Pack& core,
                        std::string& error) {
    for (SceneCamera& c : out) {
        c = SceneCamera{};
    }
    return each_scene(core, error, [&](int state, const gsexp::FormView& scene) {
        const gsexp::FormView camera(scene.find("camera"));
        if (!camera.node().valid()) {
            return;
        }
        SceneCamera& c = out[state];
        c.present = true;
        c.relative = scene.get_int("relative").value_or(0) != 0;
        c.duration = camera.get_int("duration").value_or(0);
        const gsexp::Node curves = camera.find("curves");
        for (int k = 0; k < 8; ++k) {
            read_curve(curves.child_at(static_cast<std::size_t>(1 + k)), c.curves[k]);
        }
    });
}
