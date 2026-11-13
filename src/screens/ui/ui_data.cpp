#include "screens/ui/ui_data.hpp"

#include "assets/pack_sexp.hpp"
#include "cook/pack_library.hpp"
#include "cook/sexp_read.hpp"

#include <algorithm>
#include <cstdio>

namespace {

MsgBoxRow read_box(const UiData& data, const gsexp::FormView& row) {
    MsgBoxRow box;
    int line = 0;
    for (gsexp::Node text : row.find("lines").children()) {
        if (text.is_string() && line < 6) {
            box.lines[line++] = utf8_to_glyphs(data, std::string(text.text()));
        }
    }
    box.x = row.get_int("x").value_or(0);
    box.y = row.get_int("y").value_or(0);
    // text_layer: the decomp's name since 66ea3c1, columns before
    box.columns = row.get_int("text_layer").value_or(row.get_int("columns").value_or(0));
    box.size = row.get_int("size").value_or(18);
    box.step = row.get_int("step").value_or(18);
    box.color = atom_hex(row.find_arg("color", 0));
    box.style = row.get_int("style").value_or(0);
    box.blend = row.get_int("blend").value_or(0);
    box.cx = row.get_float("cx").value_or(320);
    box.cy = row.get_float("cy").value_or(240);
    box.sx = row.get_float("sx").value_or(0.1f);
    box.sy = row.get_float("sy").value_or(0.1f);
    box.done = row.get_int("done").value_or(0) != 0;
    return box;
}

void load_layout(UiData& data, const std::string& screen, UiFile& file) {
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(data, "ui/layouts/" + screen + ".sexp", parsed, error)) {
        std::fprintf(stderr, "ui: %s\n", error.c_str());
        return;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("table")) {
            continue;
        }
        UiTable table;
        table.name = std::string(form.arg(0).text());
        table.va = atom_hex(form.find_arg("va", 0));
        table.layout = std::string(form.find_arg("layout", 0).text());
        table.count = form.get_int("count").value_or(0);
        for (gsexp::Node value : form.find("values").children()) {
            if (value.is_atom("values")) {
                continue;
            }
            if (value.is_string() || table.layout == "pointer") {
                table.strings.emplace_back(value.text());
            } else if (table.layout == "string") {
                table.strings.emplace_back(); // a null pointer in a string table
            } else {
                table.values.push_back(std::strtod(std::string(value.text()).c_str(), nullptr));
            }
        }
        for (gsexp::Node node : form.node().children()) {
            if (!node.is_list() || !node.head().is_atom("row")) {
                continue;
            }
            const gsexp::FormView row(node);
            if (table.layout == "SpriteSlot") {
                SpriteSlot slot;
                slot.sprite = row.get_int("sprite").value_or(-1);
                slot.x = row.get_float("x").value_or(0);
                slot.y = row.get_float("y").value_or(0);
                slot.blend = row.get_int("blend").value_or(0);
                slot.color = atom_hex(row.find_arg("color", 0));
                slot.z = row.get_float("z").value_or(0);
                slot.flags = atom_hex(row.find_arg("flags", 0));
                table.slots.push_back(slot);
            } else if (table.layout.rfind("MsgBox", 0) == 0) {
                table.boxes.push_back(read_box(data, row));
            } else if (table.layout == "NumberFontEntry") {
                table.pairs.push_back(
                    {row.get_int("sprite").value_or(-1), row.get_int("advance").value_or(0)});
            } else {
                // other records (Words_*, IdPair...): every field's values in order, the text of
                // strings and symbols beside them (their number 0)
                std::vector<double> fields;
                std::vector<std::string> texts;
                for (gsexp::Node field : node.children()) {
                    if (!field.is_list()) {
                        continue;
                    }
                    for (std::size_t k = 1; k < field.child_count(); ++k) {
                        const gsexp::Node value = field.child_at(k);
                        const std::string text(value.text());
                        char* end = nullptr;
                        const double number = std::strtod(text.c_str(), &end);
                        const bool numeric =
                            !value.is_string() && !text.empty() && end && *end == 0;
                        fields.push_back(numeric ? number : 0.0);
                        texts.push_back(numeric ? std::string() : text);
                    }
                }
                table.rows.push_back(std::move(fields));
                table.row_strings.push_back(std::move(texts));
                if (table.layout.rfind("Words_", 0) == 0) {
                    // a stretch of words no reader typed: its fields as plain numbers, an address
                    // (D_xxxxxxxx) as the word it is, so ui_values can read through it
                    for (gsexp::Node field : node.children()) {
                        if (!field.is_list() || field.child_count() < 2) {
                            continue;
                        }
                        const std::string text(field.child_at(1).text());
                        table.values.push_back(text.rfind("D_", 0) == 0
                                                   ? static_cast<double>(std::strtoul(
                                                         text.c_str() + 2, nullptr, 16))
                                                   : std::strtod(text.c_str(), nullptr));
                    }
                }
            }
        }
        file.tables.push_back(std::move(table));
    }
}

