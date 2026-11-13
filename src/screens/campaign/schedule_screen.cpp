// The schedule (schedule_* state 0x20, decomp src/o1/u_0357d7.c 0x357D7..0x3693D, the grid
// schedule_grid_draw and schedule_fade_step; docs/port/screens.md "The schedule
// card"): five days of the three areas' tournaments from today, the columns sliding a day at a time
// past the edges or five with the triggers, and a tournament's card on accept. Reached from the
// garage hub and Club Wired; back to whichever it came from.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/area_select.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/front_end.hpp"
#include "story/roster_tables.hpp"
#include "world/world.hpp"

#include <cstdio>

namespace {

constexpr int burning_chrome_first = 0x34; // 52..54: never marked won
constexpr int final_tournament = 0x30;     // gone from the schedule once won

int cadence_ticks(const Ui& ui, float seconds) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * seconds);
}

int id_at(const ScheduleScreen& s, int row, int day) {
    return s.ids[row][((day % 60) + 60) % 60];
}

// a cell with nothing to show: no tournament, or the final once won
bool cell_empty(const FrontEnd& fe, int id) {
    return id == -1 || (tournament_won(fe.world->story, final_tournament) && id == final_tournament);
}

SpriteSlot slot(Ui& ui, const char* table, int row) {
    const SpriteSlot* rows = ui_slots(ui.data, "schedule", table);
    return rows ? rows[row] : SpriteSlot{};
}

// rows from `row` with their alpha times the fade
// decomp: sprite_rows_draw_faded 0x358E1
void rows_faded(Ui& ui, SpriteSlot row, float alpha) {
    const std::uint32_t a = static_cast<std::uint32_t>(alpha * 255.0f) << 24 | 0xffffff;
    row.color &= a;
    sprite_row_draw(ui, row);
}

// the cursor's tournament as the help line
// decomp: schedule_title_set 0x35841
void title_set(FrontEnd& fe, Ui& ui, ScheduleScreen& s) {
    if (s.row >= 3) {
        return;
    }
    const int id = id_at(s, s.row, s.first + s.col);
    if (id == -1) {
        return;
    }
    // D_0019CAA0: the line's tail after the name
    std::string text = fe.campaign.roster.tournament_names[static_cast<std::size_t>(id)];
    text += text_string(ui, "str.19caa0");
    text_strip_init(ui, s.title, 0, 0x48, text, 0, 0, 640, 480);
}

// the five days' numbers over the columns
// decomp: schedule_day_numbers_draw 0x3596C
void day_numbers_draw(Ui& ui, ScheduleScreen& s) {
    const std::vector<double> digits = ui_values(ui.data, "schedule", "schedule_day_digits", 11);
    for (int i = 0; i < 5; ++i) {
        int day = s.first + i;
        day = day < 0 ? day + 60 : day >= 60 ? day - 60 : day;
        ++day;
        const int x = i * 0x53 + 0xd8;
        SpriteSlot tens = slot(ui, "schedule_sprites_3", 4);
        SpriteSlot ones = slot(ui, "schedule_sprites_3", 5);
        tens.x = static_cast<float>(x - 10);
        ones.x = static_cast<float>(x);
        tens.y = ones.y = 133.0f;
        tens.sprite = static_cast<int>(digits[static_cast<std::size_t>(day / 10 + 1)]);
        ones.sprite = static_cast<int>(digits[static_cast<std::size_t>(day % 10 + 1)]);
        rows_faded(ui, tens, s.alpha);
        rows_faded(ui, ones, s.alpha);
    }
}

