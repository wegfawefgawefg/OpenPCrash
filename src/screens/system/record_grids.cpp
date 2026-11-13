// The records' tournament pages (decomp src/o1/u_02edc5.c record_tournament_page,
// record_tournament_grid_update, record_tournament_grid6_update): the First
// Ranker's card, the three areas' class grids with their ranker, the other tournaments' two
// sheets, and the trophies of what was won (drawn through the renderer from `trophies`); and the
// colour lerp and the unrolling panel every records page draws with.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

// a to b by t per channel, b's alpha
// decomp: record_color_lerp 0x333B9
std::uint32_t record_color_lerp(std::uint32_t a, float t, std::uint32_t b) {
    t = t > 1.0f ? 1.0f : t < 0.0f ? 0.0f : t;
    const float u = 1.0f - t;
    auto channel = [&](int shift) {
        const float ca = static_cast<float>((a >> shift) & 0xff);
        const float cb = static_cast<float>((b >> shift) & 0xff);
        return static_cast<std::uint32_t>(static_cast<int>(ca * u + cb * t)) & 0xff;
    };
    return (channel(16) << 16) | (channel(8) << 8) | channel(0) | (b & 0xff000000u);
}

// a panel unrolling downwards over 18 updates (y scale = clock / 18), centred
void record_unroll_draw(Ui& ui, int id, float x, float y, float fade, int blend) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = static_cast<float>(sprite_width(ui, id)) * 0.5f + x;
    s.pos[1] = static_cast<float>(sprite_height(ui, id)) * 0.5f + y;
    s.blend = blend;
    s.scale[1] = fade;
    s.flags |= 8;
    sprite_submit(ui, s);
}

namespace {

constexpr std::uint32_t gold = 0xffffd300;
constexpr int first_ranker = 0x31;
constexpr int rocket_summer = 0xe; // Shinjuku's Area Ranker, hidden until trigger row 10 plays
// the First Ranker's trophy (record_tournament_page's literal words 0xBEDF66F3, 0x3F7FFFEF..)
constexpr float first_ranker_pitch = -0.436330140f;
constexpr float first_ranker_at[3] = {0.99999899f, -1.86000001f, -2.71000004f};
// the second won mark's offsets (D_0019CA64 on the grids, D_0019B4E4 on the First Ranker's card)
constexpr float won_mark_grid_dx = 70.0f;
constexpr float won_mark_card_dx = 75.0f;

int cadence(const Ui& ui) {
    return ui.frame_cadence;
}

// a row moved right and recoloured
// decomp: record_row_draw 0x33116
void row_draw(Ui& ui, SpriteSlot row, int dx, std::uint32_t color) {
    row.x += static_cast<float>(dx);
    row.color = color;
    sprite_row_draw(ui, row);
}

int area_tournament(Ui& ui, int page, int index) {
    const UiTable* list = ui_table(ui.data, "record", "record_area_tournaments");
    if (!list || page < 0 || page >= static_cast<int>(list->strings.size())) {
        return -1;
    }
    return table_int(ui, "record", list->strings[static_cast<std::size_t>(page)], index);
}

int area_card(Ui& ui, int page, int index) {
    const UiTable* list = ui_table(ui.data, "record", "record_area_cards");
    if (!list || page < 0 || page >= static_cast<int>(list->strings.size())) {
        return -1;
    }
    return table_int(ui, "record", list->strings[static_cast<std::size_t>(page)], index);
}

bool won(const FrontEnd& fe, int tournament) {
    return tournament >= 0 && tournament_won(fe.world->story, tournament);
}

int portrait(const RecordScreen& r, int tournament) {
    return tournament >= 0 && tournament < static_cast<int>(r.portrait.size())
               ? r.portrait[static_cast<std::size_t>(tournament)]
               : -1;
}

int scoobee(const RecordScreen& r, int tournament) {
    return tournament >= 0 && tournament < static_cast<int>(r.scoobee.size())
               ? r.scoobee[static_cast<std::size_t>(tournament)]
               : -1;
}

void name_draw(Ui& ui, const RecordScreen& r, int tournament) {
    if (tournament >= 0 && tournament < static_cast<int>(r.names.size())) {
        text_draw(ui, r.names[static_cast<std::size_t>(tournament)], 0xe3, 0xc6, 1, 18, 18,
                  0xffffffff, 0x80);
    }
}

// record_model_draw: queued for the 3D (record_trophy_models[model] at a point)
template <class T> void trophy(RecordScreen& r, int model, const T* at, float pitch) {
    RecordTrophy t;
    t.model = model;
    t.at[0] = static_cast<float>(at[0]);
    t.at[1] = static_cast<float>(at[1]);
    t.at[2] = static_cast<float>(at[2]);
    t.pitch = pitch;
    t.spin = r.spin;
    r.trophies.push_back(t);
}

// the five page labels (record_sprites_3 rows) with the cursor's lit copy (record_sprites_4),
// dimming to 0xFF404040 over 18 updates on a grid
void grid_labels_draw(Ui& ui, const RecordScreen& r, const std::vector<SpriteSlot>& rows) {
    for (int i = 0; i < 5 && static_cast<std::size_t>(i + 6) < rows.size(); ++i) {
        if (r.page == i) {
            const SpriteSlot& lit = rows[static_cast<std::size_t>(6 + i)];
            sprite_draw_at(ui, lit.sprite, lit.x, lit.y, lit.blend, gold);
        }
        const float t = static_cast<float>(r.grid_clock) /
                        static_cast<float>(static_cast<int>(0.3f * cadence(ui)));
        row_draw(ui, rows[static_cast<std::size_t>(i)], 0, record_color_lerp(0xffffffff, t, 0xff404040));
    }
}

void cell_rects(Ui& ui, int cols, PointerRect* out) {
    for (int c = 0; c < cols; ++c) {
        for (int row = 0; row < 2; ++row) {
            out[row * cols + c] = sprite_rect(ui, 2215, static_cast<float>(194 + 62 * c),
                                              static_cast<float>(232 + 62 * row));
        }
    }
}

} // namespace

