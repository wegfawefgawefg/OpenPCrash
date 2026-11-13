#include "tables.hpp"

#include "addresses.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | code >> 6));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | code >> 12));
        out.push_back(static_cast<char>(0x80 | (code >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | code >> 18));
        out.push_back(static_cast<char>(0x80 | (code >> 12 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Shift-JIS lead bytes: 0x81..0x9F and 0xE0..0xFC
bool is_lead(std::uint8_t c) {
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

std::string curve_text(const std::string& base, const std::string& at_mid,
                       const std::string& at_max, int mid, int negative) {
    return "(curve " + base + " " + at_mid + " " + at_max + " " + sx_int(mid) + " " +
           sx_int(negative) + ")";
}

} // namespace

void load_font_order(Tables& tables) {
    // glyph i is the i-th pair; the first 2222 glyphs have a code point in the second table
    for (int glyph = 0; glyph < xbe_at::font_unicode_count; ++glyph) {
        const std::uint32_t at = xbe_at::font_sjis_order + static_cast<std::uint32_t>(glyph) * 2;
        const std::uint16_t pair =
            static_cast<std::uint16_t>(xbe_u8(*tables.xbe, at) << 8 | xbe_u8(*tables.xbe, at + 1));
        const std::uint16_t code = static_cast<std::uint16_t>(xbe_i16(
            *tables.xbe, xbe_at::font_unicode_order + static_cast<std::uint32_t>(glyph) * 2));
        if (code != 0 && tables.unicode.find(pair) == tables.unicode.end()) {
            tables.unicode[pair] = code;
        }
    }
    // the one character table strings use that the font lacks: cp932's IBM extension for the
    // full-width apostrophe (tournament 17 "Sizzlin' Field", 37 "Pager's Friend")
    tables.unicode.emplace(static_cast<std::uint16_t>(0xFA56), 0xFF07);
}

std::string sjis_to_utf8(Tables& tables, const std::string& sjis) {
    std::string out;
    for (std::size_t i = 0; i < sjis.size(); ++i) {
        const std::uint8_t c = static_cast<std::uint8_t>(sjis[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c >= 0xA1 && c <= 0xDF) {
            append_utf8(out, 0xFF61 + (c - 0xA1)); // half-width katakana
        } else if (is_lead(c) && i + 1 < sjis.size()) {
            const std::uint16_t pair =
                static_cast<std::uint16_t>(c << 8 | static_cast<std::uint8_t>(sjis[i + 1]));
            ++i;
            const auto found = tables.unicode.find(pair);
            if (found != tables.unicode.end()) {
                append_utf8(out, found->second);
            } else {
                std::fprintf(stderr, "tables: no Unicode for Shift-JIS %04X\n", pair);
                append_utf8(out, 0xFFFD);
                ++tables.unmapped;
            }
        } else {
            append_utf8(out, 0xFFFD);
            ++tables.unmapped;
        }
    }
    return out;
}

std::string text_at(Tables& tables, std::uint32_t pointer) {
    if (pointer == 0) {
        return "nil";
    }
    return sx_string(utf8_at(tables, pointer));
}

std::string utf8_at(Tables& tables, std::uint32_t pointer) {
    return pointer == 0 ? std::string() : sjis_to_utf8(tables, xbe_cstring(*tables.xbe, pointer));
}

std::string text_in(Tables& tables, std::uint32_t va, int n) {
    std::string raw;
    for (int i = 0; i < n; ++i) {
        const std::uint8_t c = xbe_u8(*tables.xbe, va + static_cast<std::uint32_t>(i));
        if (c == 0) {
            break;
        }
        raw.push_back(static_cast<char>(c));
    }
    return sx_string(sjis_to_utf8(tables, raw));
}

std::string fl(float value) {
    char text[32];
    for (int digits = 6; digits <= 9; ++digits) {
        std::snprintf(text, sizeof text, "%.*g", digits, static_cast<double>(value));
        const float back = std::strtof(text, nullptr);
        if (std::memcmp(&back, &value, sizeof value) == 0) {
            break;
        }
    }
    return text;
}

std::string curve_f(const Xbe& xbe, std::uint32_t va) {
    return curve_text(fl(xbe_f32(xbe, va)), fl(xbe_f32(xbe, va + 4)), fl(xbe_f32(xbe, va + 8)),
                      static_cast<std::int8_t>(xbe_u8(xbe, va + 12)),
                      static_cast<std::int8_t>(xbe_u8(xbe, va + 13)));
}

std::string curve_u(const Xbe& xbe, std::uint32_t va) {
    return curve_text(sx_int(xbe_i32(xbe, va)), sx_int(xbe_i32(xbe, va + 4)),
                      sx_int(xbe_i32(xbe, va + 8)), static_cast<std::int8_t>(xbe_u8(xbe, va + 12)),
                      static_cast<std::int8_t>(xbe_u8(xbe, va + 13)));
}

std::string curve_s(const Xbe& xbe, std::uint32_t va) {
    return curve_text(sx_int(xbe_i16(xbe, va)), sx_int(xbe_i16(xbe, va + 2)),
                      sx_int(xbe_i16(xbe, va + 4)), static_cast<std::int8_t>(xbe_u8(xbe, va + 6)),
                      static_cast<std::int8_t>(xbe_u8(xbe, va + 7)));
}

std::string field(const std::string& name, const std::string& value) {
    return "(" + name + " " + value + ")";
}

std::string field(const std::string& name, long long value) {
    return "(" + name + " " + sx_int(value) + ")";
}

std::string field_enum(const std::string& name, long long value, const char* symbol) {
    return "(" + name + " " + sx_int(value) + " " + symbol + ")";
}

std::string ints_i32(const std::string& name, const Xbe& xbe, std::uint32_t va, int n) {
    std::string out = "(" + name;
    for (int i = 0; i < n; ++i) {
        out += " " + sx_int(xbe_i32(xbe, va + static_cast<std::uint32_t>(i) * 4));
    }
    return out + ")";
}

std::string ints_i16(const std::string& name, const Xbe& xbe, std::uint32_t va, int n) {
    std::string out = "(" + name;
    for (int i = 0; i < n; ++i) {
        out += " " + sx_int(xbe_i16(xbe, va + static_cast<std::uint32_t>(i) * 2));
    }
    return out + ")";
}

std::string ints_u8(const std::string& name, const Xbe& xbe, std::uint32_t va, int n) {
    std::string out = "(" + name;
    for (int i = 0; i < n; ++i) {
        out += " " + sx_int(xbe_u8(xbe, va + static_cast<std::uint32_t>(i)));
    }
    return out + ")";
}

std::string ints_i8(const std::string& name, const Xbe& xbe, std::uint32_t va, int n) {
    std::string out = "(" + name;
    for (int i = 0; i < n; ++i) {
        out +=
            " " + sx_int(static_cast<std::int8_t>(xbe_u8(xbe, va + static_cast<std::uint32_t>(i))));
    }
    return out + ")";
}

std::string floats(const std::string& name, const Xbe& xbe, std::uint32_t va, int n) {
    std::string out = "(" + name;
    for (int i = 0; i < n; ++i) {
        out += " " + fl(xbe_f32(xbe, va + static_cast<std::uint32_t>(i) * 4));
    }
    return out + ")";
}

std::string row_text(const Row& row) {
    // fields follow the head on one line while it stays under 100 columns, then wrap indented
    std::string out = "(" + row.head;
    std::size_t line_start = 0;
    for (const std::string& f : row.fields) {
        if (out.size() - line_start + 1 + f.size() > 100) {
            out += "\n ";
            line_start = out.size() - 1;
        }
        out += " " + f;
    }
    out += ")";
    if (!row.note.empty()) {
        out += " ; " + row.note;
    }
    return out;
}

void add_row(TableFile& file, const Row& row) {
    file.forms.push_back(row_text(row));
}

bool write_table(Tables& tables, const TableFile& file) {
    const std::string path = tables.dir + "/" + file.name + ".sexp";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "tables: can't write %s\n", path.c_str());
        return false;
    }
    out << ";; " << file.name << ": " << file.decomp << " at " << sx_hex(file.va)
        << " in the retail XBE, " << file.rows << " rows. Written by pc_extract (tables);\n"
        << ";; see docs/tables.md.\n";
    for (const std::string& line : file.comments) {
        out << ";; " << line << "\n";
    }
    for (const std::string& form : file.forms) {
        out << form << "\n";
    }
    if (!out) {
        return false;
    }
    tables.manifest->entries.push_back("(table (file " +
                                       sx_string("tables/" + file.name + ".sexp") + ") (decomp " +
                                       sx_string(file.decomp) + ") (xbe " + sx_hex(file.va) +
                                       ") (rows " + sx_int(file.rows) + "))");
    ++tables.files;
    return true;
}

bool extract_tables(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    Tables tables;
    tables.disc = &disc;
    tables.xbe = &disc.xbe;
    tables.dir = options.out_dir + "/tables";
    tables.manifest = &manifest;
    std::error_code made;
    std::filesystem::create_directories(tables.dir, made);
    load_font_order(tables);

    // each group writes its files
    if (!write_part_tables(tables) || !write_roster_tables(tables) ||
        !write_tournament_tables(tables) || !write_music_tables(tables) ||
        !write_flow_tables(tables) || !write_ai_tables(tables)) {
        return false;
    }
    std::printf("tables: %d files, %d characters with no Unicode\n", tables.files, tables.unmapped);
    return true;
}