void load_timeline(UiData& data, const std::string& name) {
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(data, "ui/timelines/" + name + ".sexp", parsed, error)) {
        std::fprintf(stderr, "ui: %s\n", error.c_str());
        return;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("timeline")) {
            continue;
        }
        Timeline timeline;
        timeline.fps = form.get_int("fps").value_or(60);
        for (gsexp::Node part : form.find("parts").children()) {
            if (part.is_list()) {
                const gsexp::FormView p(part);
                timeline.part_sprites.push_back(p.get_int("sprite").value_or(-1));
                timeline.part_depths.push_back(p.get_int("depth").value_or(0));
            }
        }
        for (gsexp::Node node : form.node().children()) {
            if (!node.is_list() || !node.head().is_atom("frame")) {
                continue;
            }
            TimelineFrame frame;
            for (gsexp::Node command : node.children()) {
                if (!command.is_list()) {
                    continue;
                }
                ++frame.command_count;
                const gsexp::FormView c(command);
                if (command.head().is_atom("background")) {
                    frame.has_background = true;
                    frame.background = atom_hex(c.arg(0));
                } else if (command.head().is_atom("place")) {
                    TimelinePlace place;
                    place.depth = c.get_int("depth").value_or(0);
                    place.move = c.get_int("move").value_or(0);
                    const gsexp::Node m = c.find("matrix");
                    const gsexp::Node colour = c.find("color");
                    for (int k = 0; k < 4; ++k) {
                        place.matrix[k] = atom_float(m.child_at(1 + k));
                        place.color[k] = atom_float(colour.child_at(1 + k));
                    }
                    place.x = c.get_float("x").value_or(0);
                    place.y = c.get_float("y").value_or(0);
                    frame.places.push_back(place);
                }
            }
            timeline.frames.push_back(std::move(frame));
        }
        data.timelines[name] = std::move(timeline);
    }
}

std::size_t element_size(const UiTable& table) {
    const std::string& layout = table.layout;
    if (layout == "short" || layout == "unsigned short") {
        return 2;
    }
    if (layout.rfind("Words_", 0) == 0 && table.count > 0) {
        return 4 * table.values.size() / static_cast<std::size_t>(table.count);
    }
    return layout == "int" || layout == "float" || layout == "pointer" || layout == "string" ? 4
                                                                                             : 0;
}

} // namespace

bool load_ui_data(UiData& data, PackLibrary& packs, const std::string& data_dir,
                  std::string& error) {
    data.data_dir = data_dir;
    data.packs = &packs;
    // where every file went: core's own first, then the plan's packs in order
    for (std::uint32_t i = 0; i < packs.core.header->entry_count; ++i) {
        data.pack_of.emplace(pack_string(packs.core, packs.core.entries[i].name), "core");
    }
    for (const PackPlan& pack : packs.plan.packs) {
        for (const CookItem& item : pack.items) {
            if (!item.path.empty()) {
                data.pack_of.emplace(item.path, pack.name);
            }
        }
    }
    // the registry's PNGs by id (ui/images.sexp, in core)
    gsexp::ParseResult parsed;
    if (!ui_sexp(data, "ui/images.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("image")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0 || id > 0x10000) {
            continue;
        }
        if (static_cast<int>(data.image_file.size()) <= id) {
            data.image_file.resize(static_cast<std::size_t>(id) + 1);
        }
        data.image_file[static_cast<std::size_t>(id)] =
            "ui/images/" + form.get_string("file").value_or("");
    }
    return load_talk_font(data.font, data_dir, error);
}

const Pack* ui_pack_for(UiData& data, const std::string& path) {
    const auto found = data.pack_of.find(path);
    if (found == data.pack_of.end()) {
        std::fprintf(stderr, "ui: no pack holds %s\n", path.c_str());
        return nullptr;
    }
    std::string error;
    const Pack* pack = pack_open(*data.packs, found->second, error);
    if (!pack) {
        std::fprintf(stderr, "ui: %s\n", error.c_str());
        return nullptr;
    }
    if (found->second != "core" &&
        std::find(data.opened.begin(), data.opened.end(), found->second) == data.opened.end()) {
        data.opened.push_back(found->second);
    }
    return pack;
}

