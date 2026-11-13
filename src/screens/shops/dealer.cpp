// The dealers (dealer_* state 0x22, decomp src/o1/u_0357d7.c 0x3935E..0x3B11B, docs/port/
// screens.md "Dealer"): Aeron, Photon and Holy, each with the day's three machines on the stand,
// the maker's profile, the spec page, buying into an empty hangar for the total with tax, boarding
// it, and naming its Chip (name entry kind 2). Leave goes back to the town map.
#include "screens/shops/dealer.hpp"

#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr const char* dl = "dealer";


int cad(const Ui& ui, float f) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * f);
}

// dealer_stock (tables/shop_stock.sexp "dealer"): 65 rows of three {loadout, display}
struct DealerOffer {
    int loadout = 0, display = 0;
};
// decomp: dealer_stock 0x24FB30
const std::vector<std::array<DealerOffer, 3>>& dealer_stock(Ui& ui) {
    static std::vector<std::array<DealerOffer, 3>> rows;
    if (!rows.empty()) {
        return rows;
    }
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(ui.data, "tables/shop_stock.sexp", parsed, error)) {
        std::fprintf(stderr, "dealer: %s\n", error.c_str());
        return rows;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("dealer")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0) {
            continue;
        }
        if (static_cast<int>(rows.size()) <= id) {
            rows.resize(static_cast<std::size_t>(id + 1));
        }
        int k = 0;
        for (const gsexp::Node child : form.node().children()) {
            if (k < 3 && child.is_list() && child.head().is_atom("offer") && child.child_count() >= 3) {
                DealerOffer& o = rows[static_cast<std::size_t>(id)][static_cast<std::size_t>(k++)];
                o.loadout = std::atoi(std::string(child.child_at(1).text()).c_str());
                o.display = std::atoi(std::string(child.child_at(2).text()).c_str());
            }
        }
    }
    return rows;
}

} // namespace

const ShopBuild* dealer_machine(FrontEnd& fe, const DealerScreen& d, int i) {
    const auto& builds = fe.shop.tables.builds;
    if (i < 0 || i > 2 || builds.empty()) {
        return nullptr;
    }
    // rumble_scoobee_loadout_at clamps: a row out of range reads row 0
    const int row = d.machines[i];
    return &builds[row >= 0 && row < static_cast<int>(builds.size()) ? static_cast<std::size_t>(row)
                                                                      : 0];
}

std::vector<std::string> strings(Ui& ui, const char* table, int count) {
    return ui_strings(ui.data, dl, table, 0, count);
}

void dealer_strip_init(Ui& ui, TextStrip& strip, const std::string& text) {
    text_strip_init(ui, strip, 0x118, 0x2d, text, 0x118, 0x2d, 0x26e, 0x3f);
}

