#include "scripts.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "event_scripts.hpp"
#include "extract.hpp"
#include "glyphs.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

// a loose texture's file stem without _tex: yuki, club_wired... (portraits, talk backgrounds)
std::string asset_stem(const Disc& disc, int id) {
    if (id < 0 || id >= static_cast<int>(disc.assets.size())) {
        return sx_int(id);
    }
    std::string stem = disc.assets[id].path;
    const std::size_t slash = stem.find_last_of('\\');
    stem = slash == std::string::npos ? stem : stem.substr(slash + 1);
    const std::size_t dot = stem.rfind('.');
    stem = dot == std::string::npos ? stem : stem.substr(0, dot);
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_tex") == 0) {
        stem.resize(stem.size() - 4);
    }
    // talk_clubwired -> clubwired, mht37_pepper -> pepper, sht02_nin -> nin
    const std::size_t underscore = stem.find('_');
    if (underscore != std::string::npos &&
        (stem.compare(0, 5, "talk_") == 0 || stem.compare(0, 3, "mht") == 0 ||
         stem.compare(0, 3, "sht") == 0)) {
        stem = stem.substr(underscore + 1);
    }
    return stem.empty() ? sx_int(id) : stem;
}

struct Names {
    std::vector<std::string> actors;      // by portrait actor
    std::vector<std::string> backgrounds; // by talk background
};

std::string actor_name(const Names& names, int actor) {
    return actor >= 0 && actor < static_cast<int>(names.actors.size()) ? names.actors[actor]
                                                                       : sx_int(actor);
}

// the trigger rows that play a conversation (event_policy_table 0x1D0E00, EventTrigger in the
// decomp's include/event.h), and its policy from the first of them (event_policy_row_of_state)
std::string trigger_forms(const Xbe& xbe, const Names& names, int state) {
    std::string out;
    bool policy_done = false;
    for (int row = 0; row < xbe_at::event_trigger_count; ++row) {
        std::uint8_t r[0x24];
        xbe_read(xbe, xbe_at::event_policy_table + row * 0x24, r, sizeof r);
        std::int32_t words[3];
        std::memcpy(words, r, sizeof words);
        if (words[0] != state) {
            continue;
        }
        const auto signed_at = [&](int at) {
            return static_cast<int>(static_cast<std::int8_t>(r[at]));
        };
        out += "  (trigger " + sx_int(row) + " (from " + sx_hex(words[1]) + ") (to " +
               sx_hex(words[2]) + ")";
        if (r[0x14] == 0) {
            out += " (help-only)";
        }
        if (signed_at(0x0C) != 0 || signed_at(0x0D) != 0) {
            out += signed_at(0x0C) == signed_at(0x0D) ? " (needs " + sx_int(signed_at(0x0C)) + ")"
                                                      : " (needs-range " + sx_int(signed_at(0x0C)) +
                                                            " " + sx_int(signed_at(0x0D)) + ")";
        }
        out += signed_at(0x0E) == -1  ? " (repeatable)"
               : signed_at(0x0E) == 1 ? " (once)"
                                      : " (repeat " + sx_int(signed_at(0x0E)) + ")";
        // conditions: absent means any (resolver, docs/port/progression.md "Finding a row")
        if (r[0x0F] < 75) {
            out += " (predicate " + sx_int(r[0x0F]) + ")";
        }
        if (signed_at(0x10) >= 0 && signed_at(0x10) < 56) {
            out += " (tournament " + sx_int(r[0x10]) + ")";
        }
        if (signed_at(0x11) >= 0 && signed_at(0x11) < 4) {
            out += " (area " + sx_int(r[0x11]) + ")";
        }
        if (signed_at(0x12) >= 0 && signed_at(0x12) < 28) {
            out += " (rule " + sx_int(r[0x12]) + ")";
        }
        if (signed_at(0x13) >= 0 && signed_at(0x13) < 2) {
            out += " (night " + sx_int(r[0x13]) + ")";
        }
        out += ")\n";
        if (policy_done) {
            continue;
        }
        policy_done = true;
        const int background = signed_at(0x16);
        out += "  (policy (background " +
               (background >= 0 && background < static_cast<int>(names.backgrounds.size())
                    ? names.backgrounds[background]
                    : std::string("none")) +
               ") (cast";
        for (int i = 0; i < 6 && signed_at(0x17 + i) >= 0; ++i) {
            out += " " + actor_name(names, signed_at(0x17 + i));
        }
        std::int16_t music = 0;
        float volume = 0;
        std::memcpy(&music, r + 0x1E, 2);
        std::memcpy(&volume, r + 0x20, 4);
        out += ") (music " +
               (music == -1    ? std::string("keep")
                : music == 202 ? std::string("shuffled")
                               : sx_int(music)) +
               ") (volume " + sx_float(volume) + "))\n";
    }
    return out;
}

