// VS setup's area page (decomp docs/port/screens.md "Area page"; step10 0x4112C,
// vs_area_select_draw, area_strip_draw, area_plate_play): every player
// moves one cursor, the day or night box, Random, and the picture's TV-static switch.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/area_plate.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/vs/vs_setup.hpp"
#include "talk/draw2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// the area page's labels slide from D_0019EAC8 to D_0019EAE0 over 24 ticks, or the last three to
// D_0019EB04 for three or four players, x at y 371
constexpr unsigned label_from = 0x19EAC8;
constexpr unsigned label_to = 0x19EAE0;
constexpr unsigned label_to_three = 0x19EB04;

// AreaIds (D_001FFCA8): a picture by day, by night, the name and the title of an area index
int area_id(Ui& ui, int area, int k) {
    return static_cast<int>(vs_number(ui, 0x1FFCA8 + 16u * static_cast<unsigned>(area) +
                                           4u * static_cast<unsigned>(k)));
}

int entrants(const FrontEnd& fe) {
    return static_cast<int>(std::count(fe.vs.entered.begin(), fe.vs.entered.end(), true));
}

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

// the area labels as they stand in their slide (ids from D_001FFC8C, y 371), for the pointer
void label_rects(FrontEnd& fe, Ui& ui, int first, PointerRect* out) {
    for (int k = first; k < 6; ++k) {
        const int id = static_cast<int>(vs_number(ui, 0x1FFC8C + 4u * static_cast<unsigned>(k)));
        out[k - first] =
            sprite_rect(ui, id, static_cast<float>(fe.vs.labels.pos[k - first]), 371.0f);
    }
}

// the day and night answers (rows 114, 116)
void answer_rects(Ui& ui, PointerRect out[2]) {
    for (int k = 0; k < 2; ++k) {
        const SpriteSlot* slot = vs_row(ui, 114 + 2 * k);
        out[k] = slot ? sprite_rect(ui, slot->sprite, slot->x, slot->y) : PointerRect{};
    }
}

} // namespace

