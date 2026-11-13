#pragma once
// The name entry, garage group and shop screens' payloads (decomp include/payloads.h
// NameEntryScreen, include/garage.h Garage, include/chip_screen.h PlusTechScreen,
// include/shop_screen.h ShopScreen) and the globals they leave for each other (GameState
// +0x10EA4 the garage being edited, +0x10F5C the spec page's build, name_entry_target).
#include "screens/garage/garage_rules.hpp"
#include "screens/garage/model_stand.hpp"
#include "screens/ui/screen_kit.hpp"
#include "screens/ui/screen_text.hpp"

// name_entry_target_set: what the name entry edits
struct NameTarget {
    int kind = 0;                    // 0 pilot, 1 garage, 2 an item (a Chip), 3 a playlist
    std::int16_t* buffer = nullptr;  // up to 8 glyphs and the -1 (the scratch name when null)
    std::int16_t scratch[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

// the Wild Machines menu kept over a trip to the spec page or name entry (0x415D78..0x415D88)
struct WildReturn {
    int menu = 0, cursor = 0, cursor2 = 0, next = 0;
    int chip_slot = -1; // garage_slot: the bought Chip's inventory slot
};

struct ShopGlobals {
    PartTables tables;
    int editing = 0;   // GameState +0x10EA4 (f_10ea4)
    NameTarget name;
    ShopBuild spec;    // +0x10F5C, the RosterEntry the spec page shows
    GlyphName spec_name = {-1, -1, -1, -1, -1, -1, -1, -1, -1};      // +0x10F98
    GlyphName spec_chip_name = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // +0x10FAA
    WildReturn wild;
    ModelStand stand;  // the 3D model viewer the garage screens share
};

// name_entry_* (0x16), NameEntryScreen
struct NameEntryScreen {
    int from = 0;
    int frames = 0;
    int length = 0;
    int col = 0, row = 0, on_tabs = 0, was_on_tabs = 0, tab = 0, tab_row = 6;
    int page = 0, prev_page = 0, saved_row = 0;
    float page_fade = 1, title_x = 640, title_alpha = 0, grid_alpha = 0;
    int pop_step = 0;
    int timer = -1; // the banner's
    int choice = 1; // name_entry_choice
    int result = 0;
    float pulse = 0; // name_entry_pulse_phase
    TextStrip help[3];
    MsgBox boxes[3];
    // the PC keyboard's, for the next input (name_entry_keys.cpp): glyphs typed,
    // name_typed_delete, name_typed_done
    std::vector<int> typed;
};

// garage_screen_* (0xD), the hub
struct GarageHubScreen {
    int from = 0;
    int frames = 0;
    int cursor = 0, shown = 0;
    bool turning = false;
    int turn = 0;
    int timer = -1;
    int result = 0;
    TextStrip labels[8];
};

// assemble_screen_* (4), the parts editor (Garage)
struct AssembleScreen {
    int from = 0;
    int frames = 0;   // +0x04
    int column = 0;   // +0x1C grid_row: the grid's column 0..5
    int row = 0;      // +0x20 grid_col: its row 0..1
    int strip_choice = 1;
    int banner = -1;  // +0x34
    Slide top, bottom; // +0x38.. the grid's two slides, one start between them
    int side_start = -1; // +0x6C
    int card_start = -1; // +0x70
    int result = 0;      // +0x3624
    bool available[12] = {};
    PopTimer frame[6];   // +0xB5CC: the list frame (0..2) and the card frame (3..5)
    GarageEditor editor;
    TextStrip help[12];
    MsgBox boxes[2];     // garage_strip_confirm_boxes: dismantle, options dropped
    // the mouse dragging a list row onto the grid (the port's)
    int drag_row = -1;
    bool dragging = false;
};

struct FrontEnd;

// assemble_draw.cpp: the editor's drawing (garage_screen_draw, garage_list_draw,
// garage_side_tabs_draw, garage_frame_draw) and the help strips
// (garage_help_set)
void assemble_frame_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s, int flip);
void assemble_list_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s);
void assemble_side_tabs_draw(Ui& ui, AssembleScreen& s);
bool assemble_list_frame_draw(Ui& ui, AssembleScreen& s, int mode);
void assemble_frame_timers(Ui& ui, AssembleScreen& s, int which); // init and slide in
void assemble_help_set(Ui& ui, AssembleScreen& s, int set);
// the six visible rows' and the grid cells' hit boxes
PointerRect assemble_row_rect(int i);
PointerRect assemble_cell_rect(Ui& ui, const AssembleScreen& s, int row, int column);
// the category of a grid cell (assemble_grid_categories)
int assemble_category(int column, int row);
// the grid's slides back to their starts (sprite_slide_init twice)
void assemble_grid_reset(AssembleScreen& s);

// part_card.cpp: part_card_draw (any screen's card) and the editor's card
// (garage_part_card_draw with garage_stat_compare_draw)
void part_card_draw(Ui& ui, const PartTables& t, const InventoryItem& card, int x, int y,
                    int line, int a, int b, int blend, bool compact);
void assemble_card_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s);
// the row marks of a list (garage_row_frame_draw): plate, garage number, side, class
void garage_row_marks_draw(Ui& ui, const PartTables& t, const Garages& g, const GarageEditor& e,
                           int garage, const GarageEntry& entry, int mode, float y,
                           std::uint32_t color);

// spec_scoobee_screen_* (0x24)
struct SpecScreen {
    int from = 0;
    int frames = 0;
    int page = 0;
    TextStrip help[2];
};

// stock_module_* (0x26)
struct StockScreen {
    int from = 0;
    int frames = 0;
    int tab_col = 0, tab_row = 0, tab = 0;
    int cursor = 0, scroll = 0;
    Slide tabs, bottom_tabs;
    PopTimer frame;
    PopTimer plates[2]; // the card's label plates (StockScreen +0x28684, +0x286B0)
    GaragePage pages[8];
    TextStrip help;
};
