// Buying a dealer's machine (decomp src/o1/u_0357d7.c: dealer_buy_start, the hangar wheel
// dealer_garage_select_update, dealer_message_update and the boxes, the board
// question dealer_board_choice_update), ending in the Chip's name (name entry kind 2).
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/dealer.hpp"
#include "world/world.hpp"

namespace {

constexpr const char* dl = "dealer";

// dealer_machine_fits's count: body, legs and Chip present, every part a free slot
// decomp: dealer_machine_fits 0x39481
bool machine_fits(FrontEnd& fe, const DealerScreen& d, bool& inventory_short) {
    const ShopBuild* b = dealer_machine(fe, d, d.row);
    inventory_short = false;
    if (!b) {
        return false;
    }
    int n = (b->body != -1) + (b->leg != -1) + (b->chip != -1);
    for (const int wpn : b->weapon) {
        n += wpn != -1;
    }
    for (const int o : b->option) {
        n += o != -1;
    }
    int free = 0;
    for (const InventoryItem& item : fe.world->garages.inventory) {
        free += item.part == -1;
    }
    inventory_short = n > free;
    return b->body != -1 && b->leg != -1 && b->chip != -1 && !inventory_short;
}

} // namespace

// dealer_buy_start, one update: who may take it, then the wheel or a box
// decomp: dealer_buy_start 0x3A598
void dealer_buy_start(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    const Garages& g = fe.world->garages;
    d.garage = 0;
    d.garage_was = 0;
    d.slide = 8;
    bool inventory_short = false;
    const bool fits = machine_fits(fe, d, inventory_short);
    bool any = false;
    for (int i = 0; i < garage_count; ++i) {
        d.eligible[i] = (garage_status(fe.shop.tables, g, i) & 2) != 0 && fits;
        any = any || d.eligible[i];
    }
    const UiTable* rows = ui_table(ui.data, dl, "dealer_screen_rows");
    dealer_strip_init(ui, d.wheel_strip, rows && !rows->row_strings.empty() && !rows->row_strings[0].empty() ? rows->row_strings[0][0] : std::string());
    dealer_machine_menu_draw(ui, d);
    dealer_screen_draw(fe, ui, d, false);
    if (any && d.money >= dealer_price(fe, d, d.row)) {
        fe.screen.step = step_wheel;
        return;
    }
    // retail: no free slot at all 0, none eligible 1 ("You do not have an empty hangar", also
    // when the hangars are free but the inventory can't take every part), else 2; the port
    // gives the short inventory message 0
    if (inventory_first_free(g) < 0 || (!any && inventory_short)) {
        d.message = 0;
    } else {
        d.message = any ? 2 : 1;
    }
    fe.screen.step = step_message;
}

