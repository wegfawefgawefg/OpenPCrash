#pragma once
// VS setup (state 0x2C, decomp docs/port/screens.md "VS setup (0x2C)", src/o1/u_03c67e.c
// 0x3F44F..0x425E9): the join page (who plays), the machine page (each player locks a build), the
// area page, then state 6 with vs_mode (the battle session's round, src/battle_vs.cpp). A player is a seat (src/input/seats.hpp): any device that
// presses accept on the join page takes the first free panel, in any order, and leaves with
// cancel, so it can come back on another device. Panel i is seat i: its view, HUD and record.
#include "save/vs_saves.hpp"
#include "screens/ui/screen_kit.hpp"
#include "screens/ui/screen_text.hpp"
#include "screens/ui/ui.hpp"

#include <array>
#include <vector>

struct FrontEnd;
struct Seats;

inline constexpr int vs_panel_count = 4;
// a panel's four parts (PANEL_FRAME, _LIST, _TITLE, _HINT), each 0 off, 1 opening, 2 open,
// 3 closing
enum VsPart : int { vs_frame = 0, vs_list = 1, vs_title = 2, vs_hint = 3 };

struct VsPanel {
    int anim[4] = {};
    float clock[4] = {};
    bool locked = false; // the machine page: its choice is in
    int cursor = 0;      // the machine list's lit cell in its window of three (row top + cursor)
    // the machine list: its first row in view and the one wanted, the scroll (1, 2 towards
    // earlier rows, 3, 4 later, each half 4 frames), a kept machine that didn't read (its card)
    int top = 0, top_wanted = 0, scroll = 0;
    float scroll_clock = 0;
    bool reading = false;
    float error_clock = 0;
    // PC: the seat just joined on a press still held; its pad counts once everything is let go
    bool wait_release = false;
    int joined_update = 0; // the update it joined on: its device's first read is the one after
};

// VsScreen and the VS parts of GameState that outlive it (entered, builds, the return page)
struct VsSession {
    std::array<bool, vs_panel_count> entered{};
    std::array<int, vs_panel_count> build{};  // the locked machine: a loadout row
    std::array<int, vs_panel_count> kept{-1, -1, -1, -1}; // or a kept one (saves), -1 none
    std::vector<VsSave> saves; // KEEP SV's machines, read as the machine page opens
    int return_page = -1;   // vs_return_page_set: 1 the machine page, 2 the area page, -1 fresh
    int selector = 0;       // the area picked (rumble_selected)
    // the screen
    int step = 0;  // which function of the chain runs
    int sub = 0;   // screen->step inside it
    int clock = 0; // the step's own frame count
    int choice = 0;
    std::array<VsPanel, vs_panel_count> panels;
    TextStrip help[3];
    MsgBox quit_box; // vs_quit_box
    // the area page
    int area = 0, area_shown = 0, area_phase = 0, area_clock = 0, area_picked = 0;
    int area_choice = 0, was_choice = 0;
    int plate_start = -1, plate_pick = 0; // the picture's switch (AreaStrip.start, .pick)
    Slide labels;
    int fade_wait = 0;
};

// the chain, by the original's function
enum VsStep : int {
    vs_step_load = 0,
    vs_step_join_reset = 5,
    vs_step_join_intro = 9,
    vs_step_join = 11,
    vs_step_machine_reset = 6,
    vs_step_machine_intro = 12,
    vs_step_machine = 13,
    vs_step_area_reset = 7,
    vs_step_area = 10,
    vs_step_area_leave = 14,
    vs_step_start = 8,
};

// the chain moves on: the step's own clock and sub-step from the start
void vs_go(FrontEnd& fe, int step);
// an entered seat's pad record, or none while it still holds the press it joined with
const PadRecord* vs_seat_pad(FrontEnd& fe, Ui& ui, int seat);
// the mouse is the keyboard seat's (mouse_seat, -1 while the keyboard is nobody's): its left click
// is that seat's accept (on rect when one is given), its right click its cancel; hovering moves
// that seat's cursor
int vs_mouse_seat(const FrontEnd& fe);
bool vs_accept(const FrontEnd& fe, const Ui& ui, int seat, const PadRecord& pad,
               const PointerRect* rect = nullptr);