bool ui_sexp(UiData& data, const std::string& path, gsexp::ParseResult& out, std::string& error) {
    const Pack* pack = ui_pack_for(data, path);
    if (!pack) {
        error = "no pack holds " + path;
        return false;
    }
    return pack_sexp(*pack, path, out, error);
}

void ui_close_screen_packs(UiData& data) {
    for (const std::string& name : data.opened) {
        pack_close(*data.packs, name);
    }
    data.opened.clear();
}

const UiFile& ui_file(UiData& data, const std::string& screen) {
    auto found = data.files.find(screen);
    if (found != data.files.end()) {
        return found->second;
    }
    UiFile& file = data.files[screen];
    load_layout(data, screen, file);
    return file;
}

const UiTable* ui_table(UiData& data, const std::string& screen, const std::string& name) {
    for (const UiTable& table : ui_file(data, screen).tables) {
        if (table.name == name) {
            return &table;
        }
    }
    // a D_<address> name the decomp has since named: the table at that address
    if (name.size() == 10 && name.compare(0, 2, "D_") == 0) {
        const unsigned va = static_cast<unsigned>(std::strtoul(name.c_str() + 2, nullptr, 16));
        for (const UiTable& table : ui_file(data, screen).tables) {
            if (table.va == va) {
                return &table;
            }
        }
    }
    std::fprintf(stderr, "ui: no table %s in layouts/%s.sexp\n", name.c_str(), screen.c_str());
    return nullptr;
}

std::vector<double> ui_values(UiData& data, const std::string& screen, const std::string& name,
                              int count) {
    std::vector<double> out;
    const UiFile& file = ui_file(data, screen);
    std::size_t i = 0;
    const UiTable* first = ui_table(data, screen, name);
    while (i < file.tables.size() && &file.tables[i] != first) {
        ++i;
    }
    unsigned next_va = 0;
    for (; i < file.tables.size() && static_cast<int>(out.size()) < count; ++i) {
        const UiTable& table = file.tables[i];
        if (next_va && table.va != next_va) {
            break;
        }
        out.insert(out.end(), table.values.begin(), table.values.end());
        next_va = table.va + static_cast<unsigned>(element_size(table) * table.count);
    }
    out.resize(static_cast<std::size_t>(count), 0.0);
    return out;
}

std::vector<std::string> ui_strings(UiData& data, const std::string& screen,
                                    const std::string& name, int skip, int count) {
    std::vector<std::string> out;
    const UiFile& file = ui_file(data, screen);
    std::size_t i = 0;
    const UiTable* first = ui_table(data, screen, name);
    while (i < file.tables.size() && &file.tables[i] != first) {
        ++i;
    }
    unsigned next_va = 0;
    for (; i < file.tables.size() && static_cast<int>(out.size()) < skip + count; ++i) {
        const UiTable& table = file.tables[i];
        if ((next_va && table.va != next_va) || table.layout != "string") {
            break;
        }
        out.insert(out.end(), table.strings.begin(), table.strings.end());
        next_va = table.va + static_cast<unsigned>(4 * table.count);
    }
    out.resize(static_cast<std::size_t>(skip + count));
    return {out.begin() + skip, out.end()};
}

const SpriteSlot* ui_slots(UiData& data, const std::string& screen, const std::string& name,
                           int row) {
    const UiTable* table = ui_table(data, screen, name);
    if (!table || row >= static_cast<int>(table->slots.size())) {
        return nullptr;
    }
    return &table->slots[static_cast<std::size_t>(row)];
}

const MsgBoxRow* ui_box(UiData& data, const std::string& screen, const std::string& name,
                        int row) {
    const UiTable* table = ui_table(data, screen, name);
    if (!table || row >= static_cast<int>(table->boxes.size())) {
        return nullptr;
    }
    return &table->boxes[static_cast<std::size_t>(row)];
}

const Timeline* ui_timeline(UiData& data, const std::string& name) {
    if (!data.timelines.count(name)) {
        load_timeline(data, name);
    }
    auto found = data.timelines.find(name);
    return found == data.timelines.end() ? nullptr : &found->second;
}

std::vector<std::int16_t> utf8_to_glyphs(const UiData& data, const std::string& text) {
    // the table strings carry no markup, so this is sjis_to_glyphs' search; the -1 end dropped
    GlyphText glyphs = glyphs_from_text(data.font, text);
    while (!glyphs.empty() && glyphs.back() == -1) {
        glyphs.pop_back();
    }
    return glyphs;
}