// step10: the area, by every player in turn; the first accept or cancel ends the pass
void vs_area_step(FrontEnd& fe, Ui& ui) {
    VsSession& vs = fe.vs;
    const int count = entrants(fe);
    const int first = count > 2 ? 3 : 0;
    PointerRect labels[6], answers[2];
    label_rects(fe, ui, first, labels);
    answer_rects(ui, answers);
    for (int i = 0; i < vs_panel_count; ++i) {
        const PadRecord* pad = vs_seat_pad(fe, ui, i);
        if (!pad) {
            continue;
        }
        if (vs.area_phase == 0) {
            const int before = vs.area;
            const int hover = vs_hover(fe, ui, i, labels, 6 - first);
            if (menu_cursor_move(*pad, vs.area, first, 6 - first, cursor_menu_horizontal) != 0 ||
                (hover >= 0 && first + hover != vs.area)) {
                vs.area = hover >= 0 ? first + hover : vs.area;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            if (vs_accept(fe, ui, i, *pad, &labels[vs.area - first]) && vs.area == vs.area_shown &&
                vs.plate_start == -1) {
                ui_sound(ui, bank_sys, se_menu_accept);
                if (vs.area == 5) {
                    fade_out_start(ui, half_second(ui));
                    vs_go(fe, vs_step_area_leave);
                } else if (vs.area == 4) {
                    // Random: any of the eight for two, the training stage by day or night for more
                    vs.area_phase = 5;
                    vs.area_picked = count <= 2 ? std::rand() % 8 : 6 + std::rand() % 2;
                } else {
                    vs.area_phase = 1;
                    vs.area_clock = 0;
                    vs.area_choice = 0;
                    vs.was_choice = 1;
                }
                break;
            }
            if (vs_cancel(fe, ui, i, *pad)) {
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                vs.area = 5;
            }
            if (vs.area != before) {
                vs.plate_start = static_cast<int>(ui.task_timer); // the picture switches
            }
            if (vs_accept(fe, ui, i, *pad, &labels[vs.area - first]) || vs_cancel(fe, ui, i, *pad)) {
                break;
            }
        } else if (vs.area_phase == 1 || vs.area_phase == 3) {
            // the box's clock moves once for every player each frame (retail's)
            vs.area_clock += 1;
            if (vs.area_clock >= 12) {
                vs.area_phase = vs.area_phase == 1 ? 2 : 0;
                if (vs.area_phase == 0) {
                    vs.area_choice = 0;
                    vs.was_choice = 0;
                }
            }
        } else if (vs.area_phase == 2) {
            const int before = vs.area_choice;
            const int hover = vs_hover(fe, ui, i, answers, 2);
            if (menu_cursor_move(*pad, vs.area_choice, 0, 2, cursor_menu_horizontal) != 0 ||
                (hover >= 0 && hover != vs.area_choice)) {
                vs.area_choice = hover >= 0 ? hover : vs.area_choice;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            if (vs.area_choice != before) {
                vs.was_choice = vs.area_choice == 0 ? 1 : 0;
                vs.plate_start = static_cast<int>(ui.task_timer);
            }
            if (vs_accept(fe, ui, i, *pad, &answers[vs.area_choice])) {
                ui_sound(ui, bank_sys, se_menu_accept);
                vs.area_phase = 4;
                vs.area_picked = vs.area * 2 + vs.area_choice;
                break;
            }
            if (vs_cancel(fe, ui, i, *pad)) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                vs.area_phase = 3;
                vs.area_clock = 0;
                if (vs.area_choice == 1) {
                    vs.area_choice = 0;
                    vs.was_choice = 1;
                    vs.plate_start = static_cast<int>(ui.task_timer);
                }
                break;
            }
        }
    }
    vs_area_draw(fe, ui);
    // the battle start: the area, the round's seats; main builds the round from them
    if (vs.step == vs_step_area && (vs.area_phase == 4 || vs.area_phase == 5)) {
        vs.selector = vs.area_picked;
        fade_out_start(ui, half_second(ui));
        vs_go(fe, vs_step_start);
    }
}

void vs_area_draw(FrontEnd& fe, Ui& ui) {
    VsSession& vs = fe.vs;
    sprite_rows_draw(ui, ui_slots(ui.data, "vs_mode", "vs_area_select_sprites"));
    text_strip_draw_scroll(ui, vs.help[2], 0xFFF0F0F0);
    // the picture (area_strip_draw, mode 0): its frame, then the plate switching or still
    sprite_draw_at(ui, 0x354, 21, 97, 0x104);
    const int count = entrants(fe);
    int now = area_id(ui, vs.area, vs.area_choice);
    if (vs.area == 4) {
        if ((ui.task_timer & 3u) == 0) {
            // the picture cycles every fourth tick (rand_float is the gameplay RNG in retail; menus
            // aren't synced, so CRT rand)
            if (count > 2) {
                vs.plate_pick = vs.plate_pick == 6 ? 7 : 6;
            } else {
                int r = vs.plate_pick;
                while (r == vs.plate_pick) {
                    r = std::rand() % 8;
                }
                vs.plate_pick = r;
            }
        }
        now = area_cycle_picture(ui, vs.plate_pick);
    }
    const int was = area_id(ui, vs.area_shown, vs.was_choice);
    bool running = false;
    if (vs.plate_start != -1) {
        running = area_plate_play(ui, now, was, vs.plate_start, 71, 115, 3, 0);
        if (!running) {
            vs.plate_start = -1;
            vs.area_shown = vs.area; // the shown area catches up once the picture is done
        }
    } else {
        sprite_draw_at(ui, now, 71, 115, 3);
    }
    const int name = area_id(ui, vs.area, 2), title = area_id(ui, vs.area, 3);
    if (running) {
        const float fade =
            static_cast<float>(ui.task_timer - static_cast<unsigned>(vs.plate_start)) / 21.0f;
        const int old_name = area_id(ui, vs.area_shown, 2), old_title = area_id(ui, vs.area_shown, 3);
        sprite_draw_at(ui, name, 41, 250, 0x105, 0xFFFFFFFF, 0, fade);
        sprite_draw_at(ui, title, 316, 111, 0x102, 0xFFFFFFFF, 0, fade);
        sprite_draw_at(ui, old_name, 41, 250, 0x106, 0xFFFFFFFF, 0, 1 - fade);
        sprite_draw_at(ui, old_title, 316, 111, 0x102, 0xFFFFFFFF, 0, 1 - fade);
    } else {
        sprite_draw_at(ui, name, 41, 250, 0x105);
        sprite_draw_at(ui, title, 316, 111, 0x102);
    }
    // the labels slide in; three or four players get only the training stage, Random and Exit
    const int first = count > 2 ? 3 : 0;
    int ids[6], targets[6];
    for (int k = first; k < 6; ++k) {
        ids[k - first] = static_cast<int>(vs_number(ui, 0x1FFC8C + 4u * static_cast<unsigned>(k)));
        targets[k - first] = static_cast<int>(
            first ? vs_number(ui, label_to_three + 4u * static_cast<unsigned>(k - first))
                  : vs_number(ui, label_to + 4u * static_cast<unsigned>(k)));
    }
    slide_draw(ui, vs.labels, 6 - first, 24, 0x102, 371, targets, ids);
    const int at = vs.labels.pos[std::clamp(vs.area - first, 0, 5)];
    const int lit = static_cast<int>(vs_number(ui, 0x1FFC74 + 4u * static_cast<unsigned>(vs.area)));
    Sprite cursor = sprite_of(ui, lit);
    cursor.pos[0] = static_cast<float>(at);
    cursor.pos[1] = 371;
    cursor.blend = 0x103;
    cursor.flags |= sprite_blink_soft;
    sprite_submit(ui, cursor);
    if (const SpriteSlot* bar = vs_row(ui, 120)) {
        SpriteSlot row = *bar;
        row.x = static_cast<float>(vs.labels.pos[5 - first] - 356);
        sprite_row_draw(ui, row);
    }
    // the day or night box: opening and closing over 12, the chosen answer pulsing
    if (vs.area_phase >= 1 && vs.area_phase <= 4) {
        float alpha = 1;
        if (vs.area_phase == 1) {
            alpha = static_cast<float>(vs.area_clock) / 12.0f;
        } else if (vs.area_phase == 3) {
            alpha = 1 - static_cast<float>(vs.area_clock) / 12.0f;
        }
        const float v = std::fabs(static_cast<float>(ui.task_timer % 60) / 60.0f - 0.5f) + 0.5f;
        const std::uint32_t pulse =
            ((static_cast<std::uint32_t>(v) << 16 | static_cast<std::uint32_t>(v * 79) << 8 |
              static_cast<std::uint32_t>(v * 255)) -
             0x15000u);
        const auto row = [&](int r, std::uint32_t colour) {
            const SpriteSlot* slot = vs_row(ui, r);
            if (!slot) {
                return;
            }
            Sprite sprite = sprite_of(ui, slot->sprite);
            sprite.pos[0] = slot->x;
            sprite.pos[1] = slot->y;
            sprite.blend = slot->blend;
            sprite.color = colour;
            sprite.z = alpha;
            sprite_submit(ui, sprite);
        };
        row(113, 0xFFFFFFFF);
        row(114, vs.area_choice == 0 ? pulse : 0xFF606060);
        row(116, vs.area_choice == 1 ? pulse : 0xFF606060);
    }
    menu_input_mark(ui);
}

void vs_labels_start(FrontEnd& fe, Ui& ui) {
    const int first = entrants(fe) > 2 ? 3 : 0;
    int start[6];
    for (int k = first; k < 6; ++k) {
        start[k - first] =
            static_cast<int>(vs_number(ui, label_from + 4u * static_cast<unsigned>(k)));
    }
    slide_init(fe.vs.labels, 6 - first, start);
    slide_start(ui, fe.vs.labels);
}