// the wheel of ten hangars
// decomp: dealer_garage_select_update 0x3A27E
void dealer_wheel_update(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    if (d.slide == 8) {
        d.garage_was = d.garage;
        int moved = menu_cursor(ui, d.garage, 0, 10, 0x17, 0, 4);
        if (moved == 0) {
            moved = menu_wheel(ui, d.garage, 0, 10, true, 0, 4);
        }
        // PC: a click on an arrow or the labels beside the cursor's turns the wheel a place
        const PointerRect sides[2] = {{190.0f, 205.0f, 279.0f, 262.0f},
                                      {343.0f, 205.0f, 450.0f, 262.0f}};
        const int side = moved == 0 && ui.pointer.left ? pointer_pick(ui.pointer, sides, 2) : -1;
        if (side >= 0) {
            d.garage = (d.garage + (side == 0 ? 9 : 1)) % 10;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            menu_input_mark(ui);
            ui.pointer.left = false;
            moved = 1;
        }
        if (moved != 0) {
            d.slide = 0;
        }
    } else {
        ++d.slide;
    }
    dealer_screen_draw(fe, ui, d, false);
    dealer_garage_list_draw(fe, ui, d, true);
    icon_on_plate_draw(ui, 0x150, 0x107, 0x105, 0xc3d, 0x17);
    text_strip_draw_scroll(ui, d.wheel_strip, 0xfff2e8e8, 0x101);
    // accept is read even while the wheel slides
    const PointerRect label = {279.0f, 210.0f, 343.0f, 258.0f};
    if (menu_accept(ui, &label)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        Garages& g = fe.world->garages;
        const int status = garage_status(fe.shop.tables, g, d.garage);
        if (d.eligible[d.garage]) {
            d.money -= dealer_price(fe, d, d.row);
            fe.world->story.money = d.money;
            ui_sound(ui, bank_sys, se_purchase);
            // machine_deliver: every part into a free slot fitted to the hangar, the Chip
            // item named from its record, the paint owned, the loadout named after the machine
            const ShopBuild* b = dealer_machine(fe, d, d.row);
            const int chip = b ? used_deliver(fe.shop.tables, g, *b, d.garage) : -1;
            if (chip >= 0) {
                InventoryItem& item = g.inventory[chip];
                for (std::int16_t& glyph : item.name) {
                    glyph = -1;
                }
                if (const ChipPart* part = chip_part(fe.shop.tables, b->chip)) {
                    for (int i = 0; i < 8 && i < static_cast<int>(part->name.glyphs.size()) && part->name.glyphs[static_cast<std::size_t>(i)] != -1; ++i) {
                        item.name[i] = part->name.glyphs[static_cast<std::size_t>(i)];
                    }
                }
            }
            d.bought = d.garage;
            d.message = 5;
        } else {
            d.message = (status & 1) != 0 ? 4 : 3;
        }
        fe.screen.step = step_message;
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = step_machine;
    }
}

// OK on a box, then where it leads
// decomp: dealer_message_update 0x3AB5A
void dealer_message_update(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    // retail draws the wheel under boxes 0..2 too, though it never opened: only after it did
    if (d.message >= 3) {
        dealer_garage_list_draw(fe, ui, d, false);
    }
    dealer_screen_draw(fe, ui, d, false);
    MsgBox& box = d.boxes[d.message];
    msg_box_update_ok(ui, box);
    if (!menu_accept(ui)) {
        return;
    }
    ui_sound(ui, bank_sys, se_menu_accept);
    msg_box_open(box);
    switch (d.message) {
    case 3:
    case 4:
        fe.screen.step = step_wheel;
        break;
    case 5:
        d.message = 7;
        d.pick = 1;
        fe.screen.step = step_board;
        break;
    case 6: {
        // name the Chip of the machine just delivered (name entry kind 2)
        Garages& g = fe.world->garages;
        const int chip = d.bought >= 0 ? g.garages[d.bought].chip : -1;
        fe.menu.dealer_return_menu = d.cursor;
        fe.menu.dealer_return_row = d.row;
        fe.menu.dealer_return_card_fade = d.card_fade;
        if (chip >= 0 && chip < inventory_size) {
            name_target_set(fe, 2, g.inventory[chip].name);
            dealer_finish(fe, ui, d, 2);
        } else {
            fe.screen.step = step_menu;
        }
        break;
    }
    case 8:
        d.message = 6;
        break;
    default:
        fe.screen.step = step_menu;
        break;
    }
}

// board the bought machine?
// decomp: dealer_board_choice_update 0x3AD04
void dealer_board_update(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    MsgBox& box = d.boxes[d.message];
    PointerRect answers[2];
    msg_box_choice_rects(ui, box, answers);
    menu_cursor(ui, d.pick, 0, 2, 0x13, 0, 4, box.row.done ? answers : nullptr);
    dealer_garage_list_draw(fe, ui, d, false);
    dealer_screen_draw(fe, ui, d, false);
    msg_box_update_choice(ui, box, d.pick);
    if (menu_accept(ui, box.row.done ? &answers[d.pick] : nullptr)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        msg_box_open(box);
        if (d.pick == 0) {
            fe.world->garages.selected = d.bought;
            d.message = 8;
        } else {
            d.message = 6;
        }
        fe.screen.step = step_message;
    } else if (menu_cancel(ui) && d.pick != 1) {
        d.pick = 1;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
    }
}

