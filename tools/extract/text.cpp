#include "text.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "event_scripts.hpp"
#include "extract.hpp"
#include "glyphs.hpp"
#include "png.hpp"
#include "sexp_out.hpp"
#include "textures.hpp"
#include "ui_strings.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
    return static_cast<bool>(out);
}

// the atlas (asset 1684, P8 512x2048) to font.png and every glyph to font.sexp
bool write_font(const Disc& disc, const GlyphTable& glyphs, const std::string& dir,
                Manifest& manifest) {
    const std::vector<std::uint8_t> bytes = read_asset(disc, xbe_at::font_atlas_asset);
    std::string error;
    const std::vector<DecodedTexture> atlas =
        bytes.empty() ? std::vector<DecodedTexture>{} : decode_texture_set(bytes, error);
    if (atlas.empty()) {
        std::fprintf(stderr, "text: the font atlas (asset %d): %s\n", xbe_at::font_atlas_asset,
                     bytes.empty() ? "unreadable" : error.c_str());
        return false;
    }
    const DecodedTexture& sheet = atlas[0];
    if (!write_png(dir + "/font.png", sheet.width, sheet.height, sheet.rgba)) {
        std::fprintf(stderr, "text: can't write %s/font.png\n", dir.c_str());
        return false;
    }

    std::string out =
        ";; The event and UI font (docs/text-and-scripts.md). Glyph g sits in cell\n"
        ";; ((g % 28) * 18, (g / 28) * 18) of font.png (wide_glyph_rect 0xBE9AC). sjis is its pair "
        "in\n"
        ";; font_sjis_order 0x27DE38, which turns table text into glyphs; the character comes "
        "from\n"
        ";; font_unicode_order 0x27F030 for glyphs below " +
        std::to_string(glyphs.unicode_count) +
        " and from CP932 for the rest (marked cp932).\n"
        ";; left and advance are glyph_widths 0x1C3FB0 (glyphs below 0x184; the rest are 0 and "
        "18):\n"
        ";; wide_text_draw starts the glyph left texels into its cell and moves on by advance.\n";
    out += "(atlas (file \"font.png\") (size " + sx_int(sheet.width) + " " + sx_int(sheet.height) +
           ") (cell " + sx_int(xbe_at::font_cell) + ") (per-row " +
           sx_int(xbe_at::font_cells_per_row) + ") (asset " + sx_int(xbe_at::font_atlas_asset) +
           "))\n";
    for (std::size_t g = 0; g < glyphs.glyphs.size(); ++g) {
        const Glyph& glyph = glyphs.glyphs[g];
        const int x = static_cast<int>(g % xbe_at::font_cells_per_row) * xbe_at::font_cell;
        const int y = static_cast<int>(g / xbe_at::font_cells_per_row) * xbe_at::font_cell;
        out += "(glyph " + sx_int(static_cast<long long>(g)) + " " + sx_string(glyph.utf8) +
               " (sjis " + sx_hex(glyph.sjis) + ") (cell " + sx_int(x) + " " + sx_int(y) +
               ") (left " + sx_int(glyph.left) + ") (advance " + sx_int(glyph.advance) + ")";
        out += glyph.from_cp932 ? " (cp932))" : ")";
        for (const int odd : glyphs.disagreeing) {
            if (odd == static_cast<int>(g)) {
                const std::string decoded = cp932_pair_to_utf8(glyph.sjis);
                out += " ; CP932 reads this pair as " +
                       (decoded.empty() ? std::string("nothing (unassigned)") : decoded);
            }
        }
        out += "\n";
    }
    if (!write_file(dir + "/font.sexp", out)) {
        return false;
    }
    manifest.entries.push_back("(font (asset " + sx_int(xbe_at::font_atlas_asset) + ") (source " +
                               sx_string(asset_source(disc, xbe_at::font_atlas_asset)) +
                               ") (file \"text/font.png\") (map \"text/font.sexp\") (glyphs " +
                               sx_int(static_cast<long long>(glyphs.glyphs.size())) +
                               ") (xbe font_sjis_order " + sx_hex(xbe_at::font_sjis_order) +
                               ") (xbe font_unicode_order " + sx_hex(xbe_at::font_unicode_order) +
                               ") (xbe glyph_widths " + sx_hex(xbe_at::glyph_widths) + "))");
    return true;
}

