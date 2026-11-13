#pragma once
// Plus Tech's and Wild Machines' payloads (decomp include/chip_screen.h PlusTechScreen,
// include/shop_screen.h ShopScreen) and their shared pieces.
#include "screens/flow/garage_screens.hpp"

// the centred-card carousel of Plus Tech's Chip view and Wild Machines' Chip pages
struct ChipStrip {
    int cursor = 0, shown = 0;
    int slide = 8; // 0..8, 8 still
    int dir = 0;   // -1 moving right, 1 left (times the spacing: 150 Plus Tech, 145 Wild)
    bool on_leave = false;
    int wobble = 0; // the frame's pop, 0..16
    int flash[4] = {10, 10, 10, 10};
    int belt = 0;
    bool arrows_flip = true;
    int card_frames[3] = {};
};

// plus_tech_* (0x1B), PlusTechScreen
struct PlusTechScreen {
    int frames = 0;
    int menu = 0, menu_before = 0, menu_frames = 0;
    bool turning = false;
    int next = 0, view = 0;
    int banner = -1;
    TextStrip labels[3];
    TextStrip help;  // the help line: a tab's, a part's blurb, a Chip's name, the levels
    MsgBox boxes[10];
    int message = 0;
    int pick = 1;
    // the module view
    int tab = 0, kind = 0;
    float scroll[3] = {};
    Slide tabs;
    PopTimer timers[4];
    GarageEditor editor; // its pages
    int cursor_row = 0, scroll_row = 0;
    int tune = 0, tune_was = 0, price = 0;
    int blurb_part = -1;
    // the Chip view
    ChipStrip chips;
    int chip_tune = 0, chip_tune_was = 0;
};

// a used Scoobee on today's list
struct UsedMachine {
    ShopBuild build;
    int price = 0;
};

// wild_machines_* (0xF), ShopScreen
struct WildScreen {
    int frames = 0;
    int menu = 0, menu_before = 0, menu_frames = 0;
    bool turning = false;
    int mode = 0;  // 0 hub, 1 buy modules, 2 buy a Chip, 3 used, 4 sell modules, 5 sell Chips
    int next = 0;  // the mode the fade goes to
    int result = 0;
    int banner = -1;
    TextStrip labels[6];
    TextStrip help;
    MsgBox boxes[31];
    int message = -1, after = -1; // the box up, and where its OK goes (a step)
    int pick = 1;
    // modules: the tab grid, the list, the stock or the owned page
    int col = 0, row = 0, tab = 0, kind = 0;
    Slide top, bottom;
    PopTimer timers[3];
    int header_x = -400, header_delay = 20;
    float turn = 0;        // the three frames' rotation
    float monitor = 0;     // the monitor strip's scroll
    GaragePage stock;      // today's stock of the tab (buy)
    GarageEditor editor;   // the owned pages (sell)
    int cursor = 0, scroll = 0;
    int price = 0;
    // Chips
    ChipStrip chips;
    std::vector<StockItem> chip_stock;
    int chip_slot = -1;
    // used Scoobees
    std::vector<UsedMachine> used;
    int used_cursor = 0, used_x[7] = {}, submenu = 0, garage = 0;
    bool used_sound_done = false;
};

// plus_tech_draw.cpp: the hub's, the module view's and the tune card's drawing
struct FrontEnd;
void plus_tech_menu_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p);
void plus_tech_frame_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool pulse);
void plus_tech_part_list_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool listing);
void plus_tech_tune_card_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool tuning);
// the item the cursor's row stands for (garage pages' entry), null when the list is empty
const GarageEntry* plus_tech_entry(const PlusTechScreen& p);

// plus_tech_chips.cpp: the Chip view (plus_tech_chip_list_draw, plus_tech_chip_card_draw)
// and its steps
void plus_tech_chip_list_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool input);
void plus_tech_chip_card_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p);
// steps: 0 the carousel, 1 the uplift, 2 the yes/no, 3 the OK boxes; returns the step to go to
// (-1 stay; 100 back to the hub's fade)
int plus_tech_chip_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p, int step);
void plus_tech_chip_setup(FrontEnd& fe, Ui& ui, PlusTechScreen& p);