// schedule_grid_draw: the cells, their rule marks and won marks, sliding or squeezing
// while a day scrolls past
// decomp: schedule_grid_draw 0x35A1F
void grid_draw(FrontEnd& fe, Ui& ui, ScheduleScreen& s) {
    if (s.anim == 0 && s.row < 3) {
        const int id = id_at(s, s.row, s.first + s.col);
        if (!cell_empty(fe, id)) {
            text_strip_draw_scroll(ui, s.title, 0xffffffff);
        }
    }
    day_numbers_draw(ui, s);
    const int step = cadence_ticks(ui, 0.05f);
    int first = s.first - s.shift;
    if (s.anim == 2 || s.anim == 3) {
        first = s.shift > 0 ? (first + 1) % 60 : s.shift < 0 ? (first + 59) % 60 : first;
    }
    const std::vector<double> marks = ui_values(ui.data, "schedule", "schedule_marks", 9);
    // the cell plates by area row: none, day and night, lit and not
    const int plates[3][4] = {{0x962, 0x963, 0x964, 0x965},
                              {0x95d, 0x95e, 0x95f, 0x960},
                              {0x959, 0x95a, 0x95b, 0x95c}};
    for (int i = 0; i < 5; ++i) {
        const bool lit = i == s.col && s.shift_px == 0;
        float scale = 1.0f, slide = 0.0f;
        const float t = step > 0 ? static_cast<float>(s.anim_timer) / static_cast<float>(step) : 0.0f;
        if (s.anim == 1) {
            if ((s.shift > 0 && i == 0) || (s.shift < 0 && i == 4)) {
                scale = t;
            }
        } else if (s.anim == 2) {
            slide = (83.0f / static_cast<float>(step)) * static_cast<float>(s.anim_timer) *
                    (s.shift > 0 ? 1.0f : -1.0f);
            if ((s.shift > 0 && i == 4) || (s.shift < 0 && i == 0)) {
                continue;
            }
        } else if (s.anim == 3) {
            if ((s.shift < 0 && i == 0) || (s.shift > 0 && i == 4)) {
                scale = 1.0f - t;
            }
        }
        for (int r = 0; r < 3; ++r) {
            const int id = id_at(s, r, first + i);
            const bool has = !cell_empty(fe, id);
            const bool night = has && fe.story_tables.tournaments[static_cast<std::size_t>(id)].night == 1;
            int icon = -1;
            if (has) {
                int k = 7;
                switch (fe.story_tables.tournaments[static_cast<std::size_t>(id)].rule) {
                case 0: k = 6; break;
                case 1: k = 2; break;
                case 2: k = 3; break;
                case 3: k = 4; break;
                case 4: k = 5; break;
                case rule_area_ranker:
                    k = 1;
                    break;
                case rule_first_ranker:
                    k = 0;
                    break;
                default: break;
                }
                icon = static_cast<int>(marks[static_cast<std::size_t>(k)]);
            }
            SpriteSlot cell = slot(ui, "schedule_sprites_2", 3);
            cell.sprite = !has ? (lit ? 0x935 : 0x934) : plates[r][(night ? 2 : 0) + (lit ? 1 : 0)];
            cell.x = static_cast<float>(i * 0x53 + 0x9e);
            cell.y = static_cast<float>(r * 0x3e + 0x9f);
            const auto place = [&](SpriteSlot row) {
                if (s.anim == 2) {
                    sprite_row_offset(ui, row, slide, 0.0f);
                } else {
                    row_anchored(ui, row, scale, 1.0f);
                }
            };
            if (icon != -1) {
                SpriteSlot mark = slot(ui, "schedule_sprites_5", 3);
                mark.sprite = icon;
                mark.x = cell.x;
                mark.y = cell.y;
                mark.color = lit && s.anim == 0 ? 0xffffffff : 0xff404040;
                place(mark);
            }
            place(cell);
            if (has && tournament_won(fe.world->story, id) && id != burning_chrome_first &&
                id != burning_chrome_first + 1 && id != burning_chrome_first + 2) {
                SpriteSlot won = slot(ui, "schedule_sprites_2", 5);
                won.x = cell.x;
                won.y = cell.y;
                place(won);
            }
        }
    }
    if (s.anim_timer > 0) {
        --s.anim_timer;
        return;
    }
    switch (s.anim) {
    case 1:
        s.anim = 2;
        s.anim_timer = step;
        break;
    case 2:
        s.anim = 3;
        s.anim_timer = step;
        break;
    case 3:
        s.anim = 1;
        s.shift += s.shift > 0 ? -1 : 1;
        s.anim_timer = step;
        if (s.shift == 0) {
            s.anim = 0;
        }
        break;
    default:
        break;
    }
}