std::string text_comment(const Xbe& xbe, const GlyphTable& glyphs, const EventRecord& record) {
    const std::string text = narrow_for_reading(
        tokens_to_utf8(glyphs, read_tokens(xbe, static_cast<std::uint32_t>(record.arg[0]))));
    return " ; " + text;
}

// one command per line, text pairs merged into the line's command (event_cmd_text)
std::string command_forms(const Xbe& xbe, const GlyphTable& glyphs, const Names& names,
                          const EventScript& script, int& opcodes) {
    std::string out;
    int undrawn = 0;
    std::map<int, EventLine> line_at; // by the pair's first record
    std::map<int, bool> covered;
    for (const EventLine& line : event_lines(script, undrawn)) {
        line_at[line.furigana >= 0 ? line.furigana : line.command] = line;
        covered[line.command] = true;
        covered[line.furigana] = true;
    }
    int page = 0;
    out += "  (label page-0)\n";
    for (int i = 0; i < static_cast<int>(script.commands.size()); ++i) {
        const EventRecord& c = script.commands[i];
        const std::int32_t* a = c.arg;
        ++opcodes;
        switch (c.type) {
        case 1: {
            const auto found = line_at.find(i);
            if (found == line_at.end()) {
                if (!covered.count(i)) { // an odd text slot with no pair: never drawn
                    out += "  (text-skipped " + sx_string(event_text_id(script, i)) + ")\n";
                } else {
                    --opcodes; // the pair's second record, merged above
                }
                break;
            }
            const EventLine& line = found->second;
            const EventRecord& m = script.commands[line.command];
            out += "  (text " + sx_string(event_text_id(script, line.command)) + " (at " +
                   sx_int(m.arg[3]) + " " + sx_int(m.arg[4]) + ") (speed " + sx_int(m.arg[1]) + ")";
            if (m.arg[2] != 0) {
                out += " (flags " + sx_hex(static_cast<std::uint32_t>(m.arg[2])) + ")";
            }
            out += " (color " + sx_hex(static_cast<std::uint32_t>(m.arg[5])) + ")";
            if (m.arg[6] != 18 || m.arg[7] != 18) {
                out += " (furigana-size " + sx_int(m.arg[6]) + " " + sx_int(m.arg[7]) + ")";
            }
            if (m.arg[8] != 0) {
                out += " (texture " + sx_int(m.arg[8]) + ")";
            }
            if (line.furigana >= 0) {
                const std::string furigana = tokens_to_utf8(
                    glyphs, read_tokens(xbe, static_cast<std::uint32_t>(
                                                 script.commands[line.furigana].arg[0])));
                if (furigana != "\xE3\x80\x80") {
                    out += " (furigana " + sx_string(event_text_id(script, line.furigana)) + ")";
                }
            }
            out += ")" + text_comment(xbe, glyphs, m) + "\n";
            if (line.furigana >= 0) {
                ++i; // the line's record
            }
            break;
        }
        case 2:
            out += "  (wait " + sx_int(a[2]) + ")\n  (label page-" + sx_int(++page) + ")\n";
            break;
        case 3:
            out += "  (sprite " + sx_int(a[0]) + " (at " + sx_int(a[1]) + " " + sx_int(a[2]) +
                   ") (order " + sx_int(a[3]) + ") (color " +
                   sx_hex(static_cast<std::uint32_t>(a[4])) + ")";
            out += a[5] ? " (flags " + sx_hex(static_cast<std::uint32_t>(a[5])) + "))\n" : ")\n";
            break;
        case 4:
            out += "  (portrait " + actor_name(names, a[0]) + " " + sx_int(a[1]) + " (at " +
                   sx_int(a[2]) + " " + sx_int(a[3]) + ") (order " + sx_int(a[4]) + ") (color " +
                   sx_hex(static_cast<std::uint32_t>(a[5])) + ")";
            out += a[6] & 1 ? " (fade-in)" : "";
            out += a[6] & 2 ? " (fade-out)" : "";
            out += a[6] & 4 ? " (still)" : "";
            out += a[6] & ~7 ? " (flags " + sx_hex(static_cast<std::uint32_t>(a[6])) + ")" : "";
            out += ")\n";
            break;
        case 5:
            out += "  (delay " + sx_int(a[0]) + ")\n";
            break;
        case 10:
            out += "  (page-wait)\n";
            break;
        case 13:
            out += "  (end)";
            break;
        default: // 0 reset, 6..9 empty, 11 and 12 countdowns: no script has them
            out += "  (op " + sx_int(c.type);
            for (int k = 0; k < 12; ++k) {
                out += " " + sx_int(a[k]);
            }
            out += ")\n";
            break;
        }
    }
    return out;
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    return static_cast<bool>(out);
}

} // namespace

