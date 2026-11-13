#pragma once
// The screens' payloads (each state's child task) and their steps. A screen's `step` is which
// function of the original's chain runs next (X_screen_update, _step2, ...); every step redraws
// the whole screen.
#include "movie/movie_screens.hpp"
#include "save/save.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/garage_screens.hpp"
#include "screens/flow/menu_screens.hpp"
#include "screens/flow/shop_screens.hpp"
#include "screens/pc/pc_settings_screen.hpp"
#include "screens/ui/screen_text.hpp"
#include "screens/ui/screen_widgets.hpp"

struct FrontEnd;
struct Ui;

// logo_screen_* (0x12)
struct LogoScreen {
    int counter = 0;
};

// title_return_screen_* (0x28) and title_screen_* (0x15)
struct TitleScreen {
    int from = 0;       // the state it was opened from
    int timer = 0;      // the idle countdown (30 s on title return, 75 s on the title)
    int cursor = 0;     // 0 new game / load, 1 VS, 2 options
    int sub_cursor = 3; // 3 new game, 4 load
    int leave = -1;     // +0x10 / +0xC: 0x28 when going back without a fade
    int result = 0;     // +0x18
    float logo_clock = 0; // title_logo_draw's scroll
};

// options_screen_* (0x1A)
struct OptionsScreen {
    int from = 0;
    int cursor = 0;
    int choice = 0;  // the vibration box's answer (+0xC)
    int confirm = 1; // the defaults box's answer (+0x14)
    int result = 0;
    TextStrip help[7]; // and the port's PC CONFIG (item 5)
    MsgBox boxes[3];   // options_boxes: defaults confirm, defaults done, vibration
};

// sound_config_screen_* (0x23)
struct SoundConfigScreen {
    int cursor = 0;
    int values[4] = {}; // music volume, effect volume, sound test id, exit
    TextStrip help[4];
};

// records_check_* (9), save_files_* (0x2E), save_load_screen_* (0x11)
struct SaveScreen {
    int from = 0;
    int status = 0;
    int menu = 0;
    int choice = 0;
    int result = 0;
    int slot = 0; // the slot shown (retail: only 0; the port has three)
    float wait = 0;
    MsgBox box;
    TextStrip help;
    SaveImage images[save_slot_count];
    SaveRead reads[save_slot_count] = {SaveRead::Missing, SaveRead::Missing, SaveRead::Missing};
};

// a state this port has no screen for yet: says so, and goes on as the original would
struct PlaceholderScreen {
    int state = 0;
    int result = 0;
    GlyphText lines[4];
};

struct Screen {
    int step = 0;
    LevelMeter meter;
    NowPlaying strip;
    LogoScreen logo;
    TitleScreen title;
    OptionsScreen options;
    SoundConfigScreen sound;
    SaveScreen save;
    MovieScreen movie;
    PlaceholderScreen placeholder;
    NameEntryScreen name_entry;
    GarageHubScreen hub;
    AssembleScreen assemble;
    SpecScreen spec;
    StockScreen stock;
    PlusTechScreen plus;
    WildScreen wild;
    // menu_screens.hpp
    SystemScreen system;
    KeyConfigScreen keys;
    AdjustScreen adjust;
    RecordScreen record;
    HelpScreen help;
    CampaignScreens campaign; // the day round (screens/campaign/campaign.hpp)
    // extra_screens.hpp
    SonicAmpScreen sonic;
    MediaScreen media;
    DealerScreen dealer;
    PcSettingsScreen pc; // the port's PC CONFIG (0xA0)
};

// boot_screens.cpp: 0x12 logo, the movie states (0x9E, 0x2D, 2, 0x19), 1 attract demo
void logo_spawn(FrontEnd& fe, Ui& ui);
void logo_step(FrontEnd& fe, Ui& ui);
void logo_destroy(FrontEnd& fe, Ui& ui);
void movie_state_spawn(FrontEnd& fe, Ui& ui, MovieScreenKind kind);
void movie_state_step(FrontEnd& fe, Ui& ui);
void movie_state_destroy(FrontEnd& fe, Ui& ui);
void attract_demo_step(FrontEnd& fe, Ui& ui);
void attract_demo_destroy(FrontEnd& fe, Ui& ui);

