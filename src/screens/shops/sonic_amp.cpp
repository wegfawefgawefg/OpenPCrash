// Sonic Amp, the music shop (sonic_amp_* state 0x14, decomp src/o1/u_028e51.c 0x299D1..0x2BE0B,
// docs/port/screens.md "Sonic Amp"): today's ten picks, the three charts over the catalogue,
// buying a song for 2,000 NY, listening with the artist's bio. The only way out is the leave tab,
// to the town map.
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr const char* sa = "sonic_amp";
constexpr int song_price = 200; // the literal 200 (2,000 NY), not the record's
constexpr int shop_track = 0xf; // 8DEALER.WAV

enum Step : int {
    step_load = 0,
    step_fade_in,
    step_tabs,
    step_picks,
    step_chart,
    step_message,
    step_listen,
    step_leave,
    step_done,
};

int half(const Ui& ui) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
}

// the catalogue id under the cursor
// decomp: sonic_amp_cursor_track 0x29FFF
int cursor_track(const SonicAmpScreen& s) {
    if (s.tab == 0) {
        return s.picks[s.row * 5 + s.col];
    }
    return s.col + (s.row + (s.page + s.chart * 2) * 2) * 6;
}

std::string table_line(Ui& ui, const char* name) {
    return table_text(ui, sa, name, 0);
}

// the RANK ticker ("RANK" + "01".."10" + title) and the artist strip nothing draws
void ticker_set(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const MusicRecord* r = music_record(fe.music, s.picks[s.tip]);
    std::string text = table_line(ui, "sonic_amp_rank_head");
    text += string_at(ui, 0x1de208u + static_cast<unsigned>(s.tip * 4));
    text += table_line(ui, "sonic_amp_rank_mid");
    text += r ? r->title : std::string();
    text += table_line(ui, "sonic_amp_rank_tail");
    text_strip_init(ui, s.ticker, 0x32, 0xf6, text, 0x32, 0xf6, 0x152, 0x108);
    text_strip_init(ui, s.artist, 0xa0, 0x140, r ? r->artist : std::string(), 0, 0, 0x280, 0x1e0);
}

// "《title : artist》" of the cell, or three blanks
// decomp: sonic_amp_title_set 0x2A5E2
void title_set(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const MusicRecord* r = music_record(fe.music, cursor_track(s));
    text_strip_init(ui, s.title, 0x118, 0x2d,
                    r ? music_title_text(*r) : std::string("\xe3\x80\x80\xe3\x80\x80\xe3\x80\x80"),
                    0, 0, 0x280, 0x1e0);
}

void children(FrontEnd& fe, Ui& ui) {
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// a cell's hit box: the cell's art (2440, 80 x 80, drawn from 2..72 across and 3..73 down) at
// its sonic_amp_picks_sprites / sonic_amp_chart_sprites place, picks 5 x 2 from (169, 194),
// charts 6 x 2 from (89, 194), 80 and 79 apart
PointerRect cell_rect(const SonicAmpScreen& s, int col, int row, float dy) {
    const float x = (s.tab == 0 ? 169.0f : 89.0f) + 80.0f * static_cast<float>(col);
    const float y = 194.0f + 79.0f * static_cast<float>(row) + dy;
    return {x + 2.0f, y + 3.0f, x + 72.0f, y + 73.0f};
}

// the pointer over a grid cell moves the cursor there (the d-pad's sound); true on a move
bool pointer_grid(Ui& ui, SonicAmpScreen& s, int cols, int rows, float dy) {
    PointerRect cells[12];
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            cells[r * cols + c] = cell_rect(s, c, r, dy);
        }
    }
    int cell = s.row * cols + s.col;
    if (menu_cursor(ui, cell, 0, rows * cols, cursor_no_dpad, bank_sys, se_menu_cursor_horizontal,
                    cells) == 0) {
        return false;
    }
    s.col = cell % cols;
    s.row = cell / cols;
    return true;
}

// the buy / listen / back items' hit boxes: their plates' art (96 x 24 sprites drawn from 3..92
// across and 2..22 down), 95 apart, so the neighbours no longer share a column
void menu_rects(Ui& ui, PointerRect out[3]) {
    for (int j = 0; j < 3; ++j) {
        const SpriteSlot item = slot_at(ui, sa, "sonic_amp_sprites_3", j);
        out[j] = pointer_rect_inset(sprite_rect(ui, item.sprite, item.x, item.y), 3, 2, 4, 2);
    }
}