// decomp: record_percent 0x334D6
float record_percent(FrontEnd& fe, Ui& ui) {
    const Story& story = fe.world->story;
    const UiTable* list = ui_table(ui.data, "record", "record_percent_tournaments");
    unsigned won = 0, total = 0;
    for (; list && total < list->values.size() && list->values[total] != -1; ++total) {
        if (tournament_won(story, static_cast<int>(list->values[total]))) {
            ++won;
        }
    }
    const float a =
        static_cast<float>(event_done_count(story, fe.story_tables)) + static_cast<float>(won);
    return a /
           (static_cast<float>(event_once_count(fe.story_tables)) + static_cast<float>(total)) *
           100.0f;
}

// decomp: record_tournament_page 0x343D1
void record_tournament_page(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    record_header_draw(fe, ui);
    const std::vector<SpriteSlot> rows = ui_slot_run(ui, "record", "record_sprites_3");
    PointerRect rects[5];
    // the five labels slide in from the left, row 0 in 15 updates, row 4 in 30
    for (int i = 0, w = 0xa0; i < 5 && static_cast<std::size_t>(6 + i) < rows.size();
         ++i, w -= 0x28) {
        const float t = static_cast<float>(r.clock) /
                        static_cast<float>(static_cast<int>(static_cast<float>(cadence(ui)) * 0.5f));
        int x = w - static_cast<int>((t - 1.0f) * -320.0f);
        x = x > 0 ? 0 : x;
        if (r.page == i) {
            row_draw(ui, rows[static_cast<std::size_t>(6 + i)], x, gold);
        }
        row_draw(ui, rows[static_cast<std::size_t>(i)], x, 0xffffffff);
        const SpriteSlot& label = rows[static_cast<std::size_t>(i)];
        rects[i] = sprite_rect(ui, label.sprite, label.x, label.y);
    }
    float fade = static_cast<float>(r.clock) / static_cast<float>(static_cast<int>(0.3f * cadence(ui)));
    fade = fade > 1.0f ? 1.0f : fade;
    const SpriteSlot* panel = ui_slots(ui.data, "record", "record_tournament_page_sprites_2");
    if (panel) {
        record_unroll_draw(ui, panel[0].sprite, 195, 97, fade, 0x106);
        sprite_row_draw(ui, panel[1]);
    }
    if (++r.clock > 0xffffff) {
        r.clock = 0xffffff;
    }
    if (r.clock <= static_cast<int>(0.3f * cadence(ui))) {
        return;
    }
    // the First Ranker's card: Yuki and Kudryavka shown whether won or not
    std::vector<SpriteSlot> card = ui_slot_run(ui, "record", "record_tournament_page_sprites");
    if (card.size() >= 2) {
        card[0].sprite = portrait(r, first_ranker);
        card[1].sprite = scoobee(r, first_ranker);
        sprite_rows_draw(ui, card.data());
    }
    if (won(fe, first_ranker)) {
        trophy(r, 0, first_ranker_at, first_ranker_pitch);
        r.spin += 0.02f;
        const SpriteSlot* mark = ui_slots(ui.data, "record", "record_sprites_9", 1);
        if (mark) {
            sprite_row_draw(ui, *mark);
            sprite_row_offset(ui, *mark, won_mark_card_dx, 0);
        }
    }
    if (menu_cursor(ui, r.page, 0, 5, 0x16, 0, 3, rects) != 0) {
        return;
    }
    if (menu_cancel(ui)) {
        if (r.page != 4) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        r.page = 4;
    } else if (menu_accept(ui, &rects[r.page])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        r.col = r.row = r.side_col = r.side_row = r.grid_clock = r.clock = 0;
        fe.screen.step = r.page < 3    ? record_step_area_grid
                         : r.page == 3 ? record_step_side_grid
                                       : record_step_menu;
    }
}

