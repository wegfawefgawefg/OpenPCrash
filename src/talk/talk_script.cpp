#include "talk/talk_script.hpp"

#include "cook/sexp_read.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// the nine fixed colours of codes -10..-18 (wide_text_draw)
constexpr std::uint32_t code_colors[9] = {0xFF66EEEE, 0xFFEE9500, 0xFFEE88EE,
                                          0xFF808080, 0xFF308080, 0xFF8E4500,
                                          0xFF66EE33, 0xFFEEEE66, 0xFF256584};

// the 28 portrait actors by their asset stems, in portrait_actor_table order
// (portrait_actor_table); the second nin and mask take a 2, as the scripts family writes them
const char* const character_stems[28] = {
    "yuki",  "roy",   "tenka",  "siba",   "dj",   "mil",    "maiko", "mion", "namb", "suku",
    "sagi",  "lunch", "pepper", "aturo",  "ed",   "glik",   "naomi", "nin",  "nin2", "mask",
    "mask2", "miz",   "red",    "goggle", "mini", "salary", "otaku", "board"};

// one UTF-8 code point at text[at], at moved past it
std::uint32_t next_code_point(const std::string& text, std::size_t& at) {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byte(at);
    int extra = lead < 0x80 ? 0 : lead < 0xE0 ? 1 : lead < 0xF0 ? 2 : 3;
    std::uint32_t code = extra == 0   ? lead
                         : extra == 1 ? lead & 0x1Fu
                         : extra == 2 ? lead & 0x0Fu
                                      : lead & 0x07u;
    ++at;
    for (; extra > 0 && at < text.size(); --extra, ++at) {
        code = code << 6 | (byte(at) & 0x3Fu);
    }
    return code;
}

// a {markup} name to its token
std::int16_t markup_token(const std::string& name) {
    if (name == "c") {
        return -9;
    }
    if (name.rfind("c:", 0) == 0) {
        const std::uint32_t color =
            static_cast<std::uint32_t>(std::strtoul(name.c_str() + 2, nullptr, 16));
        for (int k = 0; k < 9; ++k) {
            if (code_colors[k] == color) {
                return static_cast<std::int16_t>(-10 - k);
            }
        }
        return -9;
    }
    if (name == "mark-reset") {
        return -3;
    }
    if (name == "press") {
        return -4;
    }
    if (name == "player") {
        return -5;
    }
    if (name == "chip") {
        return -6;
    }
    if (name == "speaker-a") {
        return -7;
    }
    if (name == "speaker-b") {
        return -8;
    }
    if (name.rfind("glyph:", 0) == 0 || name.rfind("ctl:", 0) == 0) {
        return static_cast<std::int16_t>(std::atoi(name.c_str() + name.find(':') + 1));
    }
    return 0;
}

int field(const gsexp::FormView& form, const char* head, int n = 0) {
    const gsexp::Node node = form.find(head);
    return node.valid() ? atom_int(node.child_at(static_cast<std::size_t>(n) + 1)) : 0;
}

// the text group a script's lines are in: talk_5b -> talk, bark_2c4b60 -> bark
std::string group_of(const std::string& name) {
    const std::size_t underscore = name.find('_');
    return underscore == std::string::npos ? name : name.substr(0, underscore);
}

// every (text "id" "...") of a group file, by id
bool load_lines(std::map<std::string, std::string>& lines, const std::string& path,
                std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(path, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (form.is_list() && form.head().is_atom("text") && form.child_count() >= 3) {
            lines[std::string(form.child_at(1).text())] = std::string(form.child_at(2).text());
        }
    }
    return true;
}

// one command form of a script, appended; false for forms that aren't commands
void read_command(TalkScript& script, const TalkFont& font,
                  const std::map<std::string, std::string>& lines, gsexp::Node node) {
    const gsexp::FormView form(node);
    TalkCommand command;
    const auto at = [&] {
        command.x = field(form, "at", 0);
        command.y = field(form, "at", 1);
    };
    if (form.head().is_atom("text")) {
        command.type = op_text;
        const auto found = lines.find(std::string(node.child_at(1).text()));
        command.text = static_cast<int>(script.lines.size());
        script.lines.push_back(
            glyphs_from_text(font, found != lines.end() ? found->second : std::string()));
        at();
        command.speed = field(form, "speed");
        command.flags = atom_hex(form.find("flags").child_at(1));
        command.color = atom_hex(form.find("color").child_at(1));
        command.texture = field(form, "texture");
    } else if (form.head().is_atom("wait")) {
        command.type = op_wait;
        command.frames = atom_int(node.child_at(1));
    } else if (form.head().is_atom("delay")) {
        command.type = op_delay;
        command.frames = atom_int(node.child_at(1));
    } else if (form.head().is_atom("sprite")) {
        command.type = op_sprite;
        command.sprite = atom_int(node.child_at(1));
        at();
        command.order = field(form, "order");
        command.color = atom_hex(form.find("color").child_at(1));
        command.flags = atom_hex(form.find("flags").child_at(1));
    } else if (form.head().is_atom("portrait")) {
        command.type = op_portrait;
        command.actor = portrait_actor_index(std::string(node.child_at(1).text()));
        command.expression = atom_int(node.child_at(2));
        at();
        command.order = field(form, "order");
        command.color = atom_hex(form.find("color").child_at(1));
        command.flags =
            (form.find("fade-in").valid() ? 1u : 0u) | (form.find("fade-out").valid() ? 2u : 0u) |
            (form.find("still").valid() ? 4u : 0u) | atom_hex(form.find("flags").child_at(1));
    } else if (form.head().is_atom("page-wait")) {
        command.type = op_page_wait;
    } else if (form.head().is_atom("end")) {
        command.type = op_end;
    } else if (form.head().is_atom("op")) {
        // types no retail script has (0, 6..9, 11, 12): the countdowns' load is arg 0
        command.type = static_cast<std::uint8_t>(atom_int(node.child_at(1)));
        command.frames = atom_int(node.child_at(2));
    } else {
        return; // label, header fields
    }
    script.commands.push_back(command);
}

} // namespace

