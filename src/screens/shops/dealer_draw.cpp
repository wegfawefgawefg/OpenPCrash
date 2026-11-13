// The dealer's drawing (decomp src/o1/u_0357d7.c: dealer_screen_draw with the logo trail
// dealer_logo_trail_step, the machine card and buttons dealer_machine_card_draw, the price
// dealer_price_draw, the profile panel dealer_profile_panel_draw in src/hand/hud_draw_o1.c, the
// machine submenu dealer_machine_menu_draw, the garage wheel dealer_garage_list_draw).
#include "screens/flow/front_end.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <cstdlib>

namespace {

constexpr const char* dl = "dealer";

std::uint32_t alpha_white(float f) {
    return static_cast<std::uint32_t>(static_cast<int>(f * 255.0f)) << 24 | 0xffffffu;
}

int table_at(Ui& ui, const char* table, int i) {
    return table_int(ui, dl, table, i, -1);
}

Sprite at(Ui& ui, int id, float x, float y, int blend) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.blend = blend;
    return s;
}

// the history shifts each update while the trail moves, its
// head 0.125 towards 0 with a machine under the cursor, towards 1 without
// decomp: dealer_logo_trail_step 0x3936E
void trail_step(DealerScreen& d) {
    const bool machine = d.row != -1;
    if ((machine && d.trail[0] >= 0.0f) || (!machine && d.trail[0] <= 1.0f)) {
        for (int i = 14; i > 0; --i) {
            d.trail[i] = d.trail[i - 1];
        }
        d.trail[0] += machine ? -0.125f : 0.125f;
        d.trail[0] = d.trail[0] < 0.0f ? 0.0f : d.trail[0] > 1.0f ? 1.0f : d.trail[0];
    }
}

// the backdrop: the dealer's four-image loop scrolling left at half brightness
void backdrop_draw(Ui& ui, DealerScreen& d) {
    const std::vector<double> strips = ui_values(ui.data, dl, "dealer_backdrop_strips", 18);
    const auto strip = [&](int k) {
        const std::size_t at = static_cast<std::size_t>(d.dealer * 6 + d.frame + k);
        return at < strips.size() ? static_cast<int>(strips[at]) : -1;
    };
    float x = d.scroll;
    for (int k = 0; k < 3; ++k) {
        Sprite s = at(ui, strip(k), x, 0.0f, 1);
        s.color = 0xff808080;
        sprite_submit(ui, s);
        x += static_cast<float>(sprite_width(ui, strip(k)));
    }
    d.scroll -= 0.5f;
    if (static_cast<float>(std::abs(static_cast<int>(d.scroll))) >
        static_cast<float>(sprite_width(ui, strip(0)))) {
        d.scroll = 0.0f;
        if (++d.frame > 3) {
            d.frame = 0;
        }
    }
}

// the logo: big and dim before the menu, brightening during the intro, then the trail
void logo_draw(Ui& ui, DealerScreen& d) {
    const int logo = table_at(ui, "dealer_logos", d.dealer);
    if (d.plate < 1.0f || d.intro < 1.0f) {
        Sprite s = at(ui, logo, 155.0f, 81.0f, 0x101);
        s.color = d.plate < 1.0f ? 0x7fffffffu
                                 : static_cast<std::uint32_t>(static_cast<int>((d.intro + 1.0f) * 127.5f))
                                           << 24 |
                                       0xffffffu;
        s.flags |= sprite_smooth;
        s.scale[0] = s.scale[1] = 1.875f;
        sprite_submit(ui, s);
        return;
    }
    float alpha = 255.0f;
    for (int i = 0; i < 15; ++i) {
        const float g = d.trail[i];
        Sprite s = at(ui, logo, g * 60.0f + 95.0f, 115.0f - g * 34.0f, 0x101);
        s.scale[0] = (g * 154.0f + 176.0f) * (1.0f / 176.0f);
        s.scale[1] = (g * 126.0f + 144.0f) * (1.0f / 144.0f);
        s.color = static_cast<std::uint32_t>(static_cast<int>(alpha)) << 24 | 0xffffffu;
        s.flags |= sprite_smooth;
        sprite_submit(ui, s);
        alpha *= 0.7f;
    }
}