void record_area_grid(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    const int page = r.page;
    record_header_draw(fe, ui);
    const std::vector<SpriteSlot> rows = ui_slot_run(ui, "record", "record_sprites_3");
    if (rows.size() > 12) {
        sprite_rows_draw(ui, &rows[12]);
    }
    const SpriteSlot* area_plate = ui_slots(ui.data, "record", "record_sprites_7", page);
    if (area_plate) {
        sprite_row_draw(ui, *area_plate);
    }
    grid_labels_draw(ui, r, rows);
    if (r.grid_clock < 0xffffff) {
        ++r.grid_clock;
    }
    float fade =
        static_cast<float>(r.grid_clock) / static_cast<float>(static_cast<int>(0.3f * cadence(ui)));
    fade = fade >= 1.0f ? 1.0f : fade;
    record_unroll_draw(ui, 0x89e, 183, 93, fade, 4);
    if (fade < 1.0f) {
        return;
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_sprites_8", 4));
    std::vector<SpriteSlot> cursor = ui_slot_run(ui, "record", "record_sprites_5");
    if (cursor.size() >= 2) {
        if (r.col < 4) {
            cursor[0].x = static_cast<float>(r.col * 0x3e) + 194.0f;
            cursor[0].y = static_cast<float>(r.row * 0x3e) + 232.0f;
            sprite_row_draw(ui, cursor[0]);
        } else {
            sprite_row_draw(ui, cursor[1]);
        }
    }
    // the column and row headers, or the area's ranker label
    if (rows.size() > 17) {
        SpriteSlot head = rows[16];
        if (r.col == 4) {
            head.sprite = table_int(ui, "record", "record_ranker_labels", page);
        } else {
            head.sprite = table_int(ui, "record", "record_grid_row_labels", r.row);
            SpriteSlot cls = rows[17];
            cls.sprite = table_int(ui, "record", "record_grid_class_labels", r.col + page * 4);
            sprite_row_draw(ui, cls);
        }
        sprite_row_draw(ui, head);
    }
    const int index = r.col != 4 ? r.col + r.row * 4 : 8;
    const int tournament = area_tournament(ui, page, index);
    if (won(fe, tournament)) {
        const SpriteSlot* mark = ui_slots(ui.data, "record", "record_sprites_9");
        if (mark) {
            sprite_row_draw(ui, *mark);
            sprite_row_offset(ui, *mark, won_mark_grid_dx, 0);
        }
    }
    std::vector<SpriteSlot> faces = ui_slot_run(ui, "record", "record_sprites_6");
    std::vector<SpriteSlot> card = ui_slot_run(ui, "record", "record_sprites_11");
    if (faces.size() >= 2 && !card.empty()) {
        // the spoiler guard: Rocket Summer's ranker stays hidden until trigger row 10 has played
        if (tournament == rocket_summer && !event_done(fe.world->story, 10)) {
            faces[0].sprite = 0x338;
            faces[1].sprite = 0x2ff;
            card[0].sprite = 0x8ba;
        } else {
            faces[0].sprite = portrait(r, tournament);
            faces[1].sprite = scoobee(r, tournament);
            card[0].sprite = area_card(ui, page, index);
        }
        sprite_row_draw(ui, card[0]);
        sprite_rows_draw(ui, faces.data());
    }
    // a trophy and a plate on every won cell
    const std::vector<double> points = ui_values(ui.data, "record", "record_area_trophy_points", 30);
    const std::vector<SpriteSlot> plates = ui_slot_run(ui, "record", "record_sprites_10");
    for (int c5 = 0; c5 < 5; ++c5) {
        for (int c = 0; c < 2; ++c) {
            if (c5 == 4 && c == 0) {
                continue;
            }
            const int id = area_tournament(ui, page, c5 != 4 ? c * 4 + c5 : 8);
            if (!won(fe, id) || plates.size() < 2) {
                continue;
            }
            const double* at = &points[static_cast<std::size_t>((c * 5 + c5) * 3)];
            if (c5 == 4) {
                trophy(r, page + 1, at, 0.0f);
                sprite_row_draw(ui, plates[1]);
            } else {
                trophy(r, 4, at, 0.0f);
                sprite_row_offset(ui, plates[0], static_cast<float>(c5 * 0x3e),
                                  static_cast<float>(c * 0x3e));
            }
        }
    }
    r.spin += 0.02f;
    name_draw(ui, r, tournament);
    // input: cancel, the row (sound 3), else the column (sound 4); the pointer picks a cell
    PointerRect cells[10];
    cell_rects(ui, 4, cells);
    cells[8] = sprite_rect(ui, 2217, 442, 232);
    cells[9] = cells[8];
    const int hover = pointer_hover(ui, cells, 9);
    const int hover_col = hover == 8 ? 4 : hover % 4;
    const int hover_row = hover == 8 ? r.row : hover / 4;
    if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = record_step_tournaments;
    } else if (hover >= 0 && (hover_col != r.col || hover_row != r.row)) {
        ui_sound(ui, bank_sys,
                 hover_row != r.row ? se_menu_cursor_vertical : se_menu_cursor_horizontal);
        r.col = hover_col;
        r.row = hover_row;
        menu_input_mark(ui);
    } else if (menu_cursor(ui, r.row, 0, 2, 0x16, 0, 3) == 0) {
        menu_cursor(ui, r.col, 0, 5, 0x17, 0, 4);
    }
}

