#pragma once
// The hangar (hangar_* state 0xE, decomp src/o1/u_01c789.c 0x1D05F..0x1ED2D; docs/port/screens.md
// "The garage group"): a wheel of the ten garages and exit, each a card with its machine standing
// over it, and the garage's menu (Ride, Test-drive, Specs, Assemble, Close), the unlock for 10,000
// (100,000 NY) and its boxes. Keep SV (0x2B) opens it in its own mode to pick a VS machine.
// hangar.cpp the steps, hangar_draw.cpp the screen.
#include "screens/garage/model_stand.hpp"
#include "screens/ui/screen_kit.hpp"
#include "screens/ui/screen_text.hpp"

struct FrontEnd;
struct Ui;

// GarageMenu (0x3918 bytes) by field
struct HangarScreen {
    int mode = 0;        // +0x00 the state it came from: 3 area select, 0x2B keep SV, else the hub
    int timer = 0;       // +0x04 fade counts
    int sel = 0;         // +0x0C the wheel's garage, 10 the exit
    int prev = 0;        // +0x10
    int kind = 0;        // +0x14 the menu's (or the unlock box's) cursor
    Slide slide;         // +0x20 the garages' name plates sliding in
    int banner = -1;     // +0x4C the boarded garage's banner timer
    int next = 0;        // +0x50 the answer on leaving
    int flags[11] = {};  // +0x54 garage_status per garage, the exit's 1
    int values[10] = {}; // +0x80 Loadout::common_class
    int turn = 8;        // +0xA8 updates into the wheel's turn (8 at rest)
    float angles[11] = {}; // +0xAC each card's x
    float turn_from = 0;   // +0xD8 where the new card stood when the turn started
    int boarded = -1;      // +0x2F8 the boarded garage
    TextStrip help[11];    // +0x2FC the lines of the help list up
    PopTimer card;         // +0x38EC the garage's menu card
    MsgBox boxes[8];       // hangar_boxes
    int box = -1;          // the box up in the box step
    int box_after = 0;     // the step its accept goes to
    PointerRect plates[11], rows[5];
    // the machines on the wheel (the model viewer's six slots: loaded at once here)
    const MechParts* mech[10] = {};
    bool loaded[10] = {};
    bool complete[10] = {};   // the build stands whole, else its parts are laid out
    bool present[10][6] = {}; // the parts it has
    float heading[10] = {};
    float spin[10] = {}; // turn_deg: 0.25 for the garage under the cursor
};

void hangar_spawn(FrontEnd& fe, Ui& ui);
void hangar_step(FrontEnd& fe, Ui& ui);
void hangar_destroy(FrontEnd& fe, Ui& ui);

// hangar_draw.cpp
// hangar_wheel_angle: the card's offset from the rest place, wrapped
float hangar_wheel_angle(const HangarScreen& h);
// hangar_screen_draw (highlight: the cursor's plate blinks)
void hangar_screen_draw(FrontEnd& fe, Ui& ui, HangarScreen& h, bool highlight);
// hangar_garage_card_draw
void hangar_card_draw(Ui& ui, HangarScreen& h);