namespace {

// the menu buttons' hit boxes at their places
void button_rects(Ui& ui, const DealerScreen& d, PointerRect out[5]) {
    const std::vector<double> place = ui_values(ui.data, dl, "dealer_menu_button_x", 10);
    for (int i = 0; i < 5; ++i) {
        out[i] = sprite_rect(ui, d.buttons[i][1], static_cast<float>(place[static_cast<std::size_t>(i * 2)]),
                             static_cast<float>(place[static_cast<std::size_t>(i * 2 + 1)]));
    }
}

// the entry (dealer_screen_update)
// decomp: dealer_screen_update 0x3AEC3
void load(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    World& w = *fe.world;
    d.dealer = fe.menu.dealer_index;
    d.money = w.story.money;
    texture_set_load(ui, 0x690);
    texture_set_load(ui, 0x66d);
    const UiTable* rows = ui_table(ui.data, dl, "dealer_screen_rows");
    const bool have_rows = rows && !rows->rows.empty() && rows->rows[0].size() > 12;
    if (have_rows) {
        texture_set_load(ui, static_cast<int>(rows->rows[0][static_cast<std::size_t>(1 + d.dealer)]));
    }
    level_meter_spawn(ui, fe.screen.meter);
    stand_start(ui, fe.shop.stand, 2);
    // the day's three machines: day_schedule[day % 60].dealer[dealer] picks the stock row
    const auto& stock = dealer_stock(ui);
    const int row = day_row(fe.story_tables, w.story.day).dealer[d.dealer];
    const std::vector<double> flat = ui_values(ui.data, dl, "dealer_leg_buttons", 134);
    for (int i = 0; i < 3; ++i) {
        const DealerOffer o = row >= 0 && row < static_cast<int>(stock.size())
                                  ? stock[static_cast<std::size_t>(row)][static_cast<std::size_t>(i)]
                                  : DealerOffer{};
        d.machines[i] = o.loadout;
        const ShopBuild* b = dealer_machine(fe, d, i);
        const int leg = b && b->leg >= 0 ? b->leg : 0;
        const auto value = [&](int k) {
            return k >= 0 && k < static_cast<int>(flat.size()) ? static_cast<int>(flat[static_cast<std::size_t>(k)]) : -1;
        };
        // dealer_leg_buttons by leg (lit, plain); dealer_machine_card_sets[dealer][display]
        d.buttons[i + 1][0] = value(leg * 2);
        d.buttons[i + 1][1] = value(leg * 2 + 1);
        d.cards[i] = value(86 + d.dealer * 16 + o.display);
    }
    // the three onto the stand's line-up, each loaded in turn and shown under the cursor
    ShopBuild line[3];
    for (int i = 0; i < 3; ++i) {
        if (const ShopBuild* b = dealer_machine(fe, d, i)) {
            line[i] = *b;
        }
    }
    stand_line_set(fe.shop.stand, line, 3);
    d.buttons[0][0] = table_int(ui, dl, "dealer_menu_buttons", 0, 1209);
    d.buttons[0][1] = table_int(ui, dl, "dealer_menu_buttons_plain", 0, 1208);
    d.buttons[4][0] = table_int(ui, dl, "dealer_machine_button_slots", 5, 1211);
    d.buttons[4][1] = table_int(ui, dl, "dealer_machine_button_slots", 6, 1210);
    for (int i = 0; i < 9; ++i) {
        d.boxes[i] = msg_box_from(ui, dl, "dealer_boxes", i);
        d.boxes[i].row.style = d.dealer + 1;
    }
    d.scroll = 0.0f;
    d.card_fade = 1.0f;
    d.frame = 0;
    d.wait = 0;
    if (d.from == app_spec_scoobee || d.from == app_name_entry) {
        d.cursor = fe.menu.dealer_return_menu;
        d.row = fe.menu.dealer_return_row;
        for (float& t : d.trail) {
            t = 0.0f;
        }
        d.intro = 1.0f;
        d.card_fade = fe.menu.dealer_return_card_fade;
        d.plate = 1.0f;
    } else {
        d.row = -1;
        d.cursor = 0;
        for (float& t : d.trail) {
            t = 1.0f;
        }
        d.intro = 0.0f;
        d.plate = 0.0f;
    }
    d.item_before = d.cursor;
    const std::vector<std::string> help = strings(ui, "dealer_menu_help_lines", 5);
    for (int i = 0; i < 5; ++i) {
        dealer_strip_init(ui, d.strips[i], i < static_cast<int>(help.size()) ? help[static_cast<std::size_t>(i)] : std::string());
    }
    state_music_play(fe, ui, 0x22);
    now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
    d.fade = cad(ui, 0.5f);
    fade_in_start(ui, static_cast<float>(d.fade));
    if (d.from == app_town_map) {
        ui_sound(ui, bank_sys, se_shop_arrive);
    }
}

} // namespace

// leave waits on its plate; the rest fade out
// decomp: dealer_screen_finish 0x3A6BA
void dealer_finish(FrontEnd& fe, Ui& ui, DealerScreen& d, int result) {
    d.result = result;
    if (result == 0) {
        fe.screen.step = step_leave_wait;
        return;
    }
    d.fade = cad(ui, 0.5f);
    fade_out_start(ui, static_cast<float>(d.fade));
    fe.screen.step = step_leave;
}

