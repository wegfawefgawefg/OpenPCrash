// Plus Tech's Chip Uplift view (decomp src/o1/u_02edc5.c: plus_tech_chip_list_draw,
// plus_tech_chip_card_draw, plus_tech_screen_step7, _step10, plus_tech_chip_list_update,
// plus_tech_chip_confirm_update, plus_tech_screen_step11) and the centred-card carousel
// Wild Machines' Chip pages copy.
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/garage/chip_carousel.hpp"
#include "world/world.hpp"

namespace {

// the Plus Tech carousel's sprites (plus_tech_chip_list_draw)
const CarouselLook plus_tech_look = {150, false, 0xaa1, 0xa9d, 0xa9a, 0xa95, 0xaa5,
                                     0xaa3, 0xa94, 0xaa4, 0xaac, 121, 282, 0xa91, 0xa92, 436, 323};

std::vector<CarouselCard> cards_of(FrontEnd& fe, const PlusTechScreen& p) {
    std::vector<CarouselCard> cards;
    for (const GarageEntry& e : p.editor.pages[4].entries) {
        const InventoryItem& item = fe.world->garages.inventory[e.item];
        CarouselCard card;
        card.part = e.id;
        card.tune = e.tune;
        card.item = e.item;
        card.name = item.name[0] != -1 ? glyph_name_text(item.name, glyph_name_length)
                                       : (e.name ? e.name->glyphs : GlyphText{-1});
        card.price = shop_item_price(fe.shop.tables, 4, e.id, e.tune);
        cards.push_back(card);
    }
    return cards;
}

void chip_line(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    const ChipStrip& c = p.chips;
    const std::vector<GarageEntry>& entries = p.editor.pages[4].entries;
    if (c.on_leave || entries.empty()) {
        help_strip_init(ui, p.help,
                        ui_strings(ui.data, "plus_tech", "plus_tech_chip_list_help", 0, 1)[0]);
    } else {
        const int id = entries[static_cast<std::size_t>(c.cursor)].id;
        help_strip_init(ui, p.help,
                        ui_strings(ui.data, "plus_tech", "plus_tech_chip_help", id, 1)[0]);
    }
    (void)fe;
}

void enter_uplift(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    const GarageEntry& e = p.editor.pages[4].entries[static_cast<std::size_t>(p.chips.cursor)];
    p.chip_tune = p.chip_tune_was = e.tune;
    p.chips.arrows_flip = false;
    p.chips.card_frames[0] = p.chips.card_frames[1] = p.chips.card_frames[2] = 0;
    help_strip_init(ui, p.help, ui_strings(ui.data, "plus_tech", "chip_level_help", 0, 1)[0]);
    (void)fe;
}

} // namespace

void plus_tech_chip_setup(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    ChipStrip& c = p.chips;
    c = ChipStrip{};
    garage_pages_build(fe.shop.tables, fe.world->garages, p.editor.pages);
    garage_pages_mark_fitted(fe.world->garages, p.editor.pages);
    chip_line(fe, ui, p);
}

// decomp: plus_tech_chip_list_draw 0x2F690
void plus_tech_chip_list_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool input) {
    garage_banner_draw(ui, *fe.world, p.banner, 378, 4);
    sprite_rows_draw(ui, ui_slots(ui.data, "plus_tech", "plus_tech_chip_list_sprites"));
    sprite_draw_at(ui, 0xe38, 39, 316, 3);
    sprite_draw_at(ui, 0xe39, 39, 344, 3);
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    const std::vector<CarouselCard> cards = cards_of(fe, p);
    carousel_draw(ui, fe.shop.tables, plus_tech_look, p.chips, cards, input,
                  p.chips.arrows_flip);
    text_strip_draw_scroll(ui, p.help, 0xffc0c0c0);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// decomp: plus_tech_chip_card_draw 0x30B5B
void plus_tech_chip_card_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    const GarageEntry& e = p.editor.pages[4].entries[static_cast<std::size_t>(p.chips.cursor)];
    const int cost = shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune) -
                     shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune_was);
    uplift_card_draw(ui, fe.shop.tables, p.chips, e.id, p.chip_tune_was, p.chip_tune_was,
                     p.chip_tune, cost, fe.world->story.money, true);
}

