#pragma once
// The centred-card Chip carousel of Plus Tech's Uplift view (plus_tech_chip_list_draw)
// and the copies Wild Machines' Chip pages paste from it (wild_machines_chip_buy_draw,
// wild_machines_chip_sell_draw), with the Uplift card's stat panels (plus_tech_chip_card_draw and
// its Wild Machines copies).
#include "screens/flow/shop_screens.hpp"

// the sprites and spacing one copy uses
struct CarouselLook {
    int spacing = 150;
    bool ring = false; // Wild Machines' buy page: seven cards round a ring, arrows always
    int plate, frame, highlight, arrow_left, arrow_right, belt, deco_a, deco_b, black;
    int black_x, black_y;
    int leave, leave_lit, leave_x, leave_y;
};

struct CarouselCard {
    int part = -1, tune = 0, item = -1;
    GlyphText name;
    int price = 0; // shown on the centre card
};

// draws the frame, belt, cards, the centre card's name, price, level and weight, the index and
// count, and steps the flash and pop counters
void carousel_draw(Ui& ui, const PartTables& t, const CarouselLook& look, ChipStrip& c,
                   const std::vector<CarouselCard>& cards, bool input, bool pulse);
// the cursor (0x13 stopping at the ends, or 0x17 wrapping round the ring), the slide and the
// leave line (0x12); the pointer clicks a side card to step towards it. Returns the card step.
int carousel_input(Ui& ui, ChipStrip& c, int count, bool ring);
// the Uplift card: the Chip at its level on the left, the new level's reticle boxes, crit and
// shield on the right, the gauge, the levels and the cost
void uplift_card_draw(Ui& ui, const PartTables& t, const ChipStrip& c, int part, int tune_left,
                      int tune_from, int tune_to, int cost, int money, bool selected);
