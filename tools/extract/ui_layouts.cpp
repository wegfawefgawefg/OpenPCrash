// The original's UI tables to data/ui/layouts/<screen>.sexp. Which tables, where, in which record
// layout and for which screen comes from ui_tables.inc (tools/ui_tables.py, from the decomp); the
// values are read here from the XBE.
//
// Field kinds in a layout: i int, u unsigned, x a word shown in hex (colours, flags), f float,
// h/H short/unsigned short, b/B char/unsigned char, r a render registry sprite id (R as a short),
// p a pointer
// (shown as the symbol it points at), s a pointer to Shift-JIS text, c inline text.
#include "disc.hpp"
#include "manifest.hpp"
#include "sexp_out.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

struct UiLayoutDef {
    const char* name;
    int size;
    const char* fields;
};

struct UiTableDef {
    const char* screen; // the screens whose code reads it, most readers first: the first owns it
    const char* name;
    std::uint32_t va;
    const char* layout;
    int count;
    const char* dims; // "5 22" for a 2D array, else empty
    const char* file;
    const char* screen_by;
    const char* readers;
    const char* decomp; // the decomp's D_ name when the table has a real one, else empty
};

struct UiSymbolDef {
    std::uint32_t va;
    const char* name;
};

struct UiSpriteUseDef {
    int sprite;
    const char* screen;
    const char* function;
};

#include "ui_tables.inc"

struct Field {
    std::string name;
    char kind = 'i';
    int offset = 0;
    int count = 1;
};

struct Layout {
    std::string name;
    int size = 0;
    std::vector<Field> fields;
};

// "sprite:r@0*1 x:f@4*1 ..." into fields
Layout parse_layout(const UiLayoutDef& def) {
    Layout layout;
    layout.name = def.name;
    layout.size = def.size;
    std::stringstream stream(def.fields);
    std::string spec;
    while (stream >> spec) {
        Field field;
        const std::size_t colon = spec.find(':');
        const std::size_t at = spec.find('@');
        const std::size_t star = spec.find('*');
        field.name = spec.substr(0, colon);
        field.kind = spec[colon + 1];
        field.offset = std::stoi(spec.substr(at + 1, star - at - 1));
        field.count = std::stoi(spec.substr(star + 1));
        layout.fields.push_back(field);
    }
    return layout;
}

int kind_size(char kind) {
    switch (kind) {
    case 'h':
    case 'H':
    case 'R':
        return 2;
    case 'b':
    case 'B':
    case 'c':
        return 1;
    default:
        return 4;
    }
}

// what a pointer word names: a symbol, or a table and an offset into it
struct Symbols {
    std::vector<UiSymbolDef> exact;
    struct Span {
        std::uint32_t va, end;
        const char* name;
    };
    std::vector<Span> tables;

    std::string name_of(std::uint32_t va) const {
        const auto hit = std::lower_bound(
            exact.begin(), exact.end(), va,
            [](const UiSymbolDef& symbol, std::uint32_t at) { return symbol.va < at; });
        if (hit != exact.end() && hit->va == va) {
            return hit->name;
        }
        for (const Span& span : tables) {
            if (va > span.va && va < span.end) {
                return std::string(span.name) + "+" + sx_hex(va - span.va);
            }
        }
        return {};
    }
};

// a zero-ended string at va as a quoted UTF-8 .sexp string
std::string text_at(const Xbe& xbe, std::uint32_t va, std::size_t limit) {
    std::string raw;
    for (std::size_t i = 0; i < limit; ++i) {
        const std::uint8_t c = xbe_u8(xbe, va + static_cast<std::uint32_t>(i));
        if (!c) {
            break;
        }
        raw += static_cast<char>(c);
    }
    return sx_string(sjis_to_utf8(xbe, raw));
}

bool in_image(const Xbe& xbe, std::uint32_t va) {
    std::uint8_t probe = 0;
    return va >= xbe.base && xbe_read(xbe, va, &probe, 1);
}

// one value of a field
std::string value_text(const Xbe& xbe, const Symbols& symbols, char kind, std::uint32_t va) {
    switch (kind) {
    case 'f':
        return sx_float(xbe_f32(xbe, va));
    case 'x':
        return sx_hex(xbe_u32(xbe, va));
    case 'u':
        return sx_int(xbe_u32(xbe, va));
    case 'h':
    case 'R':
        return sx_int(xbe_i16(xbe, va));
    case 'H':
        return sx_int(static_cast<std::uint16_t>(xbe_i16(xbe, va)));
    case 'b':
        return sx_int(static_cast<std::int8_t>(xbe_u8(xbe, va)));
    case 'B':
        return sx_int(xbe_u8(xbe, va));
    case 'p':
    case 's': {
        const std::uint32_t target = xbe_u32(xbe, va);
        if (target < xbe.base) {
            return sx_int(xbe_i32(xbe, va)); // a small number, not an address
        }
        if (kind == 's' && in_image(xbe, target)) {
            return text_at(xbe, target, 4096);
        }
        const std::string name = symbols.name_of(target);
        if (!name.empty()) {
            return name;
        }
        // a plain number in a column that also holds pointers
        return in_image(xbe, target) ? sx_hex(target) : sx_int(xbe_i32(xbe, va));
    }
    default:
        return sx_int(xbe_i32(xbe, va));
    }
}

