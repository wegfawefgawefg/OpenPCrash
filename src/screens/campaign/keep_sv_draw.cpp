// KEEP SV's drawing (keep_sv_screen_draw, keep_sv_row_draw): the header, the
// sliding row of kept machines with the cursor's details, the side menu and the boxes; and the
// port's mouse on that art.
#include "audio/sound_cues.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/campaign/keep_sv.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/front_end.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

using namespace keep_sv_states;

namespace {

// D_002002F8: keep_sv_sprites from row 4 on
constexpr int row_base = 4;

SpriteSlot row_of(Ui& ui, int k) {
    return slot_at(ui, "keep_sv", "keep_sv_sprites", row_base + k);
}

// anchored about its middle, cut to the list's window (56..584)
// decomp: SpriteRow::draw_clipped 0xBBF09
void row_clipped(Ui& ui, SpriteSlot row, float x, float y, float scale) {
    if (row.sprite < 0 || scale <= 0) {
        return;
    }
    const float w = static_cast<float>(sprite_width(ui, row.sprite));
    const float h = static_cast<float>(sprite_height(ui, row.sprite));
    Sprite s = sprite_of(ui, row.sprite);
    s.pos[0] = x + (1.0f - scale) * w * 0.5f;
    s.pos[1] = y + (1.0f - scale) * h * 0.5f;
    s.scale[0] = s.scale[1] = scale;
    s.blend = row.blend;
    s.color = row.color;
    s.z = row.z;
    s.flags |= row.flags;
    sprite_submit_clipped(ui, s, 56.0f, 0.0f, 584.0f, 480.0f);
}

} // namespace

namespace {

// the art inside the sprites the mouse hits (their see-through margins: left, top, right, bottom)
constexpr float button_art[4] = {33, 3, 35, 7}; // SAVE and EXIT, 192 x 40
constexpr float answer_art[4] = {3, 2, 4, 2};   // a box's answers, 96 x 24
constexpr float arrow_art[4] = {3, 3, 3, 9};    // the lit scroll arrows, 40 x 48
// keep_sv_sprites rows (from 4 on): SAVE, EXIT, the scroll arrows lit
constexpr int save_row = 23, exit_row = 25, arrow_left_row = 20, arrow_right_row = 22;

} // namespace

PointerRect keep_sv_place_rect(Ui& ui, int n) {
    // D_00200720's place, the plate's slanted art (4..118 of 128) cut to the places' 95 pitch
    const std::vector<double> places = ui_values(ui.data, "keep_sv", "keep_sv_row_places", 14);
    if (places.size() < 14 || n < 0 || n >= 7) {
        return {};
    }
    const float x = static_cast<float>(places[static_cast<std::size_t>(n) * 2]);
    const float y = static_cast<float>(places[static_cast<std::size_t>(n) * 2 + 1]);
    return {x + 14.0f, y + 4.0f, x + 109.0f, y + 70.0f};
}

PointerRect keep_sv_row_rect(Ui& ui, int k, const float art[4]) {
    const SpriteSlot row = row_of(ui, k);
    const PointerRect whole = sprite_rect(ui, row.sprite, row.x, row.y);
    return {whole.left + art[0], whole.top + art[1], whole.right - art[2], whole.bottom - art[3]};
}

PointerRect keep_sv_side_rect(Ui& ui, int item) {
    return keep_sv_row_rect(ui, item == 1 ? save_row : exit_row, button_art);
}

// the port's mouse over the list and the side menu: a place under it takes the list's cursor
// (sound 4), SAVE or EXIT the side menu's (sound 3); a click on a scroll arrow, or the wheel over
// the list, scrolls it as left and right do
void keep_sv_pointer_step(Ui& ui, KeepSvScreen& k) {
    if (k.scroll != 0) {
        return;
    }
    const PointerRect list = {56, 193, 584, 263};
    const auto press = [&](std::uint16_t bit) {
        ui.pad.b.cur = static_cast<std::uint16_t>(ui.pad.b.cur & ~(pad_dpad_up | pad_dpad_down));
        ui.pad.b.rising =
            static_cast<std::uint16_t>((ui.pad.b.rising & ~(pad_dpad_up | pad_dpad_down)) | bit);
        ui.pad.b.repeat =
            static_cast<std::uint16_t>((ui.pad.b.repeat & ~(pad_dpad_up | pad_dpad_down)) | bit);
        ui.pointer.left = false;
        k.state = s_list;
        k.at.menu = 0;
    };
    if ((ui.pointer.left &&
         pointer_inside(ui.pointer, keep_sv_row_rect(ui, arrow_left_row, arrow_art))) ||
        (ui.pointer.wheel > 0 && pointer_inside(ui.pointer, list))) {
        press(pad_dpad_left);
    } else if ((ui.pointer.left &&
                pointer_inside(ui.pointer, keep_sv_row_rect(ui, arrow_right_row, arrow_art))) ||
               (ui.pointer.wheel < 0 && pointer_inside(ui.pointer, list))) {
        press(pad_dpad_right);
    }
    if (!ui.pointer.moved) {
        return;
    }
    for (int n = 1; n <= keep_sv_rows; ++n) {
        if (pointer_inside(ui.pointer, keep_sv_place_rect(ui, n)) &&
            (k.at.menu != 0 || k.at.cursor != n - 1)) {
            k.at.cursor = n - 1;
            k.at.menu = 0;
            k.state = s_list;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            menu_input_mark(ui);
            return;
        }
    }
    for (int item = 1; item <= 2; ++item) {
        if (pointer_inside(ui.pointer, keep_sv_side_rect(ui, item)) && k.at.menu != item) {
            k.at.menu = item;
            k.at.chosen = -1;
            k.state = item == 1 ? s_save_item : s_exit_item;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            menu_input_mark(ui);
            return;
        }
    }
}