// the maker's panel popping by box_pop_scale(5, 10, pop)
// decomp: dealer_profile_panel_draw 0x39505
void profile_draw(Ui& ui, DealerScreen& d) {
    float scale[2];
    box_pop_scale(scale, 5, 10, d.pop);
    Sprite s = at(ui, table_at(ui, "dealer_profile_panel_block", 13 + d.dealer), 318.0f, 212.0f,
                  0x102);
    s.scale[0] = scale[0];
    s.scale[1] = scale[1];
    s.flags |= 8;
    if (scale[0] > 0.0f && scale[1] > 0.0f) {
        sprite_submit(ui, s);
    }
}

// the menu items' card: {-1, the three machines' cards, -1}
int menu_card(const DealerScreen& d, int item) {
    return item >= 1 && item <= 3 ? d.cards[item - 1] : -1;
}

// the plate sliding with the menu plate, the machine card
// cross-fading, the five buttons landing right to left, day and money
// decomp: dealer_machine_card_draw 0x3963E
void card_draw(FrontEnd& fe, Ui& ui, DealerScreen& d, bool pulse) {
    const float fade = d.card_fade;
    d.card_fade = d.card_fade < 1.0f ? d.card_fade + 0.05f : 1.0f;
    const float w = 400.0f;
    sprite_submit(ui, at(ui, 0x4bd, d.plate * w - w, 385.0f, 0x101));
    if (d.row != -1) {
        sprite_submit(ui, at(ui, table_at(ui, "dealer_machine_plates", d.dealer), 40.0f, 284.0f,
                             0x101));
    }
    if (menu_card(d, d.cursor) != -1) {
        if (menu_card(d, d.item_before) != -1) {
            Sprite was = at(ui, menu_card(d, d.item_before), 122.0f, 275.0f, 0x101);
            was.color = alpha_white(1.0f - fade);
            sprite_submit(ui, was);
        }
        Sprite now = at(ui, menu_card(d, d.cursor), 122.0f, 275.0f, 0x102);
        now.color = alpha_white(fade);
        sprite_submit(ui, now);
    }
    const std::vector<double> start = ui_values(ui.data, dl, "dealer_menu_button_start", 10);
    const std::vector<double> place = ui_values(ui.data, dl, "dealer_menu_button_x", 10);
    for (int i = 0; i < 5; ++i) {
        const float px = static_cast<float>(place[static_cast<std::size_t>(i * 2)]);
        const float py = static_cast<float>(place[static_cast<std::size_t>(i * 2 + 1)]);
        if (i == d.cursor && !(d.intro < 1.0f)) {
            Sprite lit = at(ui, d.buttons[i][0], px, py, 0x103);
            lit.flags |= pulse ? 0x2000 : 0;
            sprite_submit(ui, lit);
        }
        float x = d.intro * 800.0f + static_cast<float>(start[static_cast<std::size_t>(i * 2)]);
        x = x > px ? px : x;
        sprite_submit(ui, at(ui, d.buttons[i][1], x, py, 0x102));
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
}

} // namespace

int dealer_price(FrontEnd& fe, const DealerScreen& d, int i) {
    if (i < 0 || i > 2 || d.machines[i] < 0 ||
        d.machines[i] >= static_cast<int>(fe.shop.tables.builds.size())) {
        return 0;
    }
    // price_with_tax: (int)((float)total * 1.1f), floor(total * 11 / 10) for every total
    const ShopBuild& b = fe.shop.tables.builds[static_cast<std::size_t>(d.machines[i])];
    return static_cast<int>(static_cast<float>(shop_scoobee_total(fe.shop.tables, b)) * 1.1f);
}

