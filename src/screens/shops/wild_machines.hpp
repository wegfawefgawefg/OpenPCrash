#pragma once
// Wild Machines' pieces shared between its steps (wild_machines.cpp, wild_steps.cpp) and its
// drawing (wild_draw.cpp).
#include "screens/flow/shop_screens.hpp"

struct FrontEnd;
struct World;

// a price shown and paid: the price at the tune, half when selling (not the Gold Bar, option 0x2D)
int wild_price(const PartTables& t, int kind, int id, int tune, bool selling);
// garage_color_in_use / garage_sticker_in_use: any of the ten garages wears it
bool wild_paint_in_use(const World& world, int kind, int id);

void wild_menu_draw(FrontEnd& fe, Ui& ui, WildScreen& w);
PointerRect wild_menu_rect(Ui& ui, const WildScreen& w);
void wild_parts_frame_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool tabs);
void wild_parts_list_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool listing);
void wild_used_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool listing);
void wild_submenu_draw(Ui& ui, WildScreen& w);
void wild_garage_wheel_draw(FrontEnd& fe, Ui& ui, WildScreen& w);
void wild_chip_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool input, bool selling);
void wild_card_confirm_draw(Ui& ui, const WildScreen& w, bool selling);