bool load_talk_font(TalkFont& font, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/text/font.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node node = parsed.root(i);
        const gsexp::FormView form(node);
        if (form.head().is_atom("atlas")) {
            font.sheet_width = field(form, "size", 0);
            font.sheet_height = field(form, "size", 1);
            font.cell = form.get_int("cell").value_or(18);
            font.per_row = form.get_int("per-row").value_or(28);
            continue;
        }
        if (!form.head().is_atom("glyph")) {
            continue;
        }
        const int glyph = atom_int(node.child_at(1));
        if (glyph < 0) {
            continue;
        }
        if (static_cast<std::size_t>(glyph) >= font.left.size()) {
            font.left.resize(static_cast<std::size_t>(glyph) + 1, 0);
            font.advance.resize(static_cast<std::size_t>(glyph) + 1, 18);
        }
        font.left[static_cast<std::size_t>(glyph)] = static_cast<std::uint8_t>(field(form, "left"));
        font.advance[static_cast<std::size_t>(glyph)] =
            static_cast<std::uint8_t>(field(form, "advance"));
        const std::string character(node.child_at(2).text());
        if (!character.empty()) {
            std::size_t at = 0;
            font.glyph_of.emplace(next_code_point(character, at), glyph);
        }
    }
    if (font.left.empty()) {
        error = "no glyphs in " + data_dir + "/text/font.sexp";
        return false;
    }
    return true;
}

GlyphText glyphs_from_text(const TalkFont& font, const std::string& text) {
    GlyphText out;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '\n') {
            out.push_back(-2);
            ++at;
            continue;
        }
        if (text[at] == '{') {
            const std::size_t close = text.find('}', at);
            if (close != std::string::npos) {
                out.push_back(markup_token(text.substr(at + 1, close - at - 1)));
                at = close + 1;
                continue;
            }
        }
        const std::uint32_t code = next_code_point(text, at);
        const auto found = font.glyph_of.find(code);
        // a character the font lacks draws glyph 0, the full-width space (sjis_to_glyphs)
        out.push_back(static_cast<std::int16_t>(found != font.glyph_of.end() ? found->second : 0));
    }
    out.push_back(-1);
    return out;
}

int portrait_actor_index(const std::string& stem) {
    for (int i = 0; i < 28; ++i) {
        if (stem == character_stems[i]) {
            return i;
        }
    }
    return std::atoi(stem.c_str()); // the scripts family writes an unnamed actor as its number
}

bool load_talk_script(TalkScript& script, const TalkFont& font, const std::string& data_dir,
                      const std::string& name_in, std::string& error) {
    // a state number names its talk
    std::string name = name_in;
    if (name.rfind("0x", 0) == 0) {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, "talk_%02lx", std::strtol(name.c_str(), nullptr, 16));
        name = buffer;
    }
    const std::string group = group_of(name);
    const std::string file = data_dir + "/scripts/" + (group == "talk" ? name : group) + ".sexp";

    std::map<std::string, std::string> lines;
    gsexp::ParseResult parsed;
    if (!load_lines(lines, data_dir + "/text/" + group + ".sexp", error) ||
        !parse_sexp_file(file, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node node = parsed.root(i);
        if (!node.is_list() || !node.head().is_atom("script") || node.child_at(1).text() != name) {
            continue;
        }
        script = TalkScript{};
        script.name = name;
        const gsexp::Node state = gsexp::FormView(node).find("state");
        script.state = state.valid() ? atom_int(state.child_at(1)) : -1;
        for (gsexp::Node child : node.children()) {
            if (child.is_list()) {
                read_command(script, font, lines, child);
            }
        }
        if (script.commands.empty() || script.commands.back().type != op_end) {
            script.commands.push_back(TalkCommand{});
        }
        return true;
    }
    error = "no script " + name + " in " + file;
    return false;
}