void record_side_grid(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    record_header_draw(fe, ui);
    const SpriteSlot* plate = ui_slots(ui.data, "record", "record_sprites_7", 3);
    if (plate) {
        sprite_row_draw(ui, *plate);
    }
    const std::vector<SpriteSlot> rows = ui_slot_run(ui, "record", "record_sprites_3");
    grid_labels_draw(ui, r, rows);
    if (r.grid_clock < 0xffffff) {
        ++r.grid_clock;
    }
    // the sheet turn: the panel closes and opens over 0.3 s (D_0019B4D8)
    const float c = static_cast<float>(cadence(ui));
    const int half = static_cast<int>(0.3f * c) / 2;
    float t = r.flip > half ? static_cast<float>(r.flip - static_cast<int>(c * -0.3f) / -2) /
                                  static_cast<float>(half)
                            : 1.0f - static_cast<float>(r.flip) / static_cast<float>(half);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const SpriteSlot* sheet = ui_slots(ui.data, "record", "record_tournament_grid6_sprites");
    if (sheet) {
        record_unroll_draw(ui, sheet[0].sprite, 172, 231, t, 4);
    }
    float fade =
        static_cast<float>(r.grid_clock) / static_cast<float>(static_cast<int>(0.3f * cadence(ui)));
    fade = fade >= 1.0f ? 1.0f : fade;
    record_unroll_draw(ui, 0x89e, 183, 93, fade, 4);
    if (fade < 1.0f) {
        return;
    }
    const std::vector<SpriteSlot> arrows = ui_slot_run(ui, "record", "record_tournament_grid6_sprites_2");
    if (arrows.size() < 6) {
        return;
    }
    if (r.flip > 0) {
        sprite_row_draw(ui, arrows[2]);
        sprite_row_draw(ui, arrows[3]);
        --r.flip;
        return;
    }
    sprite_row_draw(ui, arrows[0]);
    sprite_row_draw(ui, arrows[1]);
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_sprites_8", 4));
    std::vector<SpriteSlot> cursor = ui_slot_run(ui, "record", "record_sprites_5");
    if (!cursor.empty()) {
        cursor[0].x = static_cast<float>(r.side_col * 0x3e) + 194.0f;
        cursor[0].y = static_cast<float>(r.side_row * 0x3e) + 232.0f;
        sprite_row_draw(ui, cursor[0]);
    }
    if (rows.size() > 16) {
        SpriteSlot head = rows[16];
        head.sprite = 0x877;
        sprite_row_draw(ui, head);
    }
    const std::vector<double> sides = ui_values(ui.data, "record", "record_side_tournaments", 26);
    auto side = [&](int cell) {
        return cell >= 0 && cell < 26 ? static_cast<int>(sides[static_cast<std::size_t>(cell)])
                                      : -1;
    };
    const int at = r.side_col + (r.side_row + (r.side_a ? 0 : 2)) * 6;
    const int tournament = side(at);
    if (tournament != -1) {
        name_draw(ui, r, tournament);
        std::vector<SpriteSlot> card = ui_slot_run(ui, "record", "record_sprites_11");
        std::vector<SpriteSlot> faces = ui_slot_run(ui, "record", "record_sprites_6");
        if (!card.empty() && faces.size() >= 2) {
            card[0].sprite = table_int(ui, "record", "record_side_cards", at);
            sprite_row_draw(ui, card[0]);
            // cell 13, "Enemy Drones Only", shows the drone picture
            faces[0].sprite = at == 0xd ? 0x882 : portrait(r, tournament);
            faces[1].sprite = scoobee(r, tournament);
            if (faces[0].sprite != -1) {
                sprite_row_draw(ui, faces[0]);
            }
            if (faces[1].sprite != -1) {
                sprite_row_draw(ui, faces[1]);
            }
        }
        if (won(fe, tournament)) {
            const SpriteSlot* mark = ui_slots(ui.data, "record", "record_sprites_9");
            if (mark) {
                if (at != 0xd) {
                    sprite_row_draw(ui, *mark);
                }
                sprite_row_offset(ui, *mark, won_mark_grid_dx, 0);
            }
        }
    }
    const std::vector<double> points = ui_values(ui.data, "record", "record_side_trophy_points", 36);
    const std::vector<SpriteSlot> plates = ui_slot_run(ui, "record", "record_sprites_10");
    for (int c6 = 0; c6 < 6; ++c6) {
        for (int k = 0; k < 2; ++k) {
            const int cell = k * 6 + c6 + (r.side_a ? 0 : 0xc);
            const float dx = static_cast<float>(c6 * 0x3e), dy = static_cast<float>(k * 0x3e);
            if (side(cell) == -1) {
                sprite_row_offset(ui, arrows[5], dx, dy);
                if (!plates.empty()) {
                    sprite_row_offset(ui, plates[0], dx, dy);
                }
            } else if (won(fe, side(cell))) {
                trophy(r, table_int(ui, "record", "record_side_trophy_kinds", cell % 12, 5),
                       &points[static_cast<std::size_t>((cell % 12) * 3)], 0.0f);
                if (!plates.empty()) {
                    sprite_row_offset(ui, plates[0], dx, dy);
                }
            }
        }
    }
    r.spin += 0.02f;
    // input: cancel; the row then the column, silent; off either end of the columns, LT or RT,
    // or a click on an arrow turns the sheet; a move onto an empty cell is undone
    const int col_was = r.side_col, row_was = r.side_row;
    int h = 0, v = 0;
    auto turn = [&]() {
        r.side_col = r.side_row = 0;
        r.flip = static_cast<int>(static_cast<float>(cadence(ui)) * 0.3f);
        r.side_a = !r.side_a;
    };
    PointerRect cells[12];
    cell_rects(ui, 6, cells);
    const PointerRect left = sprite_rect(ui, arrows[0].sprite, arrows[0].x, arrows[0].y);
    const PointerRect right = sprite_rect(ui, arrows[1].sprite, arrows[1].x, arrows[1].y);
    const int hover = pointer_hover(ui, cells, 12);
    if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = record_step_tournaments;
        r.side_a = true;
    } else if (ui.pointer.left && (pointer_inside(ui.pointer, left) ||
                                   pointer_inside(ui.pointer, right))) {
        turn();
    } else if (hover >= 0 && (hover % 6 != r.side_col || hover / 6 != r.side_row)) {
        v = hover / 6 != r.side_row ? 1 : 0;
        h = v ? 0 : 1;
        r.side_col = hover % 6;
        r.side_row = hover / 6;
        menu_input_mark(ui);
    } else if ((v = menu_cursor_move(ui.pad, r.side_row, 0, 2, 0x16)) == 0) {
        h = menu_cursor_move(ui.pad, r.side_col, 0, 6, 0x17);
        if (h != 0) {
            if ((h > 0 && r.side_col == 0) || (h < 0 && r.side_col == 5)) {
                turn();
            }
        } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::All, 0x4000) ||
                   pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::All, 0x8000)) {
            turn();
        }
    }
    if (h != 0 || v != 0) {
        menu_input_mark(ui);
    }
    if (side(r.side_col + (r.side_row + (r.side_a ? 0 : 2)) * 6) == -1) {
        r.side_col = col_was;
        r.side_row = row_was;
    } else if (h != 0) {
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    } else if (v != 0) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
    }
}