bool extract_scripts(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const std::string dir = options.out_dir + "/scripts";
    std::error_code made;
    std::filesystem::create_directories(dir, made);

    GlyphTable glyphs;
    std::vector<EventScript> scripts;
    std::string error;
    if (!load_glyph_table(disc.xbe, glyphs, error) ||
        !find_event_scripts(disc.xbe, scripts, error)) {
        std::fprintf(stderr, "scripts: %s\n", error.c_str());
        return false;
    }
    Names names;
    // two actors share a stem twice (nin, mask): the second gets a 2, as in progression.md
    for (int i = 0; i < xbe_at::portrait_actor_count; ++i) {
        std::string name = asset_stem(disc, xbe_i32(disc.xbe, xbe_at::portrait_actors + i * 0x28));
        for (const std::string& taken : names.actors) {
            name += taken == name ? "2" : "";
        }
        names.actors.push_back(name);
    }
    for (int i = 0; i < xbe_at::talk_background_count; ++i) {
        names.backgrounds.push_back(
            asset_stem(disc, xbe_i32(disc.xbe, xbe_at::talk_backgrounds + i * 8)));
    }

    // talks one file each, the .data groups one file a group
    std::map<std::string, std::string> files;
    std::map<std::string, int> counts;
    int opcodes = 0;
    for (const EventScript& script : scripts) {
        const std::string file = script.group == "talk" ? script.name : script.group;
        std::string& out = files[file];
        out += "\n(script " + script.name + " (address " + sx_hex(script.va) + ") (commands " +
               sx_int(static_cast<long long>(script.commands.size())) + ")";
        out += script.state >= 0 ? " (state " + sx_hex(script.state) + ")\n" : "\n";
        if (script.state >= 0) {
            out += trigger_forms(disc.xbe, names, script.state);
        }
        if (!script.used_by.empty()) {
            out += "  (used-by";
            for (const std::string& form : script.used_by) {
                out += " " + form;
            }
            out += ")\n";
        }
        out += command_forms(disc.xbe, glyphs, names, script, opcodes) + ")\n";
        ++counts[file];
    }
    for (const auto& [file, body] : files) {
        const std::string head =
            ";; Event scripts (docs/text-and-scripts.md): commands in order, text pairs merged, a\n"
            ";; label wherever the interpreter's walk restarts (after each wait). Text ids are in\n"
            ";; data/text/" +
            (file.rfind("talk_", 0) == 0 ? std::string("talk") : file) +
            ".sexp; the comment is that text for reading.\n";
        if (!write_file(dir + "/" + file + ".sexp", head + body)) {
            std::fprintf(stderr, "scripts: can't write %s/%s.sexp\n", dir.c_str(), file.c_str());
            return false;
        }
        manifest.entries.push_back("(scripts (file \"scripts/" + file + ".sexp\") (scripts " +
                                   sx_int(counts[file]) + "))");
    }
    std::printf("scripts: %zu scripts in %zu files, %d commands (text pairs counted once)\n",
                scripts.size(), files.size(), opcodes);
    return true;
}