// decomp: dealer_screen_draw 0x39DC6
void dealer_screen_draw(FrontEnd& fe, Ui& ui, DealerScreen& d, bool pulse) {
    trail_step(d);
    backdrop_draw(ui, d);
    const UiTable* rows = ui_table(ui.data, dl, "dealer_screen_rows");
    if (rows && !rows->rows.empty() && rows->rows[0].size() > 12) {
        const int top = static_cast<int>(rows->rows[0][static_cast<std::size_t>(10 + d.dealer)]);
        sprite_submit(ui, at(ui, top, 0.0f, 0.0f, 0x102));
    }
    sprite_submit(ui, at(ui, table_at(ui, "dealer_bottom_panels", d.dealer), 0.0f, 376.0f, 0x101));
    logo_draw(ui, d);
    Sprite band = at(ui, 0x4bc, 0.0f, 350.0f, 0x101);
    band.scale[0] = 100.0f;
    sprite_submit(ui, band);
    profile_draw(ui, d);
    card_draw(fe, ui, d, pulse);
    // dealer_price_draw
    if (d.row >= 0) {
        sprite_submit(ui, at(ui, 0xe8b, 416.0f, 330.0f, 0x101));
        money_amount_draw(ui, dealer_price(fe, d, d.row), 0x23a, 0x14a, 0x101, 10, 0xfff0f0f0);
    }
}

// decomp: dealer_machine_menu_draw 0x398F7
void dealer_machine_menu_draw(Ui& ui, DealerScreen& d) {
    sprite_submit(ui, at(ui, 0x4aa, 187.0f, 220.0f, 0x102));
    sprite_row_draw(ui, slot_at(ui, dl, "dealer_machine_menu_sprites", d.sub));
}

// decomp: dealer_garage_list_draw 0x39958
void dealer_garage_list_draw(FrontEnd& fe, Ui& ui, DealerScreen& d, bool lit) {
    const Garages& g = fe.world->garages;
    sprite_submit(ui, at(ui, 0x4b6, 191.0f, 186.0f, 0x104));
    sprite_submit(ui, at(ui, 0x4b7, 201.0f, 224.0f, 0x105));
    Sprite right = at(ui, 0x4b7, 422.0f, 224.0f, 0x105);
    right.flags |= sprite_mirror_u;
    sprite_submit(ui, right);
    // the wraps 9 -> 0 and 0 -> 9 scroll as 10 and -1
    const int target = d.garage_was == 0 && d.garage == 9   ? -1
                       : d.garage_was == 9 && d.garage == 0 ? 10
                                                            : d.garage;
    const float off = static_cast<float>(d.garage_was - target) * static_cast<float>(d.slide) * 7.0f;
    // the lit arrow on the side of the move: retail lights it by the raw cursor (so the wraps
    // light the wrong one) and draws it once per label, five times
    if (d.slide != 8) {
        Sprite arrow = at(ui, 0x4b7, target > d.garage_was ? 422.0f : 201.0f, 224.0f, 0x105);
        arrow.flags |= target > d.garage_was ? 0x400 : 0;
        arrow.color = 0xffff9400;
        sprite_submit(ui, arrow);
    }
    for (int k = -2; k < 3; ++k) {
        int n = d.garage_was + k;
        n = n < 0 ? n + 10 : n > 9 ? n - 10 : n;
        const bool empty = (garage_status(fe.shop.tables, g, n) & 2) != 0;
        const int label = table_at(ui, "dealer_garage_labels", n);
        if (d.slide == 8 && n == d.garage) {
            Sprite cur = at(ui, table_at(ui, "dealer_garage_labels", d.garage), 283.0f, 218.0f,
                            0x103);
            cur.color = empty ? 0xffff9400 : 0xff333333;
            cur.flags |= lit ? 0x2000 : 0;
            sprite_submit(ui, cur);
        }
        Sprite s = at(ui, label, static_cast<float>(k) * 56.0f + off + 283.0f, 218.0f, 0x102);
        s.color = empty ? 0xffffffff : 0xff333333;
        sprite_submit_clipped(ui, s, 219.0f, 218.0f, 420.0f, 258.0f);
    }
}
