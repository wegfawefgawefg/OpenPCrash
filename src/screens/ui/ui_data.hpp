#pragma once
// The screens' layout tables (ui/layouts/<screen>.sexp in the screen's pack, docs/ui-assets.md and
// docs/assets.md "Packs"), the font's glyph order for their strings, the level meter timelines,
// and which cooked pack holds each file. A screen's packs are opened when it first needs them and
// closed when it goes, as the original loads its sheets per screen.
#include "talk/talk_script.hpp"

#include <gsexp/sexp.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// SpriteSlot rows (include/tables.h): a run ends at sprite -1
struct SpriteSlot {
    int sprite = -1;
    float x = 0, y = 0;
    int blend = 0;
    std::uint32_t color = 0;
    float z = 0;
    std::uint32_t flags = 0;
};

// MsgBox rows (0x4C bytes): the text is already glyph numbers. "columns" is not a width: the text
// layer above 0x101 less one (func_000BEB8F passes it as wide_text_draw's texture argument).
struct MsgBoxRow {
    std::vector<std::int16_t> lines[6];
    int x = 0, y = 0, columns = 0, size = 18, step = 18;
    std::uint32_t color = 0xffffffff;
    int style = 0, blend = 0;
    float cx = 320, cy = 240, sx = 0.1f, sy = 0.1f;
    bool done = false;
};

struct UiTable {
    std::string name;
    std::string layout;
    unsigned va = 0;
    int count = 0;
    std::vector<double> values;       // plain arrays
    std::vector<std::string> strings; // string tables (UTF-8) and pointer tables (symbols)
    std::vector<SpriteSlot> slots;
    std::vector<MsgBoxRow> boxes;
    std::vector<std::vector<int>> pairs; // NumberFontEntry rows: sprite, advance
    std::vector<std::vector<double>> rows;             // other records: field values in order
    std::vector<std::vector<std::string>> row_strings; // their text (strings, symbols)
};

struct UiFile {
    std::vector<UiTable> tables; // address order
};

// one key of a sprite player timeline (data/ui/timelines)
struct TimelinePlace {
    int depth = 0, move = 0;
    float matrix[4] = {1, 0, 0, 1};
    float x = 0, y = 0;
    float color[4] = {1, 1, 1, 1};
};
struct TimelineFrame {
    int command_count = 0;
    std::vector<TimelinePlace> places;
    bool has_background = false;
    std::uint32_t background = 0;
};
struct Timeline {
    int fps = 60;
    std::vector<int> part_sprites; // by slot
    std::vector<int> part_depths;
    std::vector<TimelineFrame> frames;
};

struct PackLibrary;
struct Pack;

struct UiData {
    std::string data_dir;
    PackLibrary* packs = nullptr;
    std::map<std::string, std::string> pack_of; // data-relative path -> the pack holding it
    std::vector<std::string> opened;            // packs opened for the screen up now
    std::vector<std::string> image_file;        // render registry id -> its PNG under data/
    TalkFont font; // the one font (text/font.sexp): glyph widths and the glyph order
    std::map<std::string, UiFile> files;
    std::map<std::string, Timeline> timelines;
};

bool load_ui_data(UiData& data, PackLibrary& packs, const std::string& data_dir,
                  std::string& error);
// the pack holding a data-relative path, opened (and noted for the screen) when it isn't core
const Pack* ui_pack_for(UiData& data, const std::string& path);
// a .sexp kept as text in a pack, parsed
bool ui_sexp(UiData& data, const std::string& path, gsexp::ParseResult& out, std::string& error);
// the packs the screen opened, closed (its destroy)
void ui_close_screen_packs(UiData& data);
// a screen's layout file, loaded on first use (empty when missing)
const UiFile& ui_file(UiData& data, const std::string& screen);
const UiTable* ui_table(UiData& data, const std::string& screen, const std::string& name);
// count numbers from a table on, running into the tables after it when they follow in memory,
// as code that indexes past a table's end reads them (the extractor splits at every name)
std::vector<double> ui_values(UiData& data, const std::string& screen, const std::string& name,
                              int count);
// the run of SpriteSlot rows from row on (sprite_rows_draw stops at -1)
const SpriteSlot* ui_slots(UiData& data, const std::string& screen, const std::string& name,
                           int row = 0);
const MsgBoxRow* ui_box(UiData& data, const std::string& screen, const std::string& name,
                        int row = 0);
const Timeline* ui_timeline(UiData& data, const std::string& name);

// count strings of a string table from entry skip on, running into the string tables that follow
// it in memory (code indexes a part's line past its kind's table)
std::vector<std::string> ui_strings(UiData& data, const std::string& screen,
                                    const std::string& name, int skip, int count);

// UTF-8 to the font's glyph numbers as sjis_to_glyphs does (unknown characters become glyph 0)
std::vector<std::int16_t> utf8_to_glyphs(const UiData& data, const std::string& text);