// the menu over a picked cell: buy, listen, back (sonic_amp_picks_update's second half)
void menu_update(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    Story& story = fe.world->story;
    PointerRect rects[3];
    menu_rects(ui, rects);
    if (menu_cursor(ui, s.menu, 0, 3, 0x15, 0, 4, rects) != 0) {
        return;
    }
    const bool accept = menu_accept(ui, &rects[s.menu]);
    if (accept) {
        ui_sound(ui, bank_sys, se_menu_accept);
    }
    const int id = cursor_track(s);
    switch (s.menu) {
    case 0: // buy
        if (accept) {
            if (id < 0 || id >= music_owned_count_max) {
                break;
            }
            std::uint8_t& owned = story.music_owned[id];
            if (owned == 0) {
                // SHOP PRICES (PC CONFIG's CHEATS) divides it
                const int price = cheat_shop_price(story.cheats, song_price);
                if (story.money >= price) {
                    story.money -= price;
                    owned = 1;
                    s.message = 1;
                    ui_sound(ui, bank_sys, se_purchase);
                } else {
                    s.message = 2;
                }
            } else {
                s.message = 3;
            }
            fe.screen.step = step_message;
        } else if (menu_cancel(ui)) {
            if (s.menu != 2) {
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            s.menu = 2;
        }
        break;
    case 1: // listen: the song looped, the artist's bio
        if (accept) {
            const MusicRecord* r = music_record(fe.music, id);
            s.playing = id;
            if (r) {
                ui_music(ui, r->wave, true);
                s.bio_alive = script_start(fe, ui, s.bio_script, s.bio, r->bio);
            }
            s.listen_frames = 0;
            fe.screen.step = step_listen;
        } else if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            s.menu = 2;
        }
        break;
    default: // back
        if (accept) {
            s.in_list = true;
        }
        break;
    }
}

void back_to_tabs(Ui& ui, FrontEnd& fe, SonicAmpScreen& s) {
    s.on_leave = s.tab == 4;
    ui_sound(ui, bank_sys, se_menu_cancel);
    fe.screen.step = step_tabs;
}

// decomp: sonic_amp_picks_update 0x2AE9A
void picks_update(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    sonic_amp_screen_draw(fe, ui, s);
    sonic_amp_picks_draw(fe, ui, s);
    text_strip_draw_scroll(ui, s.title, 0xffffffff, 0x102);
    if (!s.in_list) {
        menu_update(fe, ui, s);
        return;
    }
    bool changed = menu_cursor(ui, s.col, 0, 5, 0x17, 0, 4) != 0 ||
                   menu_cursor(ui, s.row, 0, 2, 0x14, 0, 3) != 0;
    changed = pointer_grid(ui, s, 5, 2, 0.0f) || changed;
    const PointerRect cell = cell_rect(s, s.col, s.row, 0.0f);
    if (menu_cancel(ui)) {
        back_to_tabs(ui, fe, s);
    } else if (menu_accept(ui, &cell)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        s.in_list = false;
        s.menu = 1;
        s.message = 0;
    }
    if (changed) {
        title_set(fe, ui, s);
    }
}

int chart_rows(const SonicAmpScreen& s) {
    return s.tab == 3 && s.page == 2 ? 1 : 2;
}

// a page turn by d (wrapping), its arrow flashing
void page_turn(SonicAmpScreen& s, int d) {
    const int pages = (s.tab == 3) + 2;
    s.page = (s.page + d + pages) % pages;
    (d < 0 ? s.arrow_left : s.arrow_right) = 15;
    // retail keeps row 1 when it turns onto the six-song page, so an A on that frame opens the
    // menu on ids 78..83 (buying 81..83 breaks the media pages, listening reads past the
    // catalogue): the row is clamped on every turn
    if (s.row >= chart_rows(s)) {
        s.row = chart_rows(s) - 1;
    }
}