// decomp: schedule_screen_draw 0x35F47
void draw(FrontEnd& fe, Ui& ui, ScheduleScreen& s) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, "schedule", "schedule_sprites"));
    rows_faded(ui, slot(ui, "schedule_sprites", 4), s.alpha);
    if (s.row != 3) {
        if (!s.card && s.anim == 0) {
            const float x = static_cast<float>(s.col * 0x53 + 0x9d);
            const float y = static_cast<float>(s.row * 0x3e + 0x9f);
            for (int k = 0; k < 3; ++k) {
                SpriteSlot c = slot(ui, "schedule_sprites_3", k);
                c.x = x;
                if (k < 2) {
                    c.y = y;
                }
                sprite_row_draw(ui, c);
            }
        }
        sprite_row_draw(ui, slot(ui, "schedule_sprites_4", 1));
    } else {
        sprite_row_draw(ui, slot(ui, "schedule_sprites_4", 1));
        sprite_row_draw(ui, slot(ui, "schedule_sprites_4", 0)); // Exit lit
    }
    if (s.shift > 0) {
        sprite_row_draw(ui, slot(ui, "schedule_sprites_2", 1));
    } else if (s.shift < 0) {
        sprite_row_draw(ui, slot(ui, "schedule_sprites_2", 0));
    }
    sprite_row_draw(ui, slot(ui, "schedule_sprites", 6));
    sprite_row_draw(ui, slot(ui, "schedule_sprites", 7));
    grid_draw(fe, ui, s);
    // the cells' plates are drawn from their corner (grid_draw), not centred there
    for (int i = 0; i < 5; ++i) {
        for (int r = 0; r < 3; ++r) {
            s.cells[r][i] = sprite_rect(ui, 0x934, static_cast<float>(i * 0x53 + 0x9e),
                                        static_cast<float>(r * 0x3e + 0x9f));
        }
    }
    s.exit_rect = sprite_rect(ui, slot(ui, "schedule_sprites_4", 1).sprite, 261, 349);
    if (s.anim != 0 || !s.card) {
        return;
    }
    // the card grows out of its cell over frame_cadence * 0.2 updates
    const int delay = cadence_ticks(ui, 0.2f);
    const float t = delay > 0 ? static_cast<float>(s.card_delay) / static_cast<float>(delay) : 0.0f;
    const float scale = (1.0f - t) * 0.7f + 0.3f;
    const SpriteSlot card = slot(ui, "schedule_sprites_4", 3);
    Sprite c = sprite_of(ui, card.sprite);
    c.pos[0] = card.x + (static_cast<float>(s.col * 0x53) + 158.0f - 170.0f) * t;
    c.pos[1] = card.y + (static_cast<float>(s.row * 0x3e) + 159.0f - 110.0f) * t;
    c.blend = card.blend;
    c.color = card.color;
    c.flags |= card.flags | (scale < 1.0f ? 0x10u : 0u);
    c.scale[0] = c.scale[1] = scale;
    sprite_submit(ui, c);
    if (s.card_delay > 0) {
        --s.card_delay;
        return;
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "schedule", "schedule_sprites_5"));
    const int id = id_at(s, s.row, s.first + s.col);
    const RosterTables& t2 = fe.campaign.roster;
    const int rule = tournament_rule(fe.story_tables, id);
    text_draw(ui, glyphs_of(ui, t2.tournament_names[static_cast<std::size_t>(id)]), 0xb8, 0x9d, 8,
              18, 18, 0xfff0f0f0, 0x200);
    text_draw(ui, glyphs_of(ui, t2.rule_names[static_cast<std::size_t>(rule)]), 0xb8, 0xc0, 8, 18,
              18, 0xfff0f0f0, 0x200);
    rule_lines_draw(fe, ui, id, 0xb8, 0xe3, 0x109);
    const int fee = fe.story_tables.rules[static_cast<std::size_t>(rule)].entry_fee;
    const int width = number_draw_ex(ui, 0x104, fee, 0xb8, 0x14e, 0x109, 0xfff0f0f0, 9, 10, 3);
    number_draw_ex(ui, 4, fee, 0xb8, 0x14e, 0x109, 0xfff0f0f0, 9, 10, 3);
    sprite_draw_at(ui, 0xeaa, static_cast<float>(0xba - width), 334.0f, 0x109);
}

