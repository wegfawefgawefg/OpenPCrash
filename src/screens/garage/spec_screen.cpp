// The spec page (spec_scoobee_screen_* state 0x24, decomp src/o1/u_0357d7.c): the build left at
// GameState +0x10F5C on the stand (mode 5), its numbers on page 0 and its weapons on page 1.
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

constexpr int common_asset = 0x690, spec_asset = 0x686, module_asset = 0x692;
// D_0019D5A8: the header's origin; D_0019D5B0: the body class icons (retail .rdata)
constexpr int origin_x = 51, origin_y = 104;
constexpr int class_icons[8] = {2534, 2535, 2532, 2536, 2537, 2533, -1, -1};
// D_0019D768, the empty weapon slot
constexpr const char* not_equipped = "Ｎｏｔ　Ｅｑｕｉｐ";

// spec_sprites from row on, as sprite_rows_draw draws them. Rows 3 (the frame, 2544), 6 and 10
// (the label panels, 2542 and 2541) hold (0, 0) in the XBE; three boot constructors write their
// places from the origin (src/o1/u_11cf00.c): func_0011CF00 row 3 at the origin, func_0011CF3A
// row 6 and func_0011CF15 row 10 at the origin plus (25, 80).
void spec_rows_draw(Ui& ui, int row) {
    const SpriteSlot* rows = ui_slots(ui.data, "spec_scoobee", "spec_sprites", row);
    if (!rows) {
        return;
    }
    for (int i = row; rows->sprite >= 0; ++rows, ++i) {
        SpriteSlot slot = *rows;
        if (i == 3) {
            slot.x = origin_x;
            slot.y = origin_y;
        } else if (i == 6 || i == 10) {
            slot.x = origin_x + 25.0f;
            slot.y = origin_y + 80.0f;
        }
        sprite_row_draw(ui, slot);
    }
}

std::string help_line(Ui& ui, int page) {
    const UiTable* t = ui_table(ui.data, "spec_scoobee",
                                page == 0 ? "spec_scoobee_help" : "spec_scoobee_help_page2");
    return t && !t->strings.empty() ? t->strings[0] : std::string();
}

// walk, run and jump with the weight factors, clamped
// decomp: chassis_spec_values 0x7C1FD
void spec_rates(const PartTables& t, const ShopBuild& b, float& walk, float& run, float& jump) {
    const LegPart* leg = leg_part(t, b.leg);
    const BodyPart* body = body_part(t, b.body);
    const float capacity = leg ? curve_value(leg->capacity, b.leg_tune) : 0.0f;
    const float weight = build_weight(t, b);
    const float parts = body && leg ? curve_value(body->weight, 0) + curve_value(leg->weight, 0) : 0.0f;
    const auto factor = [&](float base) {
        float root = 1.0f;
        if (weight != 0.0f) {
            root = std::sqrt(base / weight);
            root = (1.0f - static_cast<float>(b.leg_tune) / 99.0f * 0.1f) * root;
            if (capacity < weight) {
                root = std::pow(capacity / weight, 10.0f) * root;
            }
        }
        return root;
    };
    const BodyPart* body0 = body_part(t, 0);
    const LegPart* leg0 = leg_part(t, 0);
    const float base0 =
        body0 && leg0 ? curve_value(leg0->weight, 0) + curve_value(body0->weight, 0) : 0.0f;
    const float selected = factor(parts), fixed = factor(base0);
    run = (leg ? curve_value(leg->speed, b.leg_tune) : 0.0f) * selected;
    jump = (leg ? curve_value(leg->jump, b.leg_tune) : 0.0f) * fixed;
    walk = run / 2.0f;
    walk = walk < 10.0f ? 10.0f : walk > 1000.0f ? 1000.0f : walk;
    run = run < 20.0f ? 20.0f : run > 2000.0f ? 2000.0f : run;
    jump = jump < 2.5f ? 2.5f : jump > 100.0f ? 100.0f : jump;
}

// decomp: spec_numbers_draw 0x3BBA5
void numbers_draw(FrontEnd& fe, Ui& ui) {
    const PartTables& t = fe.shop.tables;
    const ShopBuild& b = fe.shop.spec;
    float walk = 0, run = 0, jump = 0;
    spec_rates(t, b, walk, run, jump);
    spec_rows_draw(ui, 10);
    const auto num = [&](std::uint32_t flags, float v, int digits, int decimals, int y) {
        number_draw_full(ui, flags, static_cast<long long>(v), v, 0x14f, y, 0x104, 0xfff0f0f0,
                         digits, decimals, 1, 0);
    };
    int camo = 0;
    for (const int option : b.option) {
        if (option != -1 && option_part(t, option)) {
            camo += option_part(t, option)->camo_frames;
        }
    }
    const LegPart* leg = leg_part(t, b.leg);
    const BodyPart* body = body_part(t, b.body);
    int y = 0xc1;
    num(0x48, static_cast<float>(build_durability(t, b)), 6, 0, y);
    num(0x68, walk, 3, 3, y += 0x18);
    num(0x68, run, 3, 3, y += 0x18);
    num(0x68, jump, 3, 3, y += 0x18);
    num(0x48, static_cast<float>(camo) / 60.0f, 3, 1, y += 0x18);
    num(0x68, build_weight(t, b) / 100.0f, 3, 3, y += 0x18);
    num(0x68, (leg ? curve_value(leg->capacity, b.leg_tune) : 0.0f) / 100.0f, 3, 3, y += 0x18);
    num(0x68, body ? static_cast<float>(static_cast<int>(curve_value(body->sockets, b.body_tune))) : 0,
        6, 0, y += 0x18);
}