// decomp: sonic_amp_chart_update 0x2B702
void chart_update(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    sonic_amp_screen_draw(fe, ui, s);
    sonic_amp_chart_draw(fe, ui, s);
    s.arrow_left = s.arrow_left > 0 ? s.arrow_left - 1 : 0;
    s.arrow_right = s.arrow_right > 0 ? s.arrow_right - 1 : 0;
    text_strip_draw_scroll(ui, s.title, 0xffffffff, 0x102);
    if (!s.in_list) {
        menu_update(fe, ui, s);
        return;
    }
    const PadRecord pad = menu_pad_unclicked(ui);
    bool changed = false;
    if ((pad.b.rising & pad_left_trigger) != 0) {
        page_turn(s, -1);
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        changed = true;
    } else if ((pad.b.rising & pad_right_trigger) != 0) {
        page_turn(s, 1);
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        changed = true;
    } else if (s.col == 0 && (pad.b.rising & pad_dpad_left) != 0) {
        page_turn(s, -1);
        s.col = 5;
        changed = true;
    } else if (s.col == 5 && (pad.b.rising & pad_dpad_right) != 0) {
        page_turn(s, 1);
        s.col = 0;
        changed = true;
    } else {
        const int delta = menu_cursor(ui, s.col, 0, 6, 0x17, 0, 4);
        const int rows = chart_rows(s);
        if (delta != 0 || menu_cursor(ui, s.row, 0, rows, 0x14, 0, 3) != 0) {
            changed = true;
        }
        // the stick's wrap turns the page too, and ends the update
        if ((delta == -1 && s.col == 5) || (delta == 1 && s.col == 0)) {
            page_turn(s, delta);
            title_set(fe, ui, s);
            return;
        }
        // the mouse wheel turns pages as the triggers do, and so does a click on an arrow
        const SpriteSlot left = slot_at(ui, sa, "sonic_amp_chart_sprites_3", 0);
        const SpriteSlot right = slot_at(ui, sa, "sonic_amp_chart_sprites_3", 2);
        // their rows' rectangles widened 20 px: the art sits off them (the right one past 596)
        const PointerRect arrows[2] = {
            pointer_rect_inset(sprite_rect(ui, left.sprite, left.x, left.y), -20, 0, -20, 0),
            pointer_rect_inset(sprite_rect(ui, right.sprite, right.x, right.y), -20, 0, -20, 0)};
        const int arrow = ui.pointer.left ? pointer_pick(ui.pointer, arrows, 2) : -1;
        if (ui.pointer.wheel != 0 || arrow >= 0) {
            page_turn(s, arrow == 0 || (arrow < 0 && ui.pointer.wheel > 0) ? -1 : 1);
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            ui.pointer.left = false;
            changed = true;
        }
        changed = pointer_grid(ui, s, 6, chart_rows(s), chart_rows(s) == 1 ? 39.0f : 0.0f) ||
                  changed;
    }
    const PointerRect cell =
        cell_rect(s, s.col, s.row, chart_rows(s) == 1 ? 39.0f : 0.0f);
    if (menu_cancel(ui)) {
        back_to_tabs(ui, fe, s);
    } else if (menu_accept(ui, &cell)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        s.in_list = false;
        s.menu = 1;
        s.message = 0;
    }
    if (changed) {
        title_set(fe, ui, s);
    }
}

// the five tab plates' hit boxes: their art (56 x 56 sprites drawn from 3..52 each way)
void tab_rects(Ui& ui, PointerRect out[5]) {
    for (int i = 0; i < 5; ++i) {
        const SpriteSlot plate = slot_at(ui, sa, "sonic_amp_sprites", 6 + i);
        out[i] = pointer_rect_inset(sprite_rect(ui, plate.sprite, plate.x, plate.y), 3, 3, 4, 4);
    }
}

// the intro, then the tabs, the RANK ticker counting down
// decomp: sonic_amp_tab_update 0x2B16C
void tab_update(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const float c = static_cast<float>(ui.frame_cadence);
    const int intro_end = static_cast<int>(0.4f * c) - (static_cast<int>(-0.2f * c) << 3) -
                          static_cast<int>(-0.1f * c);
    if (s.intro_frames < intro_end - static_cast<int>(0.25f * c)) {
        s.zoom = static_cast<int>(c * 0.25f);
    }
    sonic_amp_screen_draw(fe, ui, s);
    sonic_amp_banner_draw(ui, s);
    if (s.intro_frames < intro_end) {
        ++s.intro_frames;
        return;
    }
    sonic_amp_arrows_draw(ui, s);
    if (++s.tip_frames >= ui.frame_cadence * 11) {
        s.tip_frames = 0;
        if (--s.tip < 0) {
            s.tip = 9;
        }
        ticker_set(fe, ui, s);
    }
    PointerRect rects[5];
    tab_rects(ui, rects);
    if (menu_cursor(ui, s.tab, 0, 5, 0x17, 0, 4, rects) != 0) {
        s.on_leave = s.tab == 4;
    } else if (menu_cancel(ui)) {
        if (s.tab != 4) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            s.on_leave = true;
        }
        s.tab = 4;
    } else if (menu_accept(ui, &rects[s.tab])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (s.tab == 4) {
            ui_sound(ui, bank_sys, se_shop_leave);
            s.fade_frames = half(ui);
            fade_out_start(ui, static_cast<float>(s.fade_frames));
            fe.screen.step = step_leave;
        } else {
            s.title_frames = 0;
            s.on_leave = false;
            s.in_list = true;
            s.row = s.col = s.page = 0;
            title_set(fe, ui, s);
            fe.screen.step = s.tab == 0 ? step_picks : step_chart;
        }
    }
    s.chart = s.tab >= 1 ? s.tab - 1 : 0;
}

} // namespace

