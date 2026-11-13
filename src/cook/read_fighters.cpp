// data/tables/fighter_assets.sexp and sv_graphs.sexp read back, for the cook.
#include "cook/cook_read.hpp"
#include "cook/sexp_read.hpp"

#include <map>

namespace {

std::vector<int> int_list(gsexp::Node form) {
    std::vector<int> out;
    bool head = true;
    for (gsexp::Node child : form.children()) {
        if (!head) {
            out.push_back(atom_int(child));
        }
        head = false;
    }
    return out;
}

// (name (row class "label") ...) -> class by row
std::vector<int> class_list(gsexp::Node form) {
    std::vector<int> out;
    for (gsexp::Node row : form.children()) {
        if (row.is_list()) {
            const int index = atom_int(row.child_at(0));
            if (index >= static_cast<int>(out.size())) {
                out.resize(index + 1, -1);
            }
            out[index] = atom_int(row.child_at(1));
        }
    }
    return out;
}

// (block asset (points n) (stream k (x y z node) ...) x 4 [(frames open close)]
//  [(clip frames (channel node flags (scale c c c) (rotate c c c) (translate c c c)) ...)])
void read_weapon_block(gsexp::Node row, WeaponBlock& block) {
    block.asset = atom_int(row.child_at(1));
    for (gsexp::Node part : row.children()) {
        if (!part.is_list()) {
            continue;
        }
        if (part.head().is_atom("stream")) {
            const int k = atom_int(part.child_at(1));
            for (std::size_t i = 2; k >= 0 && k < 4 && i < part.child_count(); ++i) {
                const gsexp::Node point = part.child_at(i);
                WeaponMuzzle& m = block.streams[k].emplace_back();
                for (int c = 0; c < 3; ++c) {
                    m.point[c] = atom_float(point.child_at(c));
                }
                m.node = atom_int(point.child_at(3));
            }
        } else if (part.head().is_atom("frames")) {
            block.locked = true;
            block.open = atom_int(part.child_at(1));
            block.close = atom_int(part.child_at(2));
        } else if (part.head().is_atom("clip")) {
            block.clip = true;
            block.frames = atom_int(part.child_at(1));
            for (std::size_t i = 2; i < part.child_count(); ++i) {
                const gsexp::Node c = part.child_at(i);
                WeaponClipChannel& channel = block.channels.emplace_back();
                channel.node = atom_int(c.child_at(1));
                channel.flags = atom_hex(c.child_at(2));
                // child 3 is the scale group, which no clip has and the pose doesn't apply
                for (int axis = 0; axis < 3; ++axis) {
                    read_motion_curve(c.child_at(4).child_at(1 + axis), channel.rotate[axis]);
                    read_motion_curve(c.child_at(5).child_at(1 + axis), channel.translate[axis]);
                }
            }
        }
    }
}

bool load_graph_nodes(FighterTables& tables, const std::string& data_dir, std::string& error,
                      std::map<std::uint32_t, int>& index_of) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/sv_graphs.sexp", parsed, error)) {
        return false;
    }
    std::vector<std::vector<std::uint32_t>> child_addresses;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("node")) {
            continue;
        }
        SvGraphNode node;
        node.address = atom_hex(form.arg(0));
        node.type = form.get_int("type").value_or(0);
        node.flags = atom_hex(form.find("flags").child_at(1));
        node.track = form.get_int("track").value_or(-1);
        const gsexp::Node translate = form.find("translate");
        const gsexp::Node rotate = form.find("rotate");
        for (int k = 0; k < 3; ++k) {
            node.translate[k] = atom_float(translate.child_at(1 + k));
            node.rotate[k] = atom_float(rotate.child_at(1 + k));
        }
        node.matrix = form.get_int("matrix").value_or(-1);
        node.spheres = form.get_int("spheres").value_or(-1);
        node.remap = form.get_int("remap").value_or(-1);
        node.f34 = form.get_int("f34").value_or(0);
        std::vector<std::uint32_t> children;
        bool head = true;
        for (gsexp::Node child : form.find("children").children()) {
            if (!head) {
                children.push_back(child.is_atom("weapon") ? 0xFFFFFFFFu : atom_hex(child));
            }
            head = false;
        }
        index_of[node.address] = static_cast<int>(tables.nodes.size());
        tables.nodes.push_back(node);
        child_addresses.push_back(children);
    }
    for (std::size_t i = 0; i < tables.nodes.size(); ++i) {
        for (std::uint32_t address : child_addresses[i]) {
            const auto found = index_of.find(address);
            tables.nodes[i].children.push_back(found == index_of.end() ? -1 : found->second);
        }
    }
    return true;
}

std::vector<int> graph_list(gsexp::Node form, const std::map<std::uint32_t, int>& index_of) {
    std::vector<int> out;
    bool head = true;
    for (gsexp::Node child : form.children()) {
        if (!head) {
            const auto found = index_of.find(atom_hex(child));
            out.push_back(found == index_of.end() ? -1 : found->second);
        }
        head = false;
    }
    return out;
}

} // namespace

