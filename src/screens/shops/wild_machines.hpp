#pragma once
// Wild Machines' pieces shared between its steps (wild_machines.cpp, wild_steps.cpp) and its
// drawing (wild_draw.cpp).
#include "screens/flow/shop_screens.hpp"

struct FrontEnd;
struct World;

struct CheatRules;

// a price shown and paid: the price at the tune, half when selling (not the Gold Bar, option 0x2D);
// SHOP PRICES (PC CONFIG's CHEATS) divides it, bought or sold
int wild_price(const PartTables& t, const CheatRules& rules, int kind, int id, int tune,
               bool selling);
// garage_color_in_use / garage_sticker_in_use: any of the ten garages wears it
bool wild_paint_in_use(const World& world, int kind, int id);

void wild_menu_draw(FrontEnd& fe, Ui& ui, WildScreen& w);
int wild_menu_item(Ui& ui, const WildScreen& w); // the wheel's sprite of the cursor's item
void wild_parts_frame_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool tabs);
void wild_parts_monitor_draw(Ui& ui, WildScreen& w, bool selling, bool advance);
// listing: the list step's pulse and A-plate; bar: the cursor bar (not under the sold box)
void wild_parts_list_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool listing,
                          bool bar);
void wild_used_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool listing);
void wild_submenu_draw(Ui& ui, WildScreen& w);
void wild_garage_wheel_draw(FrontEnd& fe, Ui& ui, WildScreen& w);
void wild_chip_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool input, bool selling);
void wild_card_confirm_draw(Ui& ui, const WildScreen& w, bool selling);

// PC: the pointer's hit boxes. The tabs by cell (column * 2 + row); the list's four rows and its
// arrows; the used Scoobees' plates by cursor (count + 1 with the leave row; returns how many);
// the submenu's three rows; the garage wheel's five marks, k = -2..2 round the cursor's
void wild_tab_rects(Ui& ui, const WildScreen& w, PointerRect (&rects)[8]);
void wild_list_rects(Ui& ui, bool selling, PointerRect (&rows)[4], PointerRect& up,
                     PointerRect& down);
int wild_used_rects(Ui& ui, const WildScreen& w, PointerRect (&rects)[7]);
void wild_submenu_rects(Ui& ui, PointerRect (&rects)[3]);
void wild_garage_rects(Ui& ui, PointerRect (&rects)[5]);
