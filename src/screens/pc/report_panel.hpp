#pragma once
// The bug reports' own panel over any screen (docs/release.md, "Bug reports"), the port's, in PC
// CONFIG's look (the key config's letters, the option buttons, a dim plate): the question, asked
// once over the title when an install has never answered it ("SEND CRASH REPORTS TO HELP FIX BUGS",
// YES / NO), and REPORT A BUG, opened from the pause (its button among the pause's PC items, or the
// pad's BACK: screens/pc/pause_settings.hpp) or from PC CONFIG's GAME page: a short note typed
// on the keyboard or picked a letter at a time with the pad, the picture of the paused fight if
// wanted, SEND and CANCEL. While it is up the screen under it reads no pad or pointer.
//
// Port-only, headless checks: PHANTOMCRASH_DEV_REPORT=U sends a manual report ("DEV CHECK", the
// fight's record) at front end update U; PHANTOMCRASH_DEV_REPORT_OPEN=U opens REPORT A BUG at U;
// PHANTOMCRASH_DEV_CRASH=U crashes on purpose at U.
#include <SDL3/SDL_events.h>
#include <string>

struct FrontEnd;
struct Ui;

struct ReportPanel {
    int mode = 0;     // 0 closed, 1 the question, 2 REPORT A BUG
    int row = 0;      // REPORT A BUG: 0 the note, 1 PICTURE, 2 the buttons
    int button = 0;   // the question's YES / NO, or SEND / CANCEL
    int letter = 0;   // the pad's letter in the strip
    std::string note; // in the key config's letters
    bool picture_offered = false, picture = true;
    int shot = 0;     // 1 the paused picture is taken this frame (nothing drawn over it), 2 taken
    std::string said; // what happened to the report, shown before it closes
    int said_ticks = 0;
    bool asked = false;  // the question shown this session
    bool typing = false; // SDL's text input on
};

// before the screen's update: the question due, the panel's input, the pads blanked under it
void report_panel_frame(FrontEnd& fe, Ui& ui);
// after the screen's: the panel over it
void report_panel_draw(FrontEnd& fe, Ui& ui);
// every SDL event first: typing, Backspace, Enter and Escape while the note is up; true taken
bool report_panel_event(FrontEnd& fe, Ui& ui, const SDL_Event& event);
// REPORT A BUG opened, with the paused picture offered or not
void report_panel_open(FrontEnd& fe, bool picture);
// the picture's path when the frame being drawn is the one to keep (once), else ""
std::string report_panel_shot(FrontEnd& fe);
// the pause hides its dimming for the frame the picture is taken in
bool report_panel_hides_pause(const FrontEnd& fe);
