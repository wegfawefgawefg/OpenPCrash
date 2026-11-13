#include "hud/hud_data.hpp"

#include "cook/sexp_read.hpp"

#include <cstdio>

namespace {

// a table's rows in file order (HudRow, HudElemRow, SpriteSlot) and its plain values
struct RawTable {
    std::vector<HudRow> rows;
    std::vector<SpriteSlot> slots;
    std::vector<double> values;
};

HudRow read_row(const gsexp::FormView& row) {
    HudRow out;
    out.sprite = row.get_int("sprite").value_or(-1);
    out.x = row.get_float("x").value_or(0);
    out.y = row.get_float("y").value_or(0);
    out.scale_x = row.get_float("scale_x").value_or(1);
    out.scale_y = row.get_float("scale_y").value_or(1);
    out.blend = row.get_int("blend").value_or(0x101);
    out.color = atom_hex(row.find_arg("color", 0));
    out.world_x = row.get_float("world_x").value_or(0);
    out.world_y = row.get_float("world_y").value_or(0);
    out.world_yaw = row.get_float("world_yaw").value_or(0);
    return out;
}

bool read_tables(UiData& ui, const std::string& path, std::vector<std::string>& names,
                 std::vector<RawTable>& tables, std::string& error) {
    gsexp::ParseResult parsed;
    if (!ui_sexp(ui, path, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("table")) {
            continue;
        }
        RawTable table;
        const std::string layout(form.find_arg("layout", 0).text());
        for (gsexp::Node value : form.find("values").children()) {
            if (!value.is_atom("values") && !value.is_string()) {
                table.values.push_back(std::strtod(std::string(value.text()).c_str(), nullptr));
            }
        }
        for (gsexp::Node node : form.node().children()) {
            if (!node.is_list() || !node.head().is_atom("row")) {
                continue;
            }
            const gsexp::FormView row(node);
            if (layout == "SpriteSlot") {
                SpriteSlot slot;
                slot.sprite = row.get_int("sprite").value_or(-1);
                slot.x = row.get_float("x").value_or(0);
                slot.y = row.get_float("y").value_or(0);
                slot.blend = row.get_int("blend").value_or(0);
                slot.color = atom_hex(row.find_arg("color", 0));
                slot.z = row.get_float("z").value_or(0);
                slot.flags = atom_hex(row.find_arg("flags", 0));
                table.slots.push_back(slot);
            } else {
                table.rows.push_back(read_row(row));
            }
        }
        names.emplace_back(form.arg(0).text());
        tables.push_back(std::move(table));
    }
    return true;
}

const RawTable* find(const std::vector<std::string>& names, const std::vector<RawTable>& tables,
                     const std::string& name) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return &tables[i];
        }
    }
    std::fprintf(stderr, "hud: no table %s\n", name.c_str());
    return nullptr;
}

template <std::size_t N>
void copy_rows(const RawTable* table, std::array<HudRow, N>& out) {
    for (std::size_t i = 0; table && i < N && i < table->rows.size(); ++i) {
        out[i] = table->rows[i];
    }
}

template <typename T, std::size_t N>
void copy_values(const RawTable* table, std::array<T, N>& out) {
    for (std::size_t i = 0; table && i < N && i < table->values.size(); ++i) {
        out[i] = static_cast<T>(table->values[i]);
    }
}

template <std::size_t N>
void copy_pairs(const RawTable* table, std::array<std::array<int, 2>, N>& out) {
    for (std::size_t i = 0; table && i < N && 2 * i + 1 < table->values.size(); ++i) {
        out[i] = {static_cast<int>(table->values[2 * i]), static_cast<int>(table->values[2 * i + 1])};
    }
}

// a core table's forms by head, each its id's row
bool read_records(UiData& ui, const std::string& path, const char* head,
                  std::vector<gsexp::FormView>& out, gsexp::ParseResult& parsed,
                  std::string& error) {
    if (!ui_sexp(ui, path, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom(head)) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0) {
            continue;
        }
        if (static_cast<int>(out.size()) <= id) {
            out.resize(static_cast<std::size_t>(id) + 1, form);
        }
        out[static_cast<std::size_t>(id)] = form;
    }
    return true;
}

std::string string_or_empty(gsexp::Node node) {
    return node.valid() && node.is_string() ? std::string(node.text()) : std::string();
}

