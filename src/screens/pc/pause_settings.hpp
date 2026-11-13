#pragma once
// The pause's settings (the port's own, docs/settings.md "From the pause"). Every fight's pause
// (a tournament's, the test drive's, free play's and the lessons', VS's) has the PC's buttons
// under it in PC CONFIG's look: OPTIONS, PC CONFIG, MUSIC and REPORT A BUG (in a build with a
// report address). Free play's and VS's menus run their cursor on through them after Continue and
// Quit; a tournament's pause, which has no menu, gets a cursor of its own over them, two a row so
// they clear the entrance's letterbox. BACK still opens REPORT A BUG at once.
//
// MUSIC opens the MUSIC panel (music_panel.hpp) the same way: the fight's song, a song picked,
// the mode and the playlists.
//
// OPTIONS and PC CONFIG open PC CONFIG's pages over the paused fight (screen.pc, `paused`), with
// OPTIONS first: the original's options a fight can take (vibration, BGM and SE volume, the
// screen adjust), applied and kept as the options screen keeps them. EXIT, or Back on it, goes
// back to the pause on the item it left. While the panel is up the pause reads no pad or pointer,
// Start can't end the pause, Escape is Back as in the menus and the mouse is free (main's steps,
// src/front_end_steps.cpp). The fight stands still under it: the world doesn't step and nothing
// here writes it, so the fight's record, its replay and its hashes don't change.
#include "screens/pc/pc_settings_screen.hpp"
#include "screens/ui/menu_pointer.hpp"

struct FrontEnd;
struct Ui;

// the pause's PC items: OPTIONS, PC CONFIG, MUSIC, then REPORT A BUG when reports are built in
inline constexpr int pause_item_options = 0, pause_item_pc = 1, pause_item_music = 2,
                     pause_item_report = 3;
inline constexpr int pause_items_max = 4;
int pause_items_count();
// under the menu's plate (free play, VS), or right under PAUSE two a row so they clear the
// entrance's letterbox (a tournament's)
PointerRect pause_item_rect(int item, bool menu_of_its_own);
// the items drawn under the pause, the cursor's lit (menu: the menu's cursor past its own two;
// a tournament's pause moves its cursor over them here and opens one on A or a click); BACK opens
// REPORT A BUG on any pause
void pause_items(FrontEnd& fe, Ui& ui, bool menu_of_its_own);
// A or a click on the item opens it; never Start, which ends the pause
bool pause_item_accepted(const Ui& ui, const PointerRect& rect);
// an item opened, its press used up
void pause_item_open(FrontEnd& fe, Ui& ui, int item);

// before the screen's update: the panel's input and drawing over the pause, which then reads
// nothing; closed when the pause is gone
void pause_settings_frame(FrontEnd& fe, Ui& ui);
void pause_settings_close(FrontEnd& fe, Ui& ui);

// OPTIONS' rows: VIBRATION, BGM VOLUME, SE VOLUME, the screen adjust across and down, and BUTTON
// CONFIG, shown but not changed in a fight (the buttons are the world's, part of its record)
int pause_options_count();
PcRowView pause_options_view(FrontEnd& fe, int row);
// left or right: true when it changed (applied at once)
bool pause_options_step(FrontEnd& fe, Ui& ui, int row, int step);