// decomp: schedule_screen_input 0x36367
void input(FrontEnd& fe, Ui& ui, ScheduleScreen& s) {
    draw(fe, ui, s);
    if (s.anim != 0) {
        return;
    }
    if (s.card) {
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            s.card = false;
        }
        return;
    }
    // PC: the pointer lights the cell or EXIT it enters
    PointerRect places[16];
    for (int i = 0; i < 15; ++i) {
        places[i] = s.cells[i / 5][i % 5];
    }
    places[15] = s.exit_rect;
    int place = s.row == 3 ? 15 : s.row * 5 + s.col;
    if (menu_cursor(ui, place, 0, 16, cursor_no_dpad, bank_sys, se_menu_cursor_horizontal,
                    places)) {
        s.row = place == 15 ? 3 : place / 5;
        s.col = place == 15 ? s.col : place % 5;
    }
    if (s.row == 3) {
        if (menu_accept(ui, &s.exit_rect)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            s.timer = cadence_ticks(ui, 0.5f);
            fade_out_start(ui, static_cast<float>(s.timer));
            s.leaving = true;
            return;
        }
        menu_cursor(ui, s.row, 0, 4, cursor_menu_vertical, 0, 3);
        if (s.row < 3) {
            title_set(fe, ui, s);
        }
        return;
    }
    if (menu_accept(ui, &s.cells[s.row][s.col])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        const int id = id_at(s, s.row, s.first + s.col);
        if (!cell_empty(fe, id)) {
            s.card = true;
            s.card_delay = cadence_ticks(ui, 0.2f);
        }
        return;
    }
    if (menu_cancel(ui)) {
        if (s.row != 3) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        s.row = 3;
        return;
    }
    const int moved = menu_cursor(ui, s.col, 0, 5, cursor_menu_horizontal, 0, 4);
    const int step = cadence_ticks(ui, 0.05f);
    if (moved != 0) {
        // past an edge: a day scrolls in
        if (moved < 0 && s.col == 4) {
            s.col = 0;
            s.first = (s.first + 59) % 60;
            s.shift = -1;
            s.anim_timer = step;
            s.anim = 1;
        } else if (moved > 0 && s.col == 0) {
            s.col = 4;
            s.first = (s.first + 1) % 60;
            s.shift = 1;
            s.anim_timer = step;
            s.anim = 1;
        }
    } else if (menu_cursor(ui, s.row, 0, 4, cursor_menu_vertical, 0, 3) == 0) {
        // the triggers: five days on or back (schedule_fade_step's one-update dip of the days)
        // PC: and a click on the arrows beside the grid
        const PadRecord pad = menu_pad_unclicked(ui);
        const PointerRect arrows[2] = {{4, 214, 52, 272}, {588, 214, 636, 272}};
        const int arrow = ui.pointer.left ? pointer_pick(ui.pointer, arrows, 2) : -1;
        const bool right =
            arrow == 1 || pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_right_trigger);
        const bool left =
            !right &&
            (arrow == 0 || pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_left_trigger));
        if ((right || left) && s.shift == 0) {
            if (right) {
                s.col = 0;
                s.fade_a = 0x14;
                s.page = (s.page + 1) % 12;
                s.first = (s.first + 5) % 60;
                s.shift = 5;
            } else {
                s.col = 4;
                s.fade_b = 0x14;
                s.page = (s.page + 11) % 12;
                s.first = (s.first + 55) % 60;
                s.shift = -5;
            }
            s.anim = 1;
            s.anim_timer = step;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            s.fade_step = true;
        }
    }
    if (s.row < 3) {
        title_set(fe, ui, s);
    }
}

// schedule_fade_step: one update of the days' numbers dimmed, then back to the input
// decomp: schedule_fade_step 0x366F0
void fade_step(FrontEnd& fe, Ui& ui, ScheduleScreen& s) {
    if (s.fade_a > 0) {
        --s.fade_a;
        s.shift_px = (s.fade_a - 0x14) << 5;
        s.alpha = static_cast<float>(s.fade_a) * 0.05f;
    } else if (s.fade_b > 0) {
        --s.fade_b;
        s.shift_px = (0x14 - s.fade_b) << 5;
        s.alpha = static_cast<float>(s.fade_b) * 0.05f;
    }
    draw(fe, ui, s);
    s.shift_px = 0;
    s.alpha = 1.0f;
    s.fade_step = false;
}

} // namespace

void schedule_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.schedule = ScheduleScreen{};
    fe.screen.campaign.schedule.from = fe.previous;
}

void schedule_step(FrontEnd& fe, Ui& ui) {
    ScheduleScreen& s = fe.screen.campaign.schedule;
    World& w = *fe.world;
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    if (s.from == app_club_wired) {
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
    }
    switch (fe.screen.step) {
    case 0: // schedule_screen_update: today first, the three rows of 60 days
        texture_set_load(ui, 0x690);
        texture_set_load(ui, 0x683);
        level_meter_spawn(ui, fe.screen.meter);
        s.first = w.story.day;
        s.page = w.story.day / 5;
        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 60; ++d) {
                s.ids[r][d] = game_tournament_slot(w.story, fe.story_tables, d, r);
            }
        }
        title_set(fe, ui, s);
        if (s.from == app_club_wired) {
            music_shuffle_catalogue(fe.music); // Club Wired's songs carry on
        } else {
            ui_music(ui, 0xc, true);
        }
        s.timer = cadence_ticks(ui, 0.5f);
        fade_in_start(ui, static_cast<float>(s.timer));
        fe.screen.step = 1;
        break;
    case 1: // schedule_screen_fade_in_end
        menu_input_mark(ui);
        draw(fe, ui, s);
        if (--s.timer < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = 2;
        }
        break;
    case 2:
        if (s.leaving) {
            // schedule_screen_leave: the hub, or Club Wired
            menu_input_mark(ui);
            draw(fe, ui, s);
            if (--s.timer < 0) {
                fe.screen.strip.active = false;
                task_header_set(fe, s.from == app_club_wired ? 1 : 0);
                fe.screen.step = 3;
            }
        } else if (s.fade_step) {
            fade_step(fe, ui, s);
        } else {
            input(fe, ui, s);
        }
        break;
    default:
        draw(fe, ui, s);
        break;
    }
}

void schedule_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