bool load_names(HudData& data, UiData& ui, std::string& error) {
    gsexp::ParseResult parsed;
    std::vector<gsexp::FormView> forms;
    if (!read_records(ui, "tables/identities.sexp", "identity", forms, parsed, error)) {
        return false;
    }
    for (const gsexp::FormView& form : forms) {
        data.identity_name_en.push_back(string_or_empty(form.find_arg("name_en", 0)));
        data.identity_name.push_back(string_or_empty(form.find_arg("name", 0)));
        data.identity_sprite.push_back(form.get_int("sprite").value_or(-1));
    }
    forms.clear();
    gsexp::ParseResult pilots;
    if (!read_records(ui, "tables/pilots.sexp", "pilot", forms, pilots, error)) {
        return false;
    }
    for (const gsexp::FormView& form : forms) {
        data.pilot_identity.push_back(form.get_int("identity").value_or(-1));
        data.pilot_scoobee.push_back(form.get_int("scoobee_identity").value_or(-1));
        data.pilot_reaction.push_back(form.get_int("reaction").value_or(0));
    }
    forms.clear();
    gsexp::ParseResult chips;
    if (!read_records(ui, "tables/chips.sexp", "chip", forms, chips, error)) {
        return false;
    }
    for (const gsexp::FormView& form : forms) {
        data.chip_bark_profile.push_back(form.get_int("bark_profile").value_or(0));
        data.chip_identity.push_back(form.get_int("family").value_or(-1));
        data.chip_name.push_back(string_or_empty(form.find_arg("name", 0)));
    }
    forms.clear();
    gsexp::ParseResult profiles;
    if (!read_records(ui, "tables/reaction_profiles.sexp", "reaction_profile", forms, profiles,
                      error)) {
        return false;
    }
    for (const gsexp::FormView& form : forms) {
        std::array<std::uint32_t, 35> barks{};
        int kind = 0;
        for (gsexp::Node node : form.find("barks").children()) {
            if (node.is_atom("barks")) {
                continue;
            }
            if (kind < 35) {
                barks[static_cast<std::size_t>(kind++)] = node.is_atom("nil") ? 0 : atom_hex(node);
            }
        }
        data.reaction_barks.push_back(barks);
    }
    return true;
}

} // namespace

bool load_hud_data(HudData& data, UiData& ui, std::string& error) {
    std::vector<std::string> names;
    std::vector<RawTable> tables;
    if (!read_tables(ui, "ui/layouts/hud.sexp", names, tables, error) ||
        !read_tables(ui, "ui/layouts/battle.sexp", names, tables, error)) {
        return false;
    }
    copy_rows(find(names, tables, "hud_layout_1p"), data.layout_1p);
    copy_rows(find(names, tables, "hud_layout_2p"), data.layout_2p);
    copy_rows(find(names, tables, "hud_layout_4p"), data.layout_4p);
    if (const RawTable* weapon = find(names, tables, "hud_weapon_layouts")) {
        for (std::size_t i = 0; i < weapon->rows.size() && i < 5 * 22; ++i) {
            data.weapon[i / 22][i % 22] = weapon->rows[i];
        }
    }
    copy_rows(find(names, tables, "hud_vs_marks_1view"), data.vs_marks_1view);
    copy_rows(find(names, tables, "hud_vs_marks_2view"), data.vs_marks_2view);
    copy_values(find(names, tables, "hud_ammo_digits_1view_left"), data.digits_1view_left);
    copy_values(find(names, tables, "hud_ammo_digits_1view_right"), data.digits_1view_right);
    copy_values(find(names, tables, "hud_ammo_digits_left"), data.digits_left);
    copy_values(find(names, tables, "hud_ammo_digits_4view_right"), data.digits_4view_right);
    copy_values(find(names, tables, "hud_ammo_digits_2view_right"), data.digits_2view_right);
    copy_pairs(find(names, tables, "hud_meter_walk_cells"), data.walk_cells);
    copy_pairs(find(names, tables, "hud_meter_bar_cells"), data.bar_cells);
    copy_pairs(find(names, tables, "hud_meter_speed_cells"), data.speed_cells);
    copy_pairs(find(names, tables, "hud_meter_jump_cells"), data.jump_cells);
    if (const RawTable* arrows = find(names, tables, "hud_cloak_arrows")) {
        for (std::size_t i = 0; i < 4 && 3 * i + 2 < arrows->values.size(); ++i) {
            for (std::size_t k = 0; k < 3; ++k) {
                data.cloak_arrows[i][k] = static_cast<float>(arrows->values[3 * i + k]);
            }
        }
    }
    copy_values(find(names, tables, "hud_view_switch_dx"), data.view_dx);
    copy_values(find(names, tables, "hud_view_switch_dworld"), data.view_dworld);
    if (const RawTable* sprites = find(names, tables, "battle_sprites")) {
        data.battle_sprites = sprites->slots;
    }
    // vs_empty_view_x (one float) runs on into vs_empty_view_y: (x, y) pairs for the four quarters
    const RawTable* empty_x = find(names, tables, "vs_empty_view_x");
    const RawTable* empty_y = find(names, tables, "vs_empty_view_y");
    if (empty_x && empty_y) {
        std::vector<double> run = empty_x->values;
        run.insert(run.end(), empty_y->values.begin(), empty_y->values.end());
        for (std::size_t i = 0; i < 4 && 2 * i + 1 < run.size(); ++i) {
            data.vs_empty_view[i] = {static_cast<float>(run[2 * i]),
                                     static_cast<float>(run[2 * i + 1])};
        }
    }
    return load_names(data, ui, error);
}