void sonic_amp_spawn(FrontEnd& fe, Ui&) {
    fe.screen.sonic = SonicAmpScreen{};
    fe.screen.sonic.from = fe.previous;
}

void sonic_amp_step(FrontEnd& fe, Ui& ui) {
    SonicAmpScreen& s = fe.screen.sonic;
    switch (fe.screen.step) {
    case step_load: { // sonic_amp_screen_update
        texture_set_load(ui, 0x690);
        texture_set_load(ui, 0x684);
        texture_set_load(ui, 0x67a);
        level_meter_spawn(ui, fe.screen.meter);
        s.zoom = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.25f);
        ui_music(ui, shop_track, true);
        for (int i = 0; i < 10; ++i) {
            s.picks[i] = static_cast<std::uint16_t>(fe.world->profile.music.sonic_amp_stock[i]);
        }
        ticker_set(fe, ui, s);
        text_strip_init(ui, s.banner, 0x118, 0x2d, table_line(ui, "sonic_amp_banner_line"), 0, 0,
                        0x280, 0x1e0);
        ui_sound(ui, bank_sys, se_shop_arrive);
        s.fade_frames = half(ui);
        fade_in_start(ui, static_cast<float>(s.fade_frames));
        fe.screen.step = step_fade_in;
        break;
    }
    case step_fade_in: // sonic_amp_fade_in
        menu_input_mark(ui);
        s.zoom = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.25f);
        sonic_amp_screen_draw(fe, ui, s);
        sonic_amp_banner_draw(ui, s);
        if (--s.fade_frames < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = step_tabs;
        }
        break;
    case step_tabs:
        tab_update(fe, ui, s);
        break;
    case step_picks:
        picks_update(fe, ui, s);
        break;
    case step_chart:
        chart_update(fe, ui, s);
        break;
    case step_message: // sonic_amp_message_update: two seconds, or accept
        sonic_amp_screen_draw(fe, ui, s);
        if (s.tab == 0) {
            sonic_amp_picks_draw(fe, ui, s);
        } else {
            sonic_amp_chart_draw(fe, ui, s);
        }
        text_strip_draw_scroll(ui, s.title, 0xffffffff, 0x102);
        if (++s.message_frames >= ui.frame_cadence * 2 || menu_accept(ui)) {
            if (s.message_frames < ui.frame_cadence * 2) {
                ui_sound(ui, bank_sys, se_menu_accept);
            }
            s.message_frames = 0;
            s.message = 0;
            s.in_list = true;
            fe.screen.step = s.tab == 0 ? step_picks : step_chart;
        }
        break;
    case step_listen: // sonic_amp_listen_update
        sonic_amp_listen_draw(fe, ui, s);
        if (s.bio_alive) {
            script_step(fe, ui, s.bio_script, s.bio);
        }
        if (s.listen_frames < 0xff) {
            ++s.listen_frames;
        }
        if (menu_cancel(ui) && s.listen_frames > 5) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            s.in_list = true;
            s.menu = 0;
            ui_music(ui, shop_track, true);
            s.bio_alive = false;
            title_set(fe, ui, s);
            fe.screen.step = s.tab == 0 ? step_picks : step_chart;
        }
        break;
    case step_leave: // sonic_amp_leave
        menu_input_mark(ui);
        sonic_amp_screen_draw(fe, ui, s);
        sonic_amp_banner_draw(ui, s);
        if (--s.fade_frames < 0) {
            ui_music_stop(ui);
            fe.screen.strip.active = false;
            task_header_set(fe, 0);
            fe.screen.step = step_done;
        }
        break;
    default:
        sonic_amp_screen_draw(fe, ui, s);
        break;
    }
    children(fe, ui);
}

void sonic_amp_destroy(FrontEnd&, Ui& ui) {
    // sonic_amp_screen_destroy: the sheets go, clear_color black
    ui.clear_colour = 0xff000000;
}
