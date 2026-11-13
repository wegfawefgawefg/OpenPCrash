#pragma once
// PC CONFIG (state 0xA0, the port's own, docs/settings.md): reached from the options screen's PC
// CONFIG item, laid out like the original's sound config and key config. The pages are a column
// of option buttons on the left (SCREEN, PICTURE, SOUND, INPUT, BINDINGS, GAME, EXIT); the chosen
// page's rows are the key config's bars on the right, each a label and a value stepped with left
// and right, applied and saved as it changes. From a fight's pause the same pages open over the
// paused fight (screens/pc/pause_settings.hpp), OPTIONS first.
#include "screens/pc/pc_settings.hpp"
#include "screens/pc/pc_settings_art.hpp"
#include "screens/ui/screen_text.hpp"

#include <SDL3/SDL.h>
#include <string>

struct FrontEnd;
struct CheatRules;

enum PcPage {
    page_screen,
    page_picture,
    page_sound,
    page_input,
    page_bindings,
    page_game,
    page_exit,
    page_options,  // the pause's only: the original's options a fight can take
    page_music,    // the MUSIC panel's (music_panel.hpp): the fight's song and the mode
    page_playlist, // and its playlist editor
    page_cheats,   // CHEATS (pc_cheats.cpp), opened from GAME's CHEATS row; GAME stays lit
    page_count
};

// STICK CALIBRATION's panel (pc_settings_calibrate.cpp): one pad, measured in three steps: at
// rest (its centres and the triggers' rest), turned round its edges (each axis's reach, the
// triggers' full pull), then kept and shown through the result
struct CalibrationPanel {
    bool open = false;
    int phase = 0;          // 0 at rest, 1 turning, 2 kept
    SDL_JoystickID pad = 0; // the pad measured
    std::string name; // in the label font's letters
    // the last 16 raw readings at rest: the four stick axes and the two triggers
    int rest[16][6] = {};
    int rest_count = 0;
    int centre[4] = {}, low[4] = {}, high[4] = {};
    int trigger_rest[2] = {}, trigger_max[2] = {};
    float reach[2][32] = {}; // each stick's furthest lean in 32 directions, 0..1 of the travel
    bool held[3] = {};       // the pad's own A, B and Y last update
    std::string note;        // what the last press did, on the help line
};

struct PcSettingsScreen {
    bool paused = false;  // over a paused fight: OPTIONS first, EXIT back to the pause
    int page = 0;         // the column's cursor (a PcPage)
    bool in_rows = false; // the cursor is on the rows
    int row = 0;
    int top = 0; // the first row shown (BINDINGS scrolls)
    int result = 0;
    int bind_player = 0;    // BINDINGS: whose profile, 0..3 (player_profile_first + it)
    bool listening = false; // BINDINGS: the next key, mouse or pad button pressed is the row's
    int listen_ticks = 0;
    int quiet = 0;    // updates the menu ignores accept and back after a bind (the key may be held)
    std::string note; // BINDINGS: what the last bind did, shown on the help line
    int calibrate_pad = 0; // STICK CALIBRATION's pad, an index into SDL's gamepads
    CalibrationPanel calibration;
    TextStrip help;
    std::string help_text; // what the help line was made from
    // the MUSIC panel (music_panel.hpp): its own column, opened from the pause's MUSIC or from
    // SOUND's MUSIC row (EXIT goes back there)
    bool music = false;
    bool music_from_sound = false;
    int music_now = -1;     // the catalogue record playing, read once a frame
    int music_pick = -1;    // PICK A SONG's song
    bool previewed = false; // a song played in the menus: the screen's music again on leaving
};

// a row as drawn
struct PcRowView {
    std::string label;
    std::string value;   // the value (BINDINGS: the keys and mouse)
    std::string value_2; // BINDINGS: the pad
    int icon = -1;       // a pad button's icon before the label
    int bar = -1;        // a volume's fill 0..100, -1 none
    bool arrows = true;  // a value stepped with left and right
    bool dim = false;    // can't be changed now
    std::string help;
    bool wide = false; // the value from just past a short label (the MUSIC panel's)
    std::string title; // a song's title in the original's 18 dot font, in the value's place
    GlyphText glyphs;  // or a name in that font's glyphs (a playlist's)
};

