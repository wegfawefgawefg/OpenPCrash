#pragma once
// The tables family: the game's static tables in the XBE (parts, Scoobees, opponents, pilots,
// tournaments, shops, music, flow and triggers, AI tuning, impact effects) to data/tables/*.sexp,
// one file a table group, field names from the decomp's include/tables.h. docs/tables.md lists
// every file and the decomp table it came from.
#include "extract.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// what every table writer is handed
struct Tables {
    const Disc* disc = nullptr;
    const Xbe* xbe = nullptr;
    std::string dir; // <out>/tables
    Manifest* manifest = nullptr;
    // Shift-JIS pair (lead << 8 | trail) to its Unicode code point, from the font's two order
    // tables (font_sjis_order 0x27DE38, font_unicode_order 0x27F030)
    std::unordered_map<std::uint16_t, std::uint32_t> unicode;
    int unmapped = 0; // characters no table maps, written as U+FFFD
    int files = 0;
};

// one output file being built: comment lines, then one form per row
struct TableFile {
    std::string name;     // "bodies", written to tables/bodies.sexp
    std::string decomp;   // the decomp's table name(s)
    std::uint32_t va = 0; // where the (first) table sits in the XBE
    int rows = 0;
    std::vector<std::string> comments;
    std::vector<std::string> forms;
};

// a row: "(head (field value) ...)", wrapped at 100 columns
struct Row {
    std::string head;
    std::vector<std::string> fields;
    std::string note = {}; // written as a trailing "; note" comment, for names behind ids
};

// fills tables.unicode from the XBE's font order tables
void load_font_order(Tables& tables);
// a table string: Shift-JIS bytes to UTF-8 through the font order; full-width stays full-width
std::string sjis_to_utf8(Tables& tables, const std::string& sjis);
// a string pointer's text, quoted, or nil for a null pointer
std::string text_at(Tables& tables, std::uint32_t pointer);
// the same unquoted, "" for a null pointer (for notes)
std::string utf8_at(Tables& tables, std::uint32_t pointer);
// a fixed buffer of n bytes holding a zero-ended string, quoted
std::string text_in(Tables& tables, std::uint32_t va, int n);

// a float with the fewest digits that read back to the same bits
std::string fl(float value);
// stat curves (tables.h CurveF, CurveU, CurveS): "(curve base at_mid at_max mid negative)"
std::string curve_f(const Xbe& xbe, std::uint32_t va);
std::string curve_u(const Xbe& xbe, std::uint32_t va);
std::string curve_s(const Xbe& xbe, std::uint32_t va);
// "(name value)"
std::string field(const std::string& name, const std::string& value);
std::string field(const std::string& name, long long value);
// "(name value symbol)": an enum with its number
std::string field_enum(const std::string& name, long long value, const char* symbol);
// "(name a b c ...)": n values of one kind at va
std::string ints_i32(const std::string& name, const Xbe& xbe, std::uint32_t va, int n);
std::string ints_i16(const std::string& name, const Xbe& xbe, std::uint32_t va, int n);
std::string ints_u8(const std::string& name, const Xbe& xbe, std::uint32_t va, int n);
std::string ints_i8(const std::string& name, const Xbe& xbe, std::uint32_t va, int n);
std::string floats(const std::string& name, const Xbe& xbe, std::uint32_t va, int n);

std::string row_text(const Row& row);
void add_row(TableFile& file, const Row& row);
// writes tables/<name>.sexp and its manifest entry; false on a write failure
bool write_table(Tables& tables, const TableFile& file);

// the table groups, one source file each
bool write_part_tables(Tables& tables);       // tables_parts.cpp
bool write_roster_tables(Tables& tables);     // tables_roster.cpp
bool write_tournament_tables(Tables& tables); // tables_tournaments.cpp
bool write_flow_tables(Tables& tables);       // tables_flow.cpp
bool write_music_tables(Tables& tables);      // tables_music.cpp
bool write_ai_tables(Tables& tables);         // tables_ai.cpp

// the family: every table to data/tables/; false on a write failure
bool extract_tables(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
