#pragma once
// The dealer's steps and the pieces dealer.cpp and dealer_buy.cpp share (decomp dealer_*).
#include "screens/flow/extra_screens.hpp"

#include <string>

struct ShopBuild;

enum DealerStep : int {
    step_load = 0,
    step_fade_in,
    step_intro,
    step_menu,
    step_talk_start,
    step_talk,
    step_machine,
    step_buy,
    step_wheel,
    step_message,
    step_board,
    step_leave_wait,
    step_leave,
    step_done,
};

// shop_dealer_loadout: offer i's build (a row out of range reads row 0), null without builds
const ShopBuild* dealer_machine(FrontEnd& fe, const DealerScreen& d, int i);
// the strips' place: (0x118, 0x2D), clipped to (0x118, 0x2D)-(0x26E, 0x3F)
void dealer_strip_init(Ui& ui, TextStrip& strip, const std::string& text);
// dealer_screen_finish
void dealer_finish(FrontEnd& fe, Ui& ui, DealerScreen& d, int result);
// dealer_buy.cpp: buying (dealer_buy_start, the wheel dealer_garage_select_update, the boxes
// dealer_message_update, the board question dealer_board_choice_update)
void dealer_buy_start(FrontEnd& fe, Ui& ui, DealerScreen& d);
void dealer_wheel_update(FrontEnd& fe, Ui& ui, DealerScreen& d);
void dealer_message_update(FrontEnd& fe, Ui& ui, DealerScreen& d);
void dealer_board_update(FrontEnd& fe, Ui& ui, DealerScreen& d);