// the answers drawn as the screen's own sprites (keep_sv_draw's rows), or the box's two
void keep_sv_answer_rects(Ui& ui, KeepSvScreen& k, PointerRect out[3]) {
    const auto rows = [&](std::initializer_list<int> list) {
        int i = 0;
        for (const int r : list) {
            out[i++] = keep_sv_row_rect(ui, r, answer_art);
        }
    };
    switch (k.state) {
    case s_kept:
        rows({31, 33, 35});
        break;
    case s_damaged:
        rows({33, 35});
        break;
    case s_save_ask:
        rows({27, 29});
        break;
    case s_delete_ask:
        msg_box_choice_rects(ui, k.boxes[3], out);
        break;
    case s_overwrite:
        msg_box_choice_rects(ui, k.boxes[4], out);
        break;
    default:
        break;
    }
}

const VsSave* keep_sv_slot(const KeepSvScreen& k, int n) {
    const int i = k.at.top - 1 + n;
    return i >= 0 && i < static_cast<int>(k.saves.size()) ? &k.saves[static_cast<std::size_t>(i)]
                                                          : nullptr;
}

namespace {

// keep_sv_row_draw: the seven places (two off the window for the slide), each kept machine as its
// body class's plate, fading in one after another; the cursor's lit, with its frame and details
void rows_draw(FrontEnd& fe, Ui& ui, KeepSvScreen& k) {
    float fade[7];
    const float t = k.clock - 26.0f;
    for (int n = 0; n < 7; ++n) {
        const float start = static_cast<float>(n) * 7.0f;
        fade[n] = start <= t && start + 7.0f > t ? (t - start) / 7.0f : start > t ? 0.0f : 1.0f;
    }
    float shift = 0.0f;
    if (k.scroll != 0) {
        float s = 1.0f - k.scroll_clock * 0.25f;
        s = k.scroll == 3 ? -s : k.scroll == 4 ? 1.0f - s : k.scroll == 2 ? -(1.0f - s) : s;
        shift = s * 47.5f;
    }
    if (k.scroll_clock <= 0.0f) {
        k.scroll = 0;
    }
    // D_00200720: the places' x and y
    const std::vector<double> places = ui_values(ui.data, "keep_sv", "keep_sv_row_places", 14);
    for (int n = 0; n < 7 && places.size() >= 14; ++n) {
        const float x = static_cast<float>(places[static_cast<std::size_t>(n) * 2]);
        const float y = static_cast<float>(places[static_cast<std::size_t>(n) * 2 + 1]);
        const VsSave* save = keep_sv_slot(k, n);
        int id = 0x10; // damaged
        if (!save) {
            if (n < 1 || n >= 6) {
                continue;
            }
            id = 2; // an empty place
        } else if (save->present) {
            const BodyPart* body = body_part(fe.shop.tables, save->build.body);
            static constexpr int by_class[6] = {6, 8, 4, 0xc, 0xe, 0xa};
            const int c = body ? body->body_class : 0;
            if (c < 0 || c > 5) {
                return;
            }
            id = by_class[c];
        }
        row_clipped(ui, row_of(ui, id), shift + x, y, fade[n]);
        if (k.at.cursor + 1 == n && k.at.menu == 0) {
            SpriteSlot lit = row_of(ui, id + 1);
            lit.flags = k.state == s_list ? (lit.flags | sprite_blink_soft)
                                          : (lit.flags & ~sprite_blink_soft);
            if (k.scroll == 0) {
                row_clipped(ui, lit, shift + x, y, fade[n]);
                row_clipped(ui, row_of(ui, 18), x, y, fade[n]);
            }
        }
    }
    // the details of the one under the cursor: its name, and when it was kept in local time
    // (date_draw and time_hms_draw in number font 0)
    const int s = k.state;
    if (s == s_exit_item || s == s_save_item || s == s_save_ask || s == s_overwrite ||
        s == s_saved) {
        return;
    }
    const VsSave* save = keep_sv_slot(k, k.at.cursor + 1);
    if (!save || !save->present) {
        return;
    }
    const std::uint32_t white = 0xfff0f0f0;
    text_draw(ui, glyph_name_text(save->mech, glyph_name_length), 0xbd, 0x14e, 0, 0x12, 0x12,
              white, 9);
    const std::time_t when = static_cast<std::time_t>(save->written);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    date_draw(ui, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, 0xbd, 0x176, 0x101, white,
              0);
    time_hms_draw(ui, local.tm_hour, local.tm_min, local.tm_sec, 0x116, 0x176, 0x101, white, 0);
}

// keep_sv_screen_draw
} // namespace