// one file per script group: every line of every script, by script and command index
bool write_event_text(const Xbe& xbe, const GlyphTable& glyphs,
                      const std::vector<EventScript>& scripts, const std::string& dir,
                      Manifest& manifest, int& line_count) {
    std::map<std::string, std::string> files;
    std::map<std::string, int> counts;
    for (const EventScript& script : scripts) {
        std::string& out = files[script.group];
        out += "\n;; " + script.name + " at " + sx_hex(script.va) + "\n";
        int undrawn = 0;
        for (const EventLine& line : event_lines(script, undrawn)) {
            const auto text_of = [&](int command) {
                return tokens_to_utf8(
                    glyphs,
                    read_tokens(xbe, static_cast<std::uint32_t>(script.commands[command].arg[0])));
            };
            if (line.furigana >= 0) {
                const std::string furigana = text_of(line.furigana);
                if (furigana != "\xE3\x80\x80") { // a single blank in every US script
                    out += "(text " + sx_string(event_text_id(script, line.furigana)) + " " +
                           sx_string(furigana) + " (furigana))\n";
                }
            }
            out += "(text " + sx_string(event_text_id(script, line.command)) + " " +
                   sx_string(text_of(line.command)) + ")\n";
            ++counts[script.group];
            ++line_count;
        }
    }
    for (const auto& [group, body] : files) {
        const std::string name = group + ".sexp";
        const std::string head =
            ";; " + group +
            " lines: (text \"<script>.<command>\" \"UTF-8\"), the command being "
            "the record that draws it\n;; (docs/text-and-scripts.md)\n";
        if (!write_file(dir + "/" + name, head + body)) {
            return false;
        }
        manifest.entries.push_back("(text-lines (group " + group + ") (file \"text/" + name +
                                   "\") (lines " + sx_int(counts[group]) + "))");
    }
    return true;
}

// the table strings to tables.sexp, the loose ones to strings.sexp
bool write_ui_strings(const std::vector<UiString>& strings, const std::string& dir,
                      Manifest& manifest, int& table_count, int& loose_count) {
    std::string tables =
        ";; Strings of the game's tables: (string \"<table>.<row>.<field>\" \"UTF-8\")\n"
        ";; (docs/text-and-scripts.md). (missing n): pairs the game draws as blanks.\n";
    std::string loose = ";; Every other displayable Shift-JIS string in .rdata and .data, by "
                        "address: str.19b41c is\n"
                        ";; the decomp's D_0019B41C. used-by lists the pointers to it ((data VA) a "
                        "word, (code VA)\n"
                        ";; an instruction's operand); none means nothing points at it.\n";
    for (const UiString& string : strings) {
        std::string form = "(string " + sx_string(string.id) + " " + sx_string(string.text);
        if (string.missing) {
            form += " (missing " + sx_int(string.missing) + ")";
        }
        if (string.table.empty()) {
            form += " (used-by";
            for (std::size_t i = 0; i < string.used_by.size() && i < 8; ++i) {
                form += " " + string.used_by[i];
            }
            if (string.used_by.size() > 8) {
                form += " (and " + sx_int(static_cast<long long>(string.used_by.size() - 8)) + ")";
            }
            form += string.used_by.empty() ? " none)" : ")";
            loose += form + ")\n";
            ++loose_count;
        } else {
            tables += form + ")\n";
            ++table_count;
        }
    }
    if (!write_file(dir + "/tables.sexp", tables) || !write_file(dir + "/strings.sexp", loose)) {
        return false;
    }
    manifest.entries.push_back("(text-strings (file \"text/tables.sexp\") (strings " +
                               sx_int(table_count) + "))");
    manifest.entries.push_back("(text-strings (file \"text/strings.sexp\") (strings " +
                               sx_int(loose_count) + "))");
    return true;
}

} // namespace

bool extract_text(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const std::string dir = options.out_dir + "/text";
    std::error_code made;
    std::filesystem::create_directories(dir, made);

    GlyphTable glyphs;
    std::string error;
    if (!load_glyph_table(disc.xbe, glyphs, error)) {
        std::fprintf(stderr, "text: %s\n", error.c_str());
        return false;
    }
    if (!write_font(disc, glyphs, dir, manifest)) {
        return false;
    }

    std::vector<EventScript> scripts;
    if (!find_event_scripts(disc.xbe, scripts, error)) {
        std::fprintf(stderr, "text: %s\n", error.c_str());
        return false;
    }
    int lines = 0;
    if (!write_event_text(disc.xbe, glyphs, scripts, dir, manifest, lines)) {
        std::fprintf(stderr, "text: can't write the event text\n");
        return false;
    }

    std::vector<UiString> strings;
    if (!collect_ui_strings(disc.xbe, glyphs, strings, error)) {
        std::fprintf(stderr, "text: %s\n", error.c_str());
        return false;
    }
    int table_count = 0, loose_count = 0;
    if (!write_ui_strings(strings, dir, manifest, table_count, loose_count)) {
        std::fprintf(stderr, "text: can't write the strings\n");
        return false;
    }
    std::printf("text: %zu glyphs (%d named by font_unicode_order, %zu disagree with CP932), "
                "%zu scripts, %d lines, %d table strings, %d loose strings\n",
                glyphs.glyphs.size(), glyphs.unicode_count, glyphs.disagreeing.size(),
                scripts.size(), lines, table_count, loose_count);
    return true;
}