// the rows of the pages but BINDINGS (pc_settings_rows.cpp)
int pc_rows_count(int page);
PcRowView pc_row_view(const PcHost& host, int page, int row);
// left or right: true when the value changed (it's saved and applied)
bool pc_row_step(PcHost& host, int page, int row, int step);
// accept: the page's DEFAULT row puts the page back; true when it did something
bool pc_row_accept(PcHost& host, int page, int row);
// INPUT's STICK CALIBRATION row: its value is the pad, its accept opens the panel
bool pc_row_is_calibrate(int page, int row);
// GAME's REPORT A BUG row: its accept opens the report panel (screens/pc/report_panel.hpp)
bool pc_row_is_report(int page, int row);
// SOUND's MUSIC row: its accept opens the MUSIC panel (screens/pc/music_panel.hpp); its row
bool pc_row_is_music(int page, int row);
int pc_music_row();
// GAME's CHEATS row: its accept opens the CHEATS page; its row
bool pc_row_is_cheats(int page, int row);
int pc_cheats_row();
// a row a fight takes at its start (SAFE FRAME): over the pause it says it waits for the next
bool pc_row_next_fight(int page, int row);
std::string pc_page_help(int page);

// CHEATS (pc_cheats.cpp): money, songs, paints, garages, a day, and the price rules, on the
// world's story (story/cheat_rules.hpp); only between fights in a game (not from the title, not
// over the pause)
int pc_cheats_count();
PcRowView pc_cheats_view(FrontEnd& fe, int row);
// left or right on a rule: true when it changed
bool pc_cheats_step(FrontEnd& fe, int row, int step);
// accept on a cheat: true when it did something
bool pc_cheats_accept(FrontEnd& fe, int row);
bool pc_cheats_allowed(const FrontEnd& fe);
// the rules in a line, for the log and bug reports ("retail" when nothing was used)
std::string pc_cheats_text(const CheatRules& rules);

// BINDINGS (pc_settings_binds.cpp, pc_settings_axes.cpp): the picked player's own binding
// profile: PLAYER, a row per pad button, the axes, DEFAULT
int pc_binds_count();
PcRowView pc_bind_view(FrontEnd& fe, int row);
// left or right on PLAYER or an axis: true when it changed
bool pc_bind_step(FrontEnd& fe, int row, int step);
// accept: listen on a pad button row, or put the bindings back on DEFAULT
void pc_bind_accept(FrontEnd& fe, int row);
// while listening (and Delete on a row): the event's key, mouse or pad button bound; true when the
// event was taken
bool pc_bind_event(FrontEnd& fe, Ui& ui, const SDL_Event& event);

// STICK CALIBRATION (pc_settings_calibrate.cpp): the row's pad named ("PAD 1"), stepped among
// those plugged in; the panel opened on it, stepped and drawn in place of the rows; Delete puts
// the pad back as it came
std::string pc_calibrate_value(FrontEnd& fe);
bool pc_calibrate_step(FrontEnd& fe, int step);
void pc_calibrate_open(FrontEnd& fe, Ui& ui);
void pc_calibrate_update(FrontEnd& fe, Ui& ui);
void pc_calibrate_draw(FrontEnd& fe, Ui& ui);
std::string pc_calibrate_help(FrontEnd& fe);
bool pc_calibrate_event(FrontEnd& fe, Ui& ui, const SDL_Event& event);

void pc_settings_spawn(FrontEnd& fe, Ui& ui);
void pc_settings_step(FrontEnd& fe, Ui& ui);
// the menu's input (the screen's step 2) and its drawing, the pause's panel's too
void pc_settings_input(FrontEnd& fe, Ui& ui);
void pc_settings_draw(FrontEnd& fe, Ui& ui, bool pulse);
void pc_settings_destroy(FrontEnd& fe, Ui& ui);
// every SDL event before the pads see it: true when PC CONFIG took it (listening for a bind)
bool pc_settings_event(FrontEnd& fe, Ui& ui, const SDL_Event& event);

// the options screen's PC CONFIG item and its help line
void pc_options_item_draw(FrontEnd& fe, Ui& ui, float x, float y, bool lit, int blend,
                          std::uint32_t flags);
std::string pc_options_help();
// UTF-8 to the full-width forms the help lines' font has, padded as the original's help lines
// are: `lead` ideographic spaces before (the options' table has 30, the sound config's 34; data/ui
// layouts options.sexp and sound_config.sexp) and 4 after
inline constexpr int help_lead_options = 30;
inline constexpr int help_lead_sound_config = 34; // PC CONFIG is the sound config's frame
std::string pc_help_text(const std::string& text, int lead);