namespace {

void machine_menu_enter(Ui& ui, DealerScreen& d) {
    const std::vector<std::string> help = strings(ui, "dealer_machine_menu_help_lines", 3);
    for (int i = 0; i < 3; ++i) {
        dealer_strip_init(ui, d.sub_strips[i], i < static_cast<int>(help.size()) ? help[static_cast<std::size_t>(i)] : std::string());
    }
    d.sub = 0;
}

// decomp: dealer_menu_update 0x3A910
void menu_update(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    d.cursor_was = d.cursor;
    PointerRect rects[5];
    button_rects(ui, d, rects);
    const bool intro_done = !(d.plate < 1.0f) && !(d.intro < 1.0f);
    if (d.plate < 1.0f) {
        d.plate = d.plate + 0.05f > 1.0f ? 1.0f : d.plate + 0.05f;
    } else if (d.intro < 1.0f) {
        d.intro = d.intro + 0.025f > 1.0f ? 1.0f : d.intro + 0.025f;
    } else {
        menu_cursor(ui, d.cursor, 0, 5, 0x17, 0, 3, rects);
        if (d.cursor != d.cursor_was) {
            d.card_fade = 0.0f;
            d.item_before = d.cursor_was;
            const std::vector<std::string> help = strings(ui, "dealer_menu_help_lines", 5);
            if (d.cursor == 0 || d.cursor == 4) {
                dealer_strip_init(ui, d.strips[d.cursor], help[static_cast<std::size_t>(d.cursor)]);
            } else if (d.cursor_was == 0 || d.cursor_was == 4) {
                dealer_strip_init(ui, d.strips[1], help[1]);
            }
        }
    }
    if (d.pop > 0) {
        --d.pop;
    }
    dealer_screen_draw(fe, ui, d, true);
    if (!intro_done) {
        // dealer_menu_fade_draw: the menu plate fading out as the buttons arrive
        Sprite s = sprite_of(ui, 0x4c0);
        s.pos[0] = 115.0f;
        s.pos[1] = 178.0f;
        s.blend = 0x104;
        s.color = static_cast<std::uint32_t>(static_cast<int>((1.0f - d.intro) * d.plate * 255.0f))
                      << 24 |
                  0xffffffu;
        sprite_submit(ui, s);
    }
    const bool machine_item = d.cursor >= 1 && d.cursor <= 3;
    text_strip_draw_scroll(ui, d.strips[machine_item ? 1 : d.cursor], 0xfff2e8e8, 0x101);
    d.row = machine_item ? d.cursor - 1 : -1;
    const bool accept = menu_accept(ui, &rects[d.cursor]);
    const bool cancel = !accept && menu_cancel(ui);
    if ((accept || cancel) && (d.intro < 1.0f || d.plate < 1.0f)) {
        // accept or cancel during the intro skips it
        d.intro = d.plate = 1.0f;
        return;
    }
    if (accept) {
        switch (d.cursor) {
        case 0:
            ui_sound(ui, bank_sys, se_menu_accept);
            fe.screen.step = step_talk_start;
            break;
        case 4:
            ui_sound(ui, bank_sys, se_shop_leave);
            dealer_finish(fe, ui, d, 0);
            break;
        default:
            ui_sound(ui, bank_sys, se_menu_accept);
            machine_menu_enter(ui, d);
            fe.screen.step = step_machine;
            break;
        }
    } else if (cancel && d.cursor != 4) {
        // retail sets the cursor after the change test, so neither the leave strip nor the
        // card fade restarts; the port does what a move onto leave does
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        d.card_fade = 0.0f;
        d.item_before = d.cursor;
        d.cursor = 4;
        dealer_strip_init(ui, d.strips[4], strings(ui, "dealer_menu_help_lines", 5)[4]);
    }
}

// specs, buy, back
// decomp: dealer_machine_menu_update 0x3A709
void machine_update(FrontEnd& fe, Ui& ui, DealerScreen& d) {
    PointerRect rects[3];
    for (int i = 0; i < 3; ++i) {
        const SpriteSlot r = slot_at(ui, dl, "dealer_machine_menu_sprites", i);
        rects[i] = sprite_rect(ui, r.sprite, r.x, r.y);
    }
    if (menu_cursor(ui, d.sub, 0, 3, 0x17, 0, 4, rects) != 0) {
        dealer_strip_init(ui, d.sub_strips[d.sub], strings(ui, "dealer_machine_menu_help_lines", 3)[static_cast<std::size_t>(d.sub)]);
    }
    dealer_machine_menu_draw(ui, d);
    dealer_screen_draw(fe, ui, d, false);
    text_strip_draw_scroll(ui, d.sub_strips[d.sub], 0xfff2e8e8, 0x101);
    if (menu_accept(ui, &rects[d.sub])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        switch (d.sub) {
        case 0: { // the spec page: the machine, its name, the Chip's name
            fe.menu.dealer_return_menu = d.cursor;
            fe.menu.dealer_return_row = d.row;
            fe.menu.dealer_return_card_fade = d.card_fade;
            if (const ShopBuild* b = dealer_machine(fe, d, d.row)) {
                fe.shop.spec = *b;
                for (int i = 0; i < glyph_name_length; ++i) {
                    fe.shop.spec_name[i] = i < static_cast<int>(b->name.glyphs.size()) && i < 8 ? b->name.glyphs[static_cast<std::size_t>(i)] : -1;
                    fe.shop.spec_chip_name[i] = -1;
                }
                if (const ChipPart* chip = chip_part(fe.shop.tables, b->chip)) {
                    for (int i = 0; i < 8 && i < static_cast<int>(chip->name.glyphs.size()) && chip->name.glyphs[static_cast<std::size_t>(i)] != -1; ++i) {
                        fe.shop.spec_chip_name[i] = chip->name.glyphs[static_cast<std::size_t>(i)];
                    }
                }
            }
            dealer_finish(fe, ui, d, 1);
            break;
        }
        case 1:
            fe.screen.step = step_buy;
            break;
        default:
            fe.screen.step = step_menu;
            break;
        }
    } else if (menu_cancel(ui) && d.sub != 2) {
        d.sub = 2;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
    }
}

} // namespace