// one table: its header, then a (values ...) list for a plain array or a (row i ...) per record
std::string table_text(const Xbe& xbe, const Symbols& symbols, const UiTableDef& def,
                       const Layout& layout, UiSpriteUses& uses) {
    std::string text =
        "(table " + std::string(def.name) +
        (def.decomp[0] ? " (decomp " + std::string(def.decomp) + ")" : std::string()) + " (va " +
        sx_hex(def.va) + ") (layout " + layout.name + ") (count " + sx_int(def.count) + ")";
    text += " (decomp-file " + std::string(def.file) + ") (screen-by " + def.screen_by + ")";
    // a 2D array's rows are keyed by both indices
    int inner = 0;
    if (def.dims[0]) {
        text += " (dims " + std::string(def.dims) + ")";
        inner = std::stoi(std::string(def.dims).substr(std::string(def.dims).find(' ') + 1));
    }
    const std::string screens = def.screen;
    const std::size_t space = screens.find(' ');
    if (space != std::string::npos) {
        text += " (also-screens " + screens.substr(space + 1) + ")";
    }
    if (def.readers[0]) {
        text += "\n  (readers " + std::string(def.readers) + ")";
    }
    const auto note_sprite = [&](std::uint32_t va, bool half) {
        const int sprite = half ? xbe_i16(xbe, va) : xbe_i32(xbe, va);
        if (sprite < 0) {
            return;
        }
        std::stringstream each(screens);
        std::string screen;
        while (each >> screen) {
            uses.sprites[sprite].push_back({screen, def.name});
        }
    };

    // a single inline string
    if (layout.fields.size() == 1 && layout.fields[0].kind == 'c') {
        return text + "\n  (text " + text_at(xbe, def.va, layout.fields[0].count) + "))\n";
    }
    // a plain array
    if (layout.fields.size() == 1 && layout.fields[0].name == "value") {
        const char kind = layout.fields[0].kind;
        text += "\n  (values";
        for (int i = 0; i < def.count; ++i) {
            const std::uint32_t va = def.va + static_cast<std::uint32_t>(i * layout.size);
            text += (i % 16 == 0 && def.count > 16) ? "\n   " : " ";
            text += value_text(xbe, symbols, kind, va);
        }
        return text + "))\n";
    }
    for (int row = 0; row < def.count; ++row) {
        const std::uint32_t base = def.va + static_cast<std::uint32_t>(row * layout.size);
        text +=
            "\n  (row " + (inner ? sx_int(row / inner) + " " + sx_int(row % inner) : sx_int(row));
        for (const Field& field : layout.fields) {
            const std::uint32_t at = base + static_cast<std::uint32_t>(field.offset);
            text += " (" + field.name;
            if (field.kind == 'c') {
                text += " " + text_at(xbe, at, field.count) + ")";
                continue;
            }
            for (int k = 0; k < field.count; ++k) {
                const std::uint32_t va = at + static_cast<std::uint32_t>(k * kind_size(field.kind));
                if (field.kind == 'r' || field.kind == 'R') {
                    note_sprite(va, field.kind == 'R');
                }
                text += " " + value_text(xbe, symbols, field.kind, va);
            }
            text += ")";
        }
        text += ")";
    }
    return text + ")\n";
}

} // namespace

bool write_ui_layouts(const Disc& disc, const std::string& out_dir, UiSpriteUses& uses,
                      Manifest& manifest) {
    std::map<std::string, Layout> layouts;
    for (const UiLayoutDef& def : ui_layout_defs) {
        layouts[def.name] = parse_layout(def);
    }
    Symbols symbols;
    symbols.exact.assign(std::begin(ui_symbol_defs), std::end(ui_symbol_defs));
    for (const UiTableDef& def : ui_table_defs) {
        const int bytes = layouts[def.layout].size * def.count;
        symbols.tables.push_back({def.va, def.va + static_cast<std::uint32_t>(bytes), def.name});
    }

    // tables by screen, in address order within each (the .inc is sorted that way)
    std::map<std::string, std::string> files;
    std::map<std::string, int> counts;
    for (const UiTableDef& def : ui_table_defs) {
        std::string owner = def.screen;
        owner = owner.substr(0, owner.find(' '));
        std::string& text = files[owner];
        if (text.empty()) {
            text = ";; " + owner +
                   ": the original's layout tables this screen's code reads, by decomp name "
                   "(pc_extract ui family, docs/ui-assets.md).\n;; Positions are 640x480 "
                   "pixels, colours ARGB, sprite ids are render registry rows "
                   "(data/ui/images.sexp).\n";
        }
        text += table_text(disc.xbe, symbols, def, layouts[def.layout], uses);
        ++counts[owner];
    }
    // literal sprite ids in screen code
    for (const UiSpriteUseDef& def : ui_sprite_use_defs) {
        uses.sprites[def.sprite].push_back({def.screen, std::string("code:") + def.function});
    }
    // texture sets screen code loads (the sprite field holds the asset id)
    for (const UiSpriteUseDef& def : ui_set_load_defs) {
        uses.sets[def.sprite].push_back({def.screen, def.function});
    }

    for (const auto& [screen, text] : files) {
        const std::string path = out_dir + "/ui/layouts/" + screen + ".sexp";
        std::ofstream out(path, std::ios::binary);
        out << text;
        if (!out) {
            std::fprintf(stderr, "ui: can't write %s\n", path.c_str());
            return false;
        }
        manifest.entries.push_back("(ui-layouts (screen " + screen + ") (file " +
                                   sx_string("ui/layouts/" + screen + ".sexp") + ") (tables " +
                                   sx_int(counts[screen]) + "))");
    }
    std::printf("ui: %zu layout tables in %zu screen files\n", std::size(ui_table_defs),
                files.size());
    return true;
}
