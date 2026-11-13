#pragma once
// Wild Machines' internals shared by the screen (wild_machines.cpp: the wheel, the boxes, the mode
// changes) and the modes' input steps (wild_steps.cpp: parts tabs and list, Chips, used
// Scoobees). Only those two include it.
#include "screens/flow/front_end.hpp"
#include "screens/shops/wild_machines.hpp"

#include <string>

namespace wild {

enum Step {
    s_update, s_fade_in, s_hub, s_switch, s_leave, s_box, s_yes_no,
    s_tabs, s_list,
    s_chips, s_chip_confirm,
    s_used, s_submenu, s_garages,
    s_done
};
// what an OK box or a yes/no answer leads to
enum After { a_list, a_tabs, a_hub, a_chips, a_chips_reset, a_used, a_garages, a_chip_name,
             a_used_name, a_used_board, a_buy, a_sell, a_chip_sell, a_chip_sold };

float half(const Ui& ui);
std::string line(Ui& ui, const char* table, int i);
void box(WildScreen& w, int m, int after);
bool selling(const WildScreen& w);
void view_draw(FrontEnd& fe, Ui& ui, WildScreen& w, int step);
GaragePage& rows_of(WildScreen& w);
void pages_build(FrontEnd& fe, WildScreen& w);
void go_mode(Ui& ui, WildScreen& w, int mode);
void leave_to(FrontEnd& fe, Ui& ui, WildScreen& w, int result);

// wild_steps.cpp
void stock_fill(FrontEnd& fe, WildScreen& w);
void used_fill(FrontEnd& fe, Ui& ui, WildScreen& w);
void chip_name_from_record(FrontEnd& fe, int slot);
void tabs_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void list_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void chips_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void chip_confirm_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void used_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void submenu_step(FrontEnd& fe, Ui& ui, WildScreen& w);
void garages_step(FrontEnd& fe, Ui& ui, WildScreen& w);

} // namespace wild