// title_screens.cpp: 0x28 title return, 0x15 title
void title_return_spawn(FrontEnd& fe, Ui& ui);
void title_return_step(FrontEnd& fe, Ui& ui);
void title_return_destroy(FrontEnd& fe, Ui& ui);
void title_spawn(FrontEnd& fe, Ui& ui);
void title_step(FrontEnd& fe, Ui& ui);
void title_destroy(FrontEnd& fe, Ui& ui);

// options_screens.cpp: 0x1A options, 0x23 sound config
void options_spawn(FrontEnd& fe, Ui& ui);
void options_step(FrontEnd& fe, Ui& ui);
void options_destroy(FrontEnd& fe, Ui& ui);
void sound_config_step(FrontEnd& fe, Ui& ui);
void sound_config_destroy(FrontEnd& fe, Ui& ui);

// save_screens.cpp: 9 records check, 0x2E save files, 0x11 save load
void records_check_step(FrontEnd& fe, Ui& ui);
void save_files_step(FrontEnd& fe, Ui& ui);
void save_load_step(FrontEnd& fe, Ui& ui);
void save_load_destroy(FrontEnd& fe, Ui& ui);

// story_screens.cpp: the conversation states (0x30..0x9D) through the talk task, the places the
// port has no screen for yet
void talk_state_spawn(FrontEnd& fe, Ui& ui);
void talk_state_step(FrontEnd& fe, Ui& ui);
void talk_state_destroy(FrontEnd& fe, Ui& ui);
void placeholder_spawn(FrontEnd& fe, Ui& ui);
void placeholder_step(FrontEnd& fe, Ui& ui);

// name_entry.cpp: 0x16 name entry
void name_entry_spawn(FrontEnd& fe, Ui& ui);
void name_entry_step(FrontEnd& fe, Ui& ui);
void name_entry_destroy(FrontEnd& fe, Ui& ui);
// name_entry_draw.cpp: name_entry_cursor_rows and the steps after them (D_001F94E0 row 2): three
// cursors {sprite, x, y, blend}, their flags, four kind labels {sprite, x, y, blend}, the column,
// row, tab, tab row and caret steps
struct NameEntryLayout {
    std::vector<double> v;
    double at(std::size_t i) const {
        return i < v.size() ? v[i] : 0.0;
    }
};
NameEntryLayout name_entry_layout(Ui& ui);
// glyph_index of a grid cell (name_entry_grid [page][row][col]), -1 for none
int name_entry_cell_glyph(Ui& ui, int page, int row, int col);
// name_entry_screen_draw
void name_entry_draw(FrontEnd& fe, Ui& ui, NameEntryScreen& n, bool cursors_lit);
// garage_hub.cpp: 0xD the garage hub
void garage_hub_spawn(FrontEnd& fe, Ui& ui);
void garage_hub_step(FrontEnd& fe, Ui& ui);
void garage_hub_destroy(FrontEnd& fe, Ui& ui);
// assemble_screen.cpp: 4 the parts editor
void assemble_spawn(FrontEnd& fe, Ui& ui);
void assemble_step(FrontEnd& fe, Ui& ui);
void assemble_destroy(FrontEnd& fe, Ui& ui);
// spec_stock_screens.cpp: 0x24 the spec page, 0x26 stock
void spec_spawn(FrontEnd& fe, Ui& ui);
void spec_step(FrontEnd& fe, Ui& ui);
void spec_destroy(FrontEnd& fe, Ui& ui);
void stock_spawn(FrontEnd& fe, Ui& ui);
void stock_step(FrontEnd& fe, Ui& ui);
void stock_destroy(FrontEnd& fe, Ui& ui);
// plus_tech.cpp: 0x1B Plus Tech
void plus_tech_spawn(FrontEnd& fe, Ui& ui);
void plus_tech_step(FrontEnd& fe, Ui& ui);
void plus_tech_destroy(FrontEnd& fe, Ui& ui);
// wild_machines.cpp: 0xF Wild Machines
void wild_spawn(FrontEnd& fe, Ui& ui);
void wild_step(FrontEnd& fe, Ui& ui);
void wild_destroy(FrontEnd& fe, Ui& ui);