void dealer_spawn(FrontEnd& fe, Ui&) {
    fe.screen.dealer = DealerScreen{};
    fe.screen.dealer.from = fe.previous;
}

void dealer_step(FrontEnd& fe, Ui& ui) {
    DealerScreen& d = fe.screen.dealer;
    // the stand (dealer_shown_machine_update, dealer_model_slot_pick): the
    // cursor's machine slides in once loaded, the others out to the side
    if (fe.screen.step != step_load) {
        // PC: the grab kept clear of the submenu and the hangar wheel, none under a box
        const int step = fe.screen.step;
        stand_area_set(fe.shop.stand, step == step_machine || step == step_wheel
                                          ? PointerRect{460, 96, 640, 330}
                                      : step == step_message || step == step_board
                                          ? PointerRect{}
                                          : PointerRect{340, 96, 640, 330});
        stand_line_follow(ui, fe.shop.stand, d.row);
        stand_update(ui, fe.shop.stand);
    }
    switch (fe.screen.step) {
    case step_load:
        load(fe, ui, d);
        fe.screen.step = step_fade_in;
        break;
    case step_fade_in: // dealer_screen_fade_in
        if (d.from == app_spec_scoobee) {
            dealer_machine_menu_draw(ui, d);
        }
        dealer_screen_draw(fe, ui, d, false);
        if (--d.fade < 0) {
            if (d.from == app_spec_scoobee) {
                machine_menu_enter(ui, d);
                fe.screen.step = step_machine;
            } else {
                fe.screen.step = d.from == app_name_entry ? step_menu : step_intro;
            }
        }
        break;
    case step_intro: { // dealer_intro_wait: a second of nothing
        dealer_screen_draw(fe, ui, d, false);
        Sprite s = sprite_of(ui, 0x4c0);
        s.pos[0] = 115.0f;
        s.pos[1] = 178.0f;
        s.blend = 0x104;
        s.color = 0x00ffffff;
        sprite_submit(ui, s);
        if (d.wait > 60) {
            d.wait = 0;
            ui_sound(ui, bank_sys, se_panel_slide_in);
            fe.screen.step = step_menu;
        } else {
            ++d.wait;
        }
        break;
    }
    case step_menu:
        menu_update(fe, ui, d);
        break;
    case step_talk_start: // dealer_talk_start: the panel pops in, then the profile
        dealer_screen_draw(fe, ui, d, true);
        if (d.pop == 10) {
            const UiTable* rows = ui_table(ui.data, dl, "dealer_screen_rows");
            char name[32];
            const int talk = rows && !rows->rows.empty() && rows->rows[0].size() > 9
                                 ? static_cast<int>(rows->rows[0][static_cast<std::size_t>(7 + d.dealer)])
                                 : 0;
            std::snprintf(name, sizeof name, "maker_%x", talk);
            d.talk_alive = script_start(fe, ui, d.talk_script, d.talk, name);
            fe.screen.step = step_talk;
        } else {
            ++d.pop;
        }
        break;
    case step_talk: // dealer_talk_update: B goes back
        dealer_screen_draw(fe, ui, d, false);
        icon_on_plate_draw(ui, 0x19e, 0x13d, 0x103, 0xc3e, 0x10);
        if (d.talk_alive) {
            script_step(fe, ui, d.talk_script, d.talk);
        }
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            d.talk_alive = false;
            fe.screen.step = step_menu;
        }
        break;
    case step_machine:
        machine_update(fe, ui, d);
        break;
    case step_buy:
        dealer_buy_start(fe, ui, d);
        break;
    case step_wheel:
        dealer_wheel_update(fe, ui, d);
        break;
    case step_message:
        dealer_message_update(fe, ui, d);
        break;
    case step_board:
        dealer_board_update(fe, ui, d);
        break;
    case step_leave_wait: // dealer_leave_wait: the leave plate, the buttons go
        dealer_screen_draw(fe, ui, d, false);
        sprite_draw_at(ui, 0x4bf, 160.0f, 174.0f, 0x104);
        if (d.wait > 0x3c) {
            d.wait = 0;
            d.fade = cad(ui, 0.5f);
            fade_out_start(ui, static_cast<float>(d.fade));
            fe.screen.step = step_leave;
        } else {
            ++d.wait;
            if (d.intro > 0.0f) {
                d.intro = d.intro - 0.025f < 0.0f ? 0.0f : d.intro - 0.025f;
            } else if (d.plate > 0.0f) {
                d.plate = d.plate - 0.05f < 0.0f ? 0.0f : d.plate - 0.05f;
            }
        }
        break;
    case step_leave: // dealer_screen_leave
        dealer_screen_draw(fe, ui, d, false);
        if (d.result == 1) {
            dealer_machine_menu_draw(ui, d);
        } else if (d.result == 0) {
            sprite_draw_at(ui, 0x4bf, 160.0f, 174.0f, 0x104);
        }
        if (--d.fade < 0) {
            fe.screen.strip.active = false;
            ui_music_stop(ui);
            task_header_set(fe, d.result);
            fe.screen.step = step_done;
        }
        break;
    default:
        dealer_screen_draw(fe, ui, d, false);
        break;
    }
    if (fe.screen.step != step_load) {
        stand_draw(ui, fe.shop.stand);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

void dealer_destroy(FrontEnd& fe, Ui& ui) {
    // dealer_screen_destroy: the stream and the stand stop, clear_color black
    ui_music_stop(ui);
    stand_hide(fe.shop.stand);
    ui.clear_colour = 0xff000000;
}

// decomp: dealer_index_set 0x3935E
void dealer_index_set(FrontEnd& fe, int spot) {
    // town_map_leave: spot 7 "American Stars", 6 "Kojima", 5 "Ventuno"
    fe.menu.dealer_index = spot == 7 ? 0 : spot == 6 ? 1 : 2;
}
