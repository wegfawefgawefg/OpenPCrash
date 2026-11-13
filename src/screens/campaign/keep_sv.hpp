#pragma once
// KEEP SV (state 0x2B, decomp src/o1/u_03c67e.c keep_sv_screen_update ..
// keep_sv_screen_step4, keep_sv_screen_draw, keep_sv_row_draw): the VS
// Scoobees kept in storage (src/save/vs_saves.hpp), five at a time on a sliding row, and a side
// menu: the list, SAVE (a whole machine picked in the hangar's keep SV mode, 0xE) and EXIT. A
// kept machine can be looked at on the spec page (0x24) or deleted. Answers (state_keep_sv_update)
// : 0 the system menu, 1 the hangar, 3 the spec page.
#include "save/vs_saves.hpp"
#include "screens/ui/screen_text.hpp"

#include <vector>

// what the screen leaves for its next visit (GameState +0x10FC0..+0x10FD0)
struct KeepSvKept {
    int top = 0, wanted = 0, cursor = 0, menu = 0, chosen = 0;
};

// KeepSv, payload 0x608DC bytes
struct KeepSvScreen {
    int mode = 0;   // +4 the state it came from: 0x27 fresh, 0xE back from the hangar
    int state = 0;  // the input state, 0..15
    int next = 0;   // +0x608D0 the answer once faded out
    int sel = 0;    // a box's answer
    KeepSvKept at;  // the list's window and cursor, the side menu
    float clock = 0;        // +0x608D4 ticks to 75: the header, the list, the rows fade in
    float scroll_clock = 0; // the row's slide, 4 ticks a half
    int scroll = 0;         // 0 still, 1 / 3 sliding out (left / right), 2 / 4 sliding in
    int banner_timer = -1;
    TextStrip text;
    std::vector<VsSave> saves; // the list (SaveList), read on entry and after each change
    MsgBox boxes[15];          // D_001FFE10's rows
};

struct FrontEnd;
struct Ui;

inline constexpr int keep_sv_rows = 5;         // the kept machines shown at once
inline constexpr float keep_sv_clock_end = 75; // the screen's fade-in clock, ticks
// the input states (sv->state)
namespace keep_sv_states {
enum : int {
    s_list = 0,
    s_save_item = 1,
    s_kept = 2,      // a kept machine picked: look, delete, cancel
    s_damaged = 3,   // a damaged one picked: delete, cancel
    s_save_ask = 4,  // save a machine from the hangar?
    s_saved = 5,
    s_overwrite = 6,
    s_none_to_save = 7,
    s_delete_ask = 8,
    s_exit_item = 9,
    s_read_failed = 10,
    s_save_failed = 11,
    s_delete_failed = 12,
};
} // namespace keep_sv_states

// keep_sv_draw.cpp: keep_sv_screen_draw (the screen, the rows, the boxes) and the kept machine
// under place n of the seven (n - 1 from the window's top), null for none
void keep_sv_draw(FrontEnd& fe, Ui& ui, KeepSvScreen& k);
const VsSave* keep_sv_slot(const KeepSvScreen& k, int n);
// the mouse's hit boxes, on the art keep_sv_draw places: place n of the seven (1..5 on screen),
// and row k of keep_sv_sprites from 4 on less its see-through margins (left, top, right, bottom)
PointerRect keep_sv_place_rect(Ui& ui, int n);
PointerRect keep_sv_row_rect(Ui& ui, int k, const float art[4]);
// the side menu's SAVE (1) and EXIT (2), and the open box's answers
PointerRect keep_sv_side_rect(Ui& ui, int item);
void keep_sv_answer_rects(Ui& ui, KeepSvScreen& k, PointerRect out[3]);
// the port's mouse over the list and the side menu: a place under it takes the list's cursor
// (sound 4), SAVE or EXIT the side menu's (sound 3); a click on a scroll arrow, or the wheel over
// the list, scrolls it as left and right do
void keep_sv_pointer_step(Ui& ui, KeepSvScreen& k);

void keep_sv_spawn(FrontEnd& fe, Ui& ui);
void keep_sv_step(FrontEnd& fe, Ui& ui);
void keep_sv_destroy(FrontEnd& fe, Ui& ui);
