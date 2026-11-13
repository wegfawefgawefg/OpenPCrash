#pragma once
// The ui family: the original's screen and HUD layout tables as .sexp (data/ui/layouts/), every 2D
// sprite of the render registry cut from its texture set as PNG (data/ui/images/, indexed by
// data/ui/images.sexp), the standard images, and the sprite player's two timelines
// (data/ui/timelines/). docs/ui-assets.md describes the outputs.
#include <map>
#include <string>
#include <vector>

struct Disc;
struct ExtractOptions;
struct Manifest;

// where a render registry sprite is drawn from: a layout table's sprite field, or a literal id in
// a screen's code ("code:function"); for a texture set, the function that loads it
struct UiSpriteUse {
    std::string screen;
    std::string table;
};
struct UiSpriteUses {
    std::map<int, std::vector<UiSpriteUse>> sprites; // by render registry id
    std::map<int, std::vector<UiSpriteUse>> sets;    // by asset id
};

// every table of tools/extract/ui_tables.inc to data/ui/layouts/<screen>.sexp; collects the
// sprite ids each one names
bool write_ui_layouts(const Disc& disc, const std::string& out_dir, UiSpriteUses& uses,
                      Manifest& manifest);
// level_meter_a and level_meter_b to data/ui/timelines/<name>.sexp
bool write_ui_timelines(const Disc& disc, const std::string& out_dir, Manifest& manifest);
// the render registry's sprites to data/ui/images/<set>/<id>.png, the standard images to
// data/ui/images/standard/, and data/ui/images.sexp
bool write_ui_images(const Disc& disc, const std::string& out_dir, const UiSpriteUses& uses,
                     Manifest& manifest);

// Shift-JIS table text to UTF-8 through the font's own glyph order tables (cp932 for the kanji
// past them); full-width Latin stays full-width, as the game draws it
struct Xbe;
std::string sjis_to_utf8(const Xbe& xbe, const std::string& text);

bool extract_ui(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