// decomp: spec_weapons_draw 0x3B7AE
void weapons_draw(FrontEnd& fe, Ui& ui) {
    const PartTables& t = fe.shop.tables;
    const ShopBuild& b = fe.shop.spec;
    spec_rows_draw(ui, 6);
    const int ys[4] = {0xd5, 0x105, 0x135, 0x165};
    for (int m = 0; m < 4; ++m) {
        const WeaponPart* w = weapon_part(t, m < 2 ? 2 : 3, b.weapon[m]);
        const GlyphText name = w ? w->name.glyphs : utf8_to_glyphs(ui.data, not_equipped);
        text_draw(ui, name, origin_x + 0x5e, ys[m], 7, 18, 18, w ? 0xfff0f0f0 : 0xffff6060, 0x32);
    }
}

// decomp: spec_header_draw 0x3BA14
void header_draw(FrontEnd& fe, Ui& ui, SpecScreen& s) {
    const PartTables& t = fe.shop.tables;
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    spec_rows_draw(ui, 0);
    const int x = origin_x + 0x74, y = origin_y + 0x1d;
    text_draw(ui, glyph_name_text(fe.shop.spec_name, glyph_name_length), x, y, 3, 16, 16,
              0xfff0f0f0, 9);
    GlyphText chip = glyph_name_text(fe.shop.spec_chip_name, glyph_name_length);
    if (fe.shop.spec_chip_name[0] == -1 && chip_part(t, fe.shop.spec.chip)) {
        chip = chip_part(t, fe.shop.spec.chip)->name.glyphs;
    }
    text_draw(ui, chip, x, y + 0x1c, 3, 16, 16, 0xfff0f0f0, 9);
    if (const BodyPart* body = body_part(t, fe.shop.spec.body)) {
        const int klass = body->body_class & 0xff;
        sprite_draw_at(ui, klass < 8 ? class_icons[klass] : -1, origin_x + 257.0f, origin_y + 13.0f,
                       0x103);
    }
    text_strip_draw_scroll(ui, s.help[s.page], 0xfff0f0f0);
    stand_draw(ui, fe.shop.stand);
    icon_on_plate_draw(ui, 0x119, 0x17c, 0x102, 0xc3e, 0x13);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

void page_draw(FrontEnd& fe, Ui& ui, SpecScreen& s) {
    header_draw(fe, ui, s);
    if (s.page == 0) {
        numbers_draw(fe, ui);
    } else {
        weapons_draw(fe, ui);
    }
}

} // namespace

void spec_spawn(FrontEnd& fe, Ui&) {
    fe.screen.spec.from = fe.previous;
}

void spec_step(FrontEnd& fe, Ui& ui) {
    SpecScreen& s = fe.screen.spec;
    if (fe.screen.step != 0) {
        stand_update(ui, fe.shop.stand);
    }
    switch (fe.screen.step) {
    case 0: // spec_scoobee_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, spec_asset);
        texture_set_load(ui, module_asset);
        level_meter_spawn(ui, fe.screen.meter);
        help_strip_init(ui, s.help[0], help_line(ui, 0));
        help_strip_init(ui, s.help[1], help_line(ui, 1));
        stand_start(ui, fe.shop.stand, 5);
        // the machine's side of the sheet, right of the page arrows: a press there turns it
        // instead of the page
        stand_area_set(fe.shop.stand, {400, 140, 640, 420});
        stand_show(ui, fe.shop.stand, fe.shop.spec);
        s.page = 0;
        state_music_play(fe, ui, 0x24);
        s.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        fade_in_start(ui, static_cast<float>(s.frames));
        fe.screen.step = 1;
        break;
    case 1: // step2
        page_draw(fe, ui, s);
        if (--s.frames < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3 (numbers) and step4 (weapons)
        // the mouse: the wheel or a click on the page turns it
        int page = s.page;
        const bool turned = menu_cursor(ui, page, 0, 2, 0x15, 0, 9) != 0;
        const PointerRect sheet = {32, 180, 608, 400};
        bool click = ui.pointer.left && pointer_inside(ui.pointer, sheet);
        if (click || (ui.pointer.wheel != 0 && !turned)) {
            page ^= 1;
            ui_sound(ui, bank_sys, se_page_switch);
            menu_input_mark(ui);
        }
        if (page != s.page) {
            s.page = page;
            help_strip_init(ui, s.help[s.page], help_line(ui, s.page));
        }
        page_draw(fe, ui, s);
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            s.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
            fade_out_start(ui, static_cast<float>(s.frames));
            fe.screen.step = 3;
        }
        break;
    }
    case 3: // step5: back by where it was opened from
        page_draw(fe, ui, s);
        if (--s.frames < 0) {
            ui_music_stop(ui);
            task_header_set(fe, s.from == app_assemble        ? 1
                                : s.from == app_hangar        ? 0
                                : s.from == app_wild_machines ? 3
                                : s.from == app_keep_sv       ? 4
                                                              : 2);
            fe.screen.step = 4;
        }
        break;
    default:
        page_draw(fe, ui, s);
        break;
    }
}

void spec_destroy(FrontEnd& fe, Ui& ui) {
    ui_music_stop(ui);
    stand_hide(fe.shop.stand);
    ui.clear_colour = 0xff000000;
}