bool read_fighter_tables(FighterTables& tables, const std::string& data_dir, std::string& error) {
    std::map<std::uint32_t, int> index_of;
    if (!load_graph_nodes(tables, data_dir, error, index_of)) {
        return false;
    }
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/fighter_assets.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        const std::string head(form.head().text());
        if (head == "body-models") {
            tables.body_models = int_list(form);
        } else if (head == "body-sticker-models") {
            tables.body_sticker_models = int_list(form);
        } else if (head == "leg-models") {
            tables.leg_models = int_list(form);
        } else if (head == "arm-a-models") {
            tables.arm_a_models = int_list(form);
        } else if (head == "arm-b-models") {
            tables.arm_b_models = int_list(form);
        } else if (head == "back-a-models") {
            tables.back_a_models = int_list(form);
        } else if (head == "back-b-models") {
            tables.back_b_models = int_list(form);
        } else if (head == "leg-motions") {
            tables.leg_motions = int_list(form);
        } else if (head == "body-motions") {
            tables.body_motions = int_list(form);
        } else if (head == "arm-a-motions") {
            tables.arm_a_motions = int_list(form);
        } else if (head == "arm-b-motions") {
            tables.arm_b_motions = int_list(form);
        } else if (head == "arm-motion-class") {
            tables.arm_motion_class = int_list(form);
        } else if (head == "arm-a-blocks") {
            tables.arm_a_blocks = int_list(form);
        } else if (head == "arm-b-blocks") {
            tables.arm_b_blocks = int_list(form);
        } else if (head == "back-a-blocks") {
            tables.back_a_blocks = int_list(form);
        } else if (head == "back-b-blocks") {
            tables.back_b_blocks = int_list(form);
        } else if (head == "weapon-muzzles") {
            // (asset (x y z node) ...) per block
            for (gsexp::Node row : form.children()) {
                if (!row.is_list()) {
                    continue;
                }
                WeaponBlockMuzzles& block = tables.block_muzzles.emplace_back();
                block.asset = atom_int(row.child_at(0));
                for (std::size_t k = 1; k < row.child_count(); ++k) {
                    const gsexp::Node point = row.child_at(k);
                    WeaponMuzzle& muzzle = block.muzzles.emplace_back();
                    for (int c = 0; c < 3; ++c) {
                        muzzle.point[c] = atom_float(point.child_at(c));
                    }
                    muzzle.node = atom_int(point.child_at(3));
                }
            }
        } else if (head == "weapon-blocks") {
            for (gsexp::Node row : form.children()) {
                if (row.is_list() && row.head().is_atom("block")) {
                    read_weapon_block(row, tables.blocks.emplace_back());
                }
            }
        } else if (head == "leg-graphs") {
            tables.leg_graphs = graph_list(form, index_of);
        } else if (head == "arm-a-graphs") {
            tables.arm_a_graphs = graph_list(form, index_of);
        } else if (head == "arm-b-graphs") {
            tables.arm_b_graphs = graph_list(form, index_of);
        } else if (head == "body-classes") {
            tables.body_class = class_list(form);
        } else if (head == "leg-classes") {
            tables.leg_class = class_list(form);
        } else if (head == "arm-classes") {
            tables.arm_class = class_list(form);
        } else if (head == "back-classes") {
            tables.back_class = class_list(form);
        } else if (head == "part-paints") {
            for (gsexp::Node leg : form.children()) {
                if (leg.is_list()) {
                    std::vector<int> ids = int_list(leg);
                    ids.erase(ids.begin()); // the leg class number
                    tables.part_paints.push_back(ids);
                }
            }
        } else if (head == "leg-attachments") {
            // (leg n asset (a.x a.y a.z selector b.x b.y b.z) x 14): row 13's point
            for (gsexp::Node leg : form.children()) {
                if (!leg.is_list() || leg.child_count() < 3 + 14) {
                    continue;
                }
                const gsexp::Node row = leg.child_at(3 + 13);
                tables.leg_head_points.push_back({atom_float(row.child_at(0)),
                                                  atom_float(row.child_at(1)),
                                                  atom_float(row.child_at(2))});
            }
        } else if (head == "builds") {
            for (gsexp::Node row : form.children()) {
                if (!row.is_list()) {
                    continue;
                }
                FighterBuild build;
                build.body = atom_int(row.child_at(1));
                build.leg = atom_int(row.child_at(2));
                const gsexp::Node weapons = row.child_at(3);
                for (int w = 0; w < 4; ++w) {
                    build.weapons[w] = atom_int(weapons.child_at(1 + w));
                }
                build.colour = atom_int(row.child_at(4));
                build.sticker = atom_int(row.child_at(5));
                build.name = std::string(row.child_at(6).text());
                tables.builds.push_back(build);
            }
        }
    }
    return true;
}
