// data/tables/stage_scene.sexp and data/collision/*.sexp read back, for the cook.
#include "cook/cook_read.hpp"
#include "cook/sexp_read.hpp"

bool read_stage_tables(StageTables& tables, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/stage_scene.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("lod-distances")) {
            tables.lod_mid = form.get_float("mid").value_or(120);
            tables.lod_far = form.get_float("far").value_or(200);
            continue;
        }
        if (!form.head().is_atom("stage")) {
            continue;
        }
        const int selector = form.get_int("selector").value_or(-1);
        if (selector < 0 || selector >= 8) {
            continue;
        }
        StageInfo& stage = tables.stages[selector];
        stage.name = form.get_string("name").value_or("");
        stage.model_set = form.get_int("model-set").value_or(-1);
        stage.collision = form.get_int("collision").value_or(-1);
        for (gsexp::Node point : form.find("entry-points").children()) {
            if (!point.is_list() || !point.head().is_atom("point")) {
                continue;
            }
            StageEntryPoint entry;
            for (int k = 0; k < 3; ++k) {
                entry.position[k] = atom_float(point.child_at(1 + k));
            }
            entry.yaw = gsexp::FormView(point).get_float("yaw").value_or(0);
            stage.entry_points.push_back(entry);
        }
        for (gsexp::Node row : form.find("objects").children()) {
            if (!row.is_list() || !row.head().is_atom("object")) {
                continue;
            }
            const gsexp::FormView object_form(row);
            StageObject object;
            object.model = object_form.get_int("model").value_or(-1);
            object.layer = object_form.get_int("layer").value_or(2);
            object.submit = object_form.get_int("submit").value_or(1);
            object.animate = object_form.get_int("animate").value_or(0);
            const gsexp::Node lods = object_form.find("lods");
            const gsexp::Node zones = object_form.find("zones");
            for (int k = 0; k < 3; ++k) {
                object.lods[k] = atom_int(lods.child_at(1 + k));
            }
            for (int k = 0; k < 5; ++k) {
                object.zones[k] = atom_int(zones.child_at(1 + k));
            }
            stage.objects.push_back(object);
        }
    }
    return true;
}

namespace {

void read_triangles(gsexp::Node form, std::vector<CollisionTriangle>& out) {
    for (gsexp::Node row : form.children()) {
        if (!row.is_list() || row.child_count() < 13) {
            continue;
        }
        CollisionTriangle triangle;
        for (int k = 0; k < 9; ++k) {
            triangle.v[k / 3][k % 3] = atom_float(row.child_at(k));
        }
        for (int k = 0; k < 3; ++k) {
            triangle.normal[k] = atom_float(row.child_at(9 + k));
        }
        triangle.type = atom_int(row.child_at(12));
        out.push_back(triangle);
    }
}

void read_ints(gsexp::Node form, std::vector<std::int32_t>& out) {
    bool head = true;
    for (gsexp::Node value : form.children()) {
        if (!head) {
            out.push_back(atom_int(value));
        }
        head = false;
    }
}

void read_floats(gsexp::Node form, float* out, int count) {
    for (int k = 0; k < count; ++k) {
        out[k] = atom_float(form.child_at(1 + k));
    }
}

} // namespace

bool read_collision_grid(const std::string& path, CollisionGrid& out, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(path, parsed, error)) {
        return false;
    }
    const gsexp::FormView grid(parsed.root(0));
    out.asset = grid.get_int("asset").value_or(-1);
    const gsexp::Node dims = grid.find("dims");
    for (int k = 0; k < 3; ++k) {
        out.dims[k] = atom_int(dims.child_at(1 + k));
    }
    read_floats(grid.find("cell"), out.cell, 3);
    read_floats(grid.find("centre-original"), out.centre_original, 3);
    read_triangles(grid.find("triangles"), out.triangles);
    read_ints(grid.find("cells"), out.cells);
    read_ints(grid.find("refs"), out.refs);
    read_triangles(grid.find("floor-triangles"), out.floor_triangles);
    read_ints(grid.find("floor-cells"), out.floor_cells);
    read_ints(grid.find("floor-refs"), out.floor_refs);
    const gsexp::Node bits = grid.find("surface-bits");
    for (int type = 0; type < 11 && bits.valid(); ++type) {
        out.surface_bits[type] = atom_hex(bits.child_at(1 + type));
    }
    return true;
}
