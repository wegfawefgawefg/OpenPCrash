#pragma once
// Where the bug reports' panel puts things (report_panel_draw.cpp), on the original's 640x480, for
// its input (report_panel.cpp) and its drawing alike.
#include "screens/ui/ui.hpp"

#include <cstddef>

struct FrontEnd;

inline constexpr std::size_t report_note_most = 180; // letters in a note (three lines)

// the pad's letters: A..Z, the digits, the space and a few signs, all in the key config's letters
int report_strip_size();
char report_strip_letter(int i);
bool report_letter_allowed(char c);

PointerRect report_strip_rect(int i);
PointerRect report_question_button(int i); // YES, NO
PointerRect report_note_button(int i);     // SEND, CANCEL
PointerRect report_picture_rect();