bool vs_cancel(const FrontEnd& fe, const Ui& ui, int seat, const PadRecord& pad);
// the rectangle the seat's pointer moved onto or clicked this update, -1 none or not the mouse's
int vs_hover(const FrontEnd& fe, const Ui& ui, int seat, const PointerRect* rects, int count);

// the state machine's hooks (front_end.cpp): 0x2C's screen (state 6 with vs_mode is the
// battle screen's, src/battle_vs.cpp)
bool vs_screens_enter(FrontEnd& fe, Ui& ui, int state);
bool vs_screens_step(FrontEnd& fe, Ui& ui);
bool vs_screens_destroy(FrontEnd& fe, Ui& ui);
bool vs_screens_next(FrontEnd& fe, int r, int& next);

// vs_draw.cpp: the panels, the machine list, the area page
void vs_panel_anim(VsPanel& panel, int part, int anim);
void vs_panels_tick(VsSession& vs);
void vs_panels_draw(FrontEnd& fe, Ui& ui, int page);
// vs_panels_stagger_open (intros), vs_panels_open (closes everything), vs_panels_close
void vs_panels_stagger(FrontEnd& fe, int page, int clock);
void vs_panels_close_all(VsSession& vs, int clock);
void vs_panels_close_contents(VsSession& vs, int clock);
void vs_panel_idle(FrontEnd& fe, int page, int i);
// vs_area.cpp: the area page's input (step10) and drawing
void vs_area_step(FrontEnd& fe, Ui& ui);
void vs_area_draw(FrontEnd& fe, Ui& ui);
// the vs_mode file's numbers by address (tables that run into each other, Words rows by field)
float vs_number(Ui& ui, unsigned address);
// the area labels' slide in (sprite_slide_init over 24 ticks)
void vs_labels_start(FrontEnd& fe, Ui& ui);
// rows of vs_sprites_2, vs_sprites, vs_area_select_sprites
const SpriteSlot* vs_row(Ui& ui, int row);
void vs_row_draw(Ui& ui, int row, float scale_x = 1, float scale_y = 1, int anchor = 0,
                 std::uint32_t colour = 0, bool use_colour = false);
// the page frame (vs_sprites, the page's label row, its help line); the start question (rows 0, 2,
// grown by s) and its answer from any entered player: 1 yes, 0 no, -1 still asking
void vs_page_draw(FrontEnd& fe, Ui& ui, int page);
void vs_confirm_draw(FrontEnd& fe, Ui& ui, float s);
int vs_confirm_input(FrontEnd& fe, Ui& ui);
// a panel's (x, y) from a table of four pairs (D_001FFB38 wins, D_001FFB58 losses, ...)
void vs_pair_at(Ui& ui, unsigned table, int i, float& x, float& y);
// vs_machines.cpp: the machine page: its reset (KEEP SV's machines read, the lists at the top),
// its step, each panel's list and locked machine, the cells in view as the pointer's hit boxes
// (clipped to the panel), and the picks written to VsSession::build and kept
void vs_machines_reset(FrontEnd& fe);
void vs_machine_step(FrontEnd& fe, Ui& ui);
void vs_machine_draw(FrontEnd& fe, Ui& ui, int i, float fill);
void vs_locked_draw(FrontEnd& fe, Ui& ui, int i, float fill);
void vs_machine_rects(Ui& ui, int i, PointerRect* out);
void vs_machine_picks(FrontEnd& fe, Ui& ui);
// the stock machines (vs_stock_loadouts) and the area page's ids
int vs_stock_loadout(Ui& ui, int row);
inline constexpr int vs_stock_count = 3;