int plus_tech_chip_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p, int step) {
    ChipStrip& c = p.chips;
    World& w = *fe.world;
    const int count = static_cast<int>(p.editor.pages[4].entries.size());
    switch (step) {
    case 0: { // step10: the carousel
        const int moved = carousel_input(ui, c, count, false);
        if (moved != 0) {
            chip_line(fe, ui, p);
        }
        plus_tech_chip_list_draw(fe, ui, p, true);
        if (c.flash[3] != 0 || c.slide < 8) {
            return -1;
        }
        const PointerRect card = {246, 136, 396, 286};
        const PointerRect leave = sprite_rect(ui, 0xa91, 436, 323);
        if (menu_accept(ui, c.on_leave ? &leave : &card)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            if (c.on_leave) {
                return 100;
            }
            const GarageEntry& e = p.editor.pages[4].entries[static_cast<std::size_t>(c.cursor)];
            if (e.tune >= 99) {
                p.message = 9;
                msg_box_open(p.boxes[9]);
                p.pick = 0;
                return 2;
            }
            enter_uplift(fe, ui, p);
            return 1;
        }
        if (menu_cancel(ui) && !c.on_leave) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            c.on_leave = true;
            chip_line(fe, ui, p);
        }
        return -1;
    }
    case 1: { // plus_tech_chip_list_update: the level, up only
        menu_cursor(ui, p.chip_tune, p.chip_tune_was, 100 - p.chip_tune_was, 0x13, 0, 4);
        if (ui.pointer.wheel != 0) {
            const int to = std::min(99, std::max(p.chip_tune_was, p.chip_tune + ui.pointer.wheel));
            if (to != p.chip_tune) {
                p.chip_tune = to;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                menu_input_mark(ui);
            }
        }
        const int limits[3] = {10, 10, 6}; // D_001FB230..238
        for (int i = 0; i < 3; ++i) {
            if (c.card_frames[i] < limits[i]) {
                ++c.card_frames[i];
                break;
            }
        }
        plus_tech_chip_list_draw(fe, ui, p, false);
        plus_tech_chip_card_draw(fe, ui, p);
        const PointerRect card = {104, 110, 536, 380};
        if (menu_accept(ui, &card)) {
            if (p.chip_tune == p.chip_tune_was) {
                return -1;
            }
            const GarageEntry& e = p.editor.pages[4].entries[static_cast<std::size_t>(c.cursor)];
            const int cost = shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune) -
                             shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune_was);
            ui_sound(ui, bank_sys, se_menu_accept);
            if (w.story.money < cost) {
                p.message = 5;
                msg_box_open(p.boxes[5]);
                return 3;
            }
            p.message = 6;
            msg_box_open(p.boxes[6]);
            p.pick = 1;
            return 2;
        }
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            c.arrows_flip = true;
            chip_line(fe, ui, p);
            return 0;
        }
        return -1;
    }
    case 2: { // plus_tech_chip_confirm_update
        MsgBox& box = p.boxes[p.message];
        PointerRect answers[2];
        msg_box_choice_rects(ui, box, answers);
        menu_cursor(ui, p.pick, 0, 2, 0x13, 0, 4, box.row.done ? answers : nullptr);
        plus_tech_chip_list_draw(fe, ui, p, false);
        if (p.message == 6) {
            plus_tech_chip_card_draw(fe, ui, p);
        }
        msg_box_update_choice(ui, box, p.pick);
        if (menu_accept(ui, &answers[p.pick])) {
            msg_box_open(box);
            if (p.message == 6) {
                if (p.pick != 0) {
                    return 1;
                }
                const GarageEntry& e =
                    p.editor.pages[4].entries[static_cast<std::size_t>(c.cursor)];
                const int cost = shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune) -
                                 shop_item_price(fe.shop.tables, 4, e.id, p.chip_tune_was);
                ui_sound(ui, bank_sys, se_purchase);
                money_add(w.story, -cost);
                w.garages.inventory[e.item].tune = static_cast<std::int8_t>(p.chip_tune);
                p.message = 7;
                msg_box_open(p.boxes[7]);
                return 3;
            }
            // message 9: the parameters only, or back to the carousel
            if (p.pick == 0) {
                enter_uplift(fe, ui, p);
                return 1;
            }
            c.arrows_flip = true;
            return 0;
        }
        if (menu_cancel(ui) && p.pick != 1) {
            p.pick = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        return -1;
    }
    default: { // step11: the OK boxes
        MsgBox& box = p.boxes[p.message];
        plus_tech_chip_list_draw(fe, ui, p, false);
        msg_box_update_ok(ui, box);
        const PointerRect ok = msg_box_ok_rect(ui, box);
        if (menu_accept(ui, &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            msg_box_open(box);
            if (p.message == 8) {
                return 100;
            }
            // 5 (no money) and 7 (done): the carousel starts again from the first card
            plus_tech_chip_setup(fe, ui, p);
            return 0;
        }
        return -1;
    }
    }
}