void keep_sv_draw(FrontEnd& fe, Ui& ui, KeepSvScreen& k) {
    const World& w = *fe.world;
    day_draw(ui, w, 0x108);
    money_draw(ui, w, 0x108);
    garage_banner_draw(ui, w, k.banner_timer, 0x67, 0x102);
    if (k.clock < keep_sv_clock_end) {
        k.clock += static_cast<float>(60 / ui.frame_cadence);
    }
    k.clock = std::min(k.clock, keep_sv_clock_end);
    const float head_fade = k.clock <= 16.0f ? k.clock / 16.0f : 1.0f;
    const float list_fade = k.clock < 16.0f    ? 0.0f
                            : k.clock <= 26.0f ? (k.clock - 16.0f) * 0.1f
                                               : 1.0f;
    sprite_rows_draw(ui, ui_slots(ui.data, "keep_sv", "keep_sv_sprites"));
    text_strip_draw_scroll(ui, k.text, 0xfff0f0f0);
    rows_draw(fe, ui, k);
    row_anchored(ui, row_of(ui, 0), head_fade, head_fade);
    row_anchored(ui, row_of(ui, 1), 1.0f, list_fade);
    if (k.state == s_exit_item) {
        sprite_row_draw(ui, row_of(ui, 26));
    }
    sprite_row_draw(ui, row_of(ui, 25));
    if (k.state == s_save_item) {
        sprite_row_draw(ui, row_of(ui, 24));
    }
    sprite_row_draw(ui, row_of(ui, 23));
    if (k.at.top > 0 && k.state == s_list) {
        row_anchored(ui, row_of(ui, 20), 1.0f, list_fade);
    }
    if (static_cast<int>(k.saves.size()) > k.at.top + keep_sv_rows && k.state == s_list) {
        row_anchored(ui, row_of(ui, 22), 1.0f, list_fade);
    }
    row_anchored(ui, row_of(ui, 19), 1.0f, list_fade);
    row_anchored(ui, row_of(ui, 21), 1.0f, list_fade);
    // the boxes, and the answers drawn as the screen's own sprites
    const auto answers = [&](std::initializer_list<int> rows, int lit_index) {
        int i = 0;
        for (const int r : rows) {
            if (i == lit_index) {
                sprite_row_draw(ui, row_of(ui, r + 1));
            }
            sprite_row_draw(ui, row_of(ui, r));
            ++i;
        }
    };
    switch (k.state) {
    case s_kept:
        msg_box_update(ui, k.boxes[1]);
        if (k.boxes[1].row.done) {
            answers({31, 33, 35}, k.sel);
        }
        break;
    case s_damaged:
        msg_box_update(ui, k.boxes[10]);
        if (k.boxes[10].row.done) {
            answers({33, 35}, k.sel);
        }
        break;
    case s_save_ask:
        msg_box_update(ui, k.boxes[0]);
        if (k.boxes[0].row.done) {
            answers({27, 29}, k.sel);
        }
        break;
    case s_saved:
        msg_box_update_ok(ui, k.boxes[2]);
        break;
    case s_delete_ask:
        msg_box_update_choice(ui, k.boxes[3], k.sel);
        break;
    case s_overwrite:
        msg_box_update_choice(ui, k.boxes[4], k.sel);
        break;
    case s_read_failed:
        msg_box_update_ok(ui, k.boxes[5]);
        break;
    case s_save_failed:
        msg_box_update_ok(ui, k.boxes[6]);
        break;
    case s_delete_failed:
        msg_box_update_ok(ui, k.boxes[7]);
        break;
    case s_none_to_save:
        msg_box_update(ui, k.boxes[14]);
        break;
    default:
        break;
    }
}
