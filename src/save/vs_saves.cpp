#include "save/vs_saves.hpp"

#include "save/save_read.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using namespace save_read;

void put_list(std::string& out, const char* name, const std::int16_t* values, int count) {
    out += " (";
    out += name;
    for (int i = 0; i < count && values[i] != -1; ++i) {
        out += " " + std::to_string(values[i]);
    }
    out += ")";
}

void put_part(std::string& out, const char* name, int id, int tune) {
    out += " (" + std::string(name) + " " + std::to_string(id) + " " + std::to_string(tune) + ")";
}

std::string vs_save_text(const VsSave& s) {
    const RosterBuild& b = s.build;
    std::string out = "(vs-save (version 1) (written " + std::to_string(s.written) + ")\n ";
    put_list(out, "mech", s.mech, glyph_name_length);
    put_list(out, "chip-name", s.chip, glyph_name_length);
    put_list(out, "pilot", s.pilot, pilot_name_length);
    out += "\n ";
    put_part(out, "body", b.body, b.body_tune);
    put_part(out, "leg", b.leg, b.leg_tune);
    put_part(out, "chip", b.chip, b.chip_tune);
    out += " (weapons";
    for (const RosterWeapon& w : b.weapon) {
        out += " (" + std::to_string(w.id) + " " + std::to_string(w.tune) + ")";
    }
    out += ")\n (color " + std::to_string(b.color) + ") (sticker " + std::to_string(b.sticker) +
           ") (option";
    for (const std::int16_t o : b.option) {
        out += " " + std::to_string(o);
    }
    out += ")\n (buttons " + std::to_string(s.buttons.control) + " " +
           std::to_string(s.buttons.view_change) + " " + std::to_string(s.buttons.beginner);
    for (const int key : s.buttons.keys) {
        out += " " + std::to_string(key);
    }
    out += "))\n";
    return out;
}

// (buttons control view-change beginner key0..key7), left as the default when missing
bool read_buttons(In& in, FormView form, ButtonConfig& b) {
    const Node node = form.find("buttons");
    if (!node.is_list()) {
        return true;
    }
    if (node.child_count() != 4 + button_action_count) {
        return false;
    }
    ButtonConfig read;
    bool ok = number_as(in, node.child_at(1), read.control) &&
              number_as(in, node.child_at(2), read.view_change) &&
              number_as(in, node.child_at(3), read.beginner);
    for (int k = 0; k < button_action_count && ok; ++k) {
        ok = number_as(in, node.child_at(static_cast<std::size_t>(4 + k)), read.keys[k]) &&
             read.keys[k] >= 0 && read.keys[k] < button_action_count;
    }
    if (ok) {
        b = read;
    }
    return ok;
}

bool part(In& in, FormView form, std::string_view name, std::int16_t& id, std::int8_t& tune) {
    const Node node = form.find(name);
    return number_as(in, node.child_at(1), id) && number_as(in, node.child_at(2), tune);
}

bool vs_save_parse(const std::string& text, VsSave& s) {
    const gsexp::ParseResult parsed = gsexp::parse(text);
    if (!parsed.ok || parsed.root_count() != 1) {
        return false;
    }
    const FormView form(parsed.root(0));
    In in;
    int version = 0;
    RosterBuild& b = s.build;
    const Node weapons = form.find("weapons");
    if (!form.head().is_atom("vs-save") || !field(in, form, "version", version) || version != 1 ||
        !field(in, form, "written", s.written) ||
        !glyphs(in, form, "mech", s.mech, glyph_name_length) ||
        !glyphs(in, form, "chip-name", s.chip, glyph_name_length) ||
        !glyphs(in, form, "pilot", s.pilot, pilot_name_length) ||
        !part(in, form, "body", b.body, b.body_tune) || !part(in, form, "leg", b.leg, b.leg_tune) ||
        !part(in, form, "chip", b.chip, b.chip_tune) || !field(in, form, "color", b.color) ||
        !field(in, form, "sticker", b.sticker) ||
        !list(in, form, "option", b.option, 10, std::int16_t{-1}, true) ||
        weapons.child_count() != 5 || !read_buttons(in, form, s.buttons)) {
        return false;
    }
    for (int w = 0; w < 4; ++w) {
        const Node weapon = weapons.child_at(static_cast<std::size_t>(w) + 1);
        if (!number_as(in, weapon.child_at(0), b.weapon[w].id) ||
            !number_as(in, weapon.child_at(1), b.weapon[w].tune)) {
            return false;
        }
    }
    return true;
}

std::string file_of(const std::string& save_dir, const std::string& name) {
    return vs_save_dir(save_dir) + "/" + name + ".sexp";
}

} // namespace

std::string vs_save_dir(const std::string& save_dir) {
    return save_dir + "/vs";
}

std::vector<VsSave> vs_saves_list(const std::string& save_dir) {
    std::vector<VsSave> saves;
    std::error_code code;
    const std::filesystem::path dir(vs_save_dir(save_dir));
    if (!std::filesystem::is_directory(dir, code)) {
        return saves;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (!entry.is_regular_file(code) || entry.path().extension() != ".sexp") {
            continue;
        }
        VsSave save;
        save.name = entry.path().stem().string();
        std::ifstream in(entry.path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        save.present = in && vs_save_parse(text.str(), save);
        saves.push_back(save);
    }
    // retail lists in the drive's own order, a new save going last: oldest first here, by name
    // among equals
    std::sort(saves.begin(), saves.end(), [](const VsSave& a, const VsSave& b) {
        return a.written != b.written ? a.written < b.written : a.name < b.name;
    });
    return saves;
}

bool vs_save_store(const std::string& save_dir, const VsSave& save, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(vs_save_dir(save_dir), code);
    const std::string path = file_of(save_dir, save.name);
    const std::string temporary = path + ".tmp";
    const std::string text = vs_save_text(save);
    bool ok = false;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        ok = static_cast<bool>(out);
    }
    if (ok) {
        std::filesystem::rename(temporary, path, code);
        ok = !code;
    }
    if (!ok) {
        error = "can't write " + path;
        std::filesystem::remove(temporary, code);
    }
    return ok;
}

bool vs_save_remove(const std::string& save_dir, const std::string& name) {
    std::error_code code;
    return std::filesystem::remove(file_of(save_dir, name), code) && !code;
}
