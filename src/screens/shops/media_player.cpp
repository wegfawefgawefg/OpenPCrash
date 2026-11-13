// The media player (media_* state 0x21, decomp src/o1/u_0357d7.c 0x3693D..0x39244, docs/port/
// screens.md "Media playlist"): eight playlists of up to twelve owned songs, choosing the one the
// battles play, editing a list from the owned catalogue with a preview, RANDOM, renaming a list
// through name entry (kind 3). Opened from the garage hub; exit goes back to it.
#include "screens/shops/media_player.hpp"

#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <algorithm>

namespace {

constexpr const char* mp = "media_playlist";
constexpr int shop_track = 0xf; // 8DEALER.WAV


int cad(const Ui& ui, float f) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * f);
}

SpriteSlot row(Ui& ui, const char* table, int i) {
    return slot_at(ui, mp, table, i);
}

// decomp: SpriteRow::draw_offset_color 0xBC016
void row_color(Ui& ui, SpriteSlot slot, float dx, std::uint32_t color) {
    slot.color = color;
    sprite_row_offset(ui, slot, dx, 0.0f);
}

std::uint32_t alpha_white(float f) {
    return static_cast<std::uint32_t>(static_cast<int>(f * 255.0f)) << 24 | 0xffffffu;
}

} // namespace

void media_help_spawn(Ui& ui, MediaScreen& m) {
    text_strip_init(ui, m.help, 0, 0x48, string_at(ui, 0x1fcb70), 0, 0, 0x280, 0x1e0);
    m.help_alive = true;
}

// "《title : artist》" of the cell, or three blanks
// decomp: media_track_text_set 0x36BFC
void media_track_text_set(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    const int cell = m.row * 5 + m.col;
    const int at = cell >= 15 ? -1 : m.page * 15 + cell;
    const MusicRecord* r = at >= 0 && at < 81 && m.ids[at] >= 0 ? music_record(fe.music, m.ids[at])
                                                                 : nullptr;
    text_strip_init(ui, m.text, 0, 0x48,
                    r ? music_title_text(*r) : std::string("\xe3\x80\x80\xe3\x80\x80\xe3\x80\x80"),
                    0, 0, 0x280, 0x1e0);
}

namespace {

// the panel's wait before the menu takes input: 45 + 3 * the list's count
bool panel_ready(const Ui& ui, const MediaScreen& m) {
    const int wait = cad(ui, 0.5f) - m.counts[m.list] * cad(ui, -0.05f) - cad(ui, -0.1f) -
                     cad(ui, -0.15f);
    return m.panel_clock >= wait;
}

// the menu's five items: the four bars and exit
void menu_rects(Ui& ui, PointerRect out[5]) {
    const std::vector<double> bar = ui_values(ui.data, mp, "media_menu_bar_y", 4);
    const SpriteSlot b = row(ui, "media_sprites_2", 1);
    for (int i = 0; i < 4; ++i) {
        out[i] = sprite_rect(ui, b.sprite, b.x, static_cast<float>(bar[static_cast<std::size_t>(i)]));
    }
    const SpriteSlot exit = row(ui, "media_sprites_2", 4);
    out[4] = sprite_rect(ui, exit.sprite, exit.x, exit.y);
}

} // namespace

void media_list_rects(Ui& ui, PointerRect out[8]) {
    for (int i = 0; i < 8; ++i) {
        const SpriteSlot c = row(ui, "media_list_cursor_sprites", i);
        out[i] = sprite_rect(ui, c.sprite, c.x, c.y);
    }
}

namespace {

void children(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    if (m.help_alive) {
        text_strip_draw_scroll(ui, m.help, 0xffffffff, 0x102);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// the slides between the menu and a list panel (media_list_select_enter and
// media_edit_select_enter, identical)
// decomp: media_list_select_enter 0x38347
void select_enter(FrontEnd& fe, Ui& ui, MediaScreen& m, int next) {
    ++m.transition;
    media_header_draw(fe, ui, m);
    media_panel_draw(fe, ui, m);
    sprite_row_draw(ui, row(ui, "media_sprites_2", 4));
    const std::uint32_t color =
        alpha_white(1.0f - static_cast<float>(m.transition) / static_cast<float>(cad(ui, 0.3f)));
    row_color(ui, row(ui, "media_sprites_5", 1), 0.0f, color);
    row_color(ui, row(ui, "media_sprites_5", 2), 0.0f, color);
    const int total = cad(ui, 0.3f), half = total / 2;
    int n = m.transition;
    if (n < half) {
        sprite_row_offset(ui, row(ui, "media_sprites_2", 0),
                          static_cast<float>(n) / static_cast<float>(half) * -320.0f, 0.0f);
    } else if (n < total) {
        n -= cad(ui, -0.3f) / -2;
        sprite_row_offset(ui, row(ui, "media_sprites_3", 3),
                          (static_cast<float>(n) / static_cast<float>(half) - 1.0f) * 320.0f, 0.0f);
    } else {
        sprite_row_draw(ui, row(ui, "media_sprites_3", 3));
        m.transition = 0;
        m.label_pop = cad(ui, 0.2f);
        fe.screen.step = next;
    }
}

// media_list_select_leave (stamp: the kept list's 1697 fading) and
// media_edit_select_leave (no stamp)
// decomp: media_list_select_leave 0x37323
void select_leave(FrontEnd& fe, Ui& ui, MediaScreen& m, bool stamp) {
    ++m.transition;
    media_header_draw(fe, ui, m);
    media_panel_draw(fe, ui, m);
    sprite_row_draw(ui, row(ui, "media_sprites_2", 4));
    const int total = cad(ui, 0.3f), half = total / 2;
    int n = m.transition;
    if (n < half) {
        sprite_row_offset(ui, row(ui, "media_sprites_3", 3),
                          static_cast<float>(n) / static_cast<float>(half) * -320.0f, 0.0f);
    } else if (n < total) {
        n -= cad(ui, -0.3f) / -2;
        sprite_row_offset(ui, row(ui, "media_sprites_2", 0),
                          (static_cast<float>(n) / static_cast<float>(half) - 1.0f) * 320.0f, 0.0f);
    } else {
        sprite_row_draw(ui, row(ui, "media_sprites_2", 0));
    }
    n = m.transition;
    if (stamp && n < total) {
        if (m.question == 2) {
            row_color(ui, row(ui, "media_sprites_3", 0), 0.0f,
                      alpha_white(1.0f - static_cast<float>(n) / static_cast<float>(total)));
        }
        return;
    }
    const int fade = cad(ui, 0.8f);
    if (n < fade) {
        const std::uint32_t color = alpha_white(static_cast<float>(n) / static_cast<float>(fade));
        row_color(ui, row(ui, "media_sprites_5", 1), 0.0f, color);
        row_color(ui, row(ui, "media_sprites_5", 2), 0.0f, color);
    } else {
        sprite_row_draw(ui, row(ui, "media_sprites_5", 1));
        sprite_row_draw(ui, row(ui, "media_sprites_5", 2));
        m.question = 0;
        m.transition = 0;
        m.label_pop = cad(ui, 0.2f);
        fe.screen.step = step_menu;
    }
}

// once the panel is in, the menu frame slides in and its plates fade up
// decomp: media_menu_enter 0x3872A
void menu_enter(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_header_draw(fe, ui, m);
    media_menu_draw(fe, ui, m);
    media_panel_draw(fe, ui, m); // a second time: the panel's clock runs double here, as retail's
    if (!panel_ready(ui, m)) {
        return;
    }
    ++m.transition;
    const std::uint32_t color =
        alpha_white(static_cast<float>(m.transition) / static_cast<float>(cad(ui, 0.8f)));
    row_color(ui, row(ui, "media_sprites_5", 1), 0.0f, color);
    row_color(ui, row(ui, "media_sprites_5", 2), 0.0f, color);
    const int half = cad(ui, 0.3f) / 2;
    const int n = m.transition;
    if (n < half) {
        sprite_row_offset(ui, row(ui, "media_sprites_2", 0),
                          (static_cast<float>(n) / static_cast<float>(half) - 1.0f) * 320.0f, 0.0f);
    } else {
        sprite_row_draw(ui, row(ui, "media_sprites_2", 0));
        if (n >= cad(ui, 0.8f)) {
            m.transition = 0;
            m.label_pop = cad(ui, 0.2f);
            fe.screen.step = step_menu;
        }
    }
}

// decomp: media_menu_update 0x38587
void menu_update(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_header_draw(fe, ui, m);
    media_menu_draw(fe, ui, m);
    if (!panel_ready(ui, m)) {
        return;
    }
    PointerRect rects[5];
    menu_rects(ui, rects);
    if (m.nudge == 0) {
        const int moved = menu_cursor(ui, m.menu, 0, 5, 0x16, 0, 3, rects);
        if (moved != 0) {
            m.label_pop = cad(ui, 0.2f);
            m.nudge = 1;
            m.nudge_down = moved > 0;
            return;
        }
    }
    if (menu_cancel(ui)) {
        if (m.menu != 4) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        m.menu = 4;
    } else if (menu_accept(ui, &rects[m.menu])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        switch (m.menu) {
        case 0:
            m.list = m.list_before;
            fe.screen.step = step_list_enter;
            break;
        case 1:
            fe.screen.step = step_edit_enter;
            break;
        case 2:
            m.random_choice = m.random;
            m.random_box = true;
            fe.screen.step = step_random;
            break;
        case 3:
            fe.screen.step = step_rename;
            break;
        default:
            m.fade_frames = cad(ui, 0.5f);
            fade_out_start(ui, static_cast<float>(m.fade_frames));
            fe.screen.step = step_exit;
            break;
        }
    }
}

// which list the battles play, asked twice
// decomp: media_list_select_update 0x381C7
void list_update(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_header_draw(fe, ui, m);
    media_panel_draw(fe, ui, m);
    sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_sprites_7"));
    media_list_cursor_draw(ui, m);
    PointerRect rects[8];
    media_list_rects(ui, rects);
    if (m.question == 1) {
        sprite_row_draw(ui, row(ui, "media_sprites_3", 1));
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            m.question = 0;
        } else if (menu_accept(ui)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            m.question = 2;
            m.list_before = m.list;
            fe.screen.step = step_list_leave;
        }
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        m.list = m.list_before;
        fe.screen.step = step_list_leave;
    } else if (menu_cursor(ui, m.list, 0, 8, 0x16, 0, 3, rects) == 0 &&
               menu_accept(ui, &rects[m.list])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        m.question = 1;
    }
}

// a fresh entry's payload: the owned catalogue, the lists from the game state
void fresh_entry(FrontEnd& fe, MediaScreen& m) {
    const Story& story = fe.world->story;
    const Music& music = fe.world->profile.music;
    m.total = music_owned_count(story);
    // retail's total / 15 + 1 gives an empty last page when the count is a multiple of 15
    m.pages = std::max(1, (m.total + 14) / 15);
    std::fill(std::begin(m.ids), std::end(m.ids), -1);
    int n = 0;
    for (int i = 0; i < 81; ++i) {
        if (music_record_owned(fe.music, story, i)) {
            m.ids[n++] = i;
        }
    }
    m.list_before = music.playlist_selected;
    // retail opens on playlist 1 (list 0) whichever list plays, and renames that one until
    // choose or edit has run: the port opens on the playing list
    m.list = m.list_before;
    m.random = music.playlist_random ? 1 : 0;
    for (int j = 0; j < playlist_count; ++j) {
        int k = 0;
        for (; k < playlist_length && music.playlists[j][k] != -1; ++k) {
            m.tracks[j][k] = music.playlists[j][k];
        }
        m.counts[j] = k;
        for (int r = k; r < playlist_length; ++r) {
            m.tracks[j][r] = -1;
        }
    }
}

} // namespace

void media_spawn(FrontEnd& fe, Ui&) {
    fe.screen.media = MediaScreen{};
    fe.screen.media.from = fe.previous;
}

void media_step(FrontEnd& fe, Ui& ui) {
    MediaScreen& m = fe.screen.media;
    switch (fe.screen.step) {
    case step_load: // media_playlist_screen_update
        texture_set_load(ui, 0x690);
        texture_set_load(ui, 0x679);
        texture_set_load(ui, 0x67a);
        level_meter_spawn(ui, fe.screen.meter);
        ui_music(ui, shop_track, true);
        if (m.from == app_name_entry && fe.extra.media_saved_valid) {
            // back from name entry: the payload media_rename_start kept, its clocks cleared
            m = fe.extra.media_saved;
            m.from = app_name_entry;
            m.panel_clock = m.edit_clock = m.sweep = m.bars = 0;
            m.arrow_left = m.arrow_right = 0;
            m.random_box = false;
        } else {
            fresh_entry(fe, m);
        }
        fe.extra.media_saved_valid = false;
        m.menu = m.from == app_name_entry ? m.menu : 0;
        m.nudge = 0;
        m.transition = 1;
        m.label_pop = 0;
        m.nudge_down = false;
        m.question = 0;
        media_help_spawn(ui, m);
        m.fade_frames = cad(ui, 0.5f);
        fade_in_start(ui, static_cast<float>(m.fade_frames));
        fe.screen.step = step_fade_in;
        break;
    case step_fade_in: // media_screen_fade_in
        menu_input_mark(ui);
        media_header_draw(fe, ui, m);
        media_menu_draw(fe, ui, m);
        m.sweep = m.bars = 0;
        if (--m.fade_frames < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = step_menu_enter;
        }
        break;
    case step_menu_enter:
        menu_enter(fe, ui, m);
        break;
    case step_menu:
        menu_update(fe, ui, m);
        break;
    case step_list_enter:
        select_enter(fe, ui, m, step_list);
        break;
    case step_list:
        list_update(fe, ui, m);
        break;
    case step_list_leave:
        select_leave(fe, ui, m, true);
        break;
    case step_edit_enter:
        select_enter(fe, ui, m, step_edit_select);
        break;
    case step_edit_select:
        media_edit_select_update(fe, ui, m);
        break;
    case step_edit_leave:
        select_leave(fe, ui, m, false);
        break;
    case step_edit:
        media_edit_update(fe, ui, m);
        break;
    case step_preview: // media_preview_update
        media_preview_draw(fe, ui, m);
        if (m.bio_alive) {
            script_step(fe, ui, m.bio_script, m.bio);
        }
        if (m.preview_frames < 0xff) {
            ++m.preview_frames;
        }
        if (menu_cancel(ui) && m.preview_frames > 5) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            ui_music(ui, shop_track, true);
            m.bio_alive = false;
            m.help_alive = false;
            media_track_text_set(fe, ui, m);
            fe.screen.step = step_edit;
        }
        break;
    case step_random: { // media_random_update: sequential or random
        media_header_draw(fe, ui, m);
        media_menu_draw(fe, ui, m);
        sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_random_sprites_2"));
        sprite_row_draw(ui, row(ui, "media_random_sprites", 0));
        sprite_row_offset(ui, row(ui, "media_random_sprites", 1),
                          m.random_choice == 1 ? 118.0f : 0.0f, 0.0f);
        const SpriteSlot sel = row(ui, "media_random_sprites", 1);
        PointerRect rects[2];
        for (int i = 0; i < 2; ++i) {
            rects[i] = sprite_rect(ui, sel.sprite, sel.x + 118.0f * static_cast<float>(i), sel.y);
        }
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            m.random_box = false;
            fe.screen.step = step_menu;
        } else if (menu_accept(ui, &rects[m.random_choice])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            m.random = m.random_choice;
            m.random_box = false;
            fe.screen.step = step_menu;
        } else {
            menu_cursor(ui, m.random_choice, 0, 2, 0x15, 0, 4, rects);
        }
        break;
    }
    case step_rename: // media_rename_start: a cut to name entry, the payload kept
        media_header_draw(fe, ui, m);
        name_target_set(fe, 3, fe.world->profile.music.playlist_names[m.list]);
        m.help_alive = false;
        fe.extra.media_saved = m;
        fe.extra.media_saved_valid = true;
        fe.screen.strip.active = false;
        task_header_set(fe, 1);
        fe.screen.step = step_done;
        break;
    case step_exit: // media_screen_exit
        menu_input_mark(ui);
        media_header_draw(fe, ui, m);
        media_menu_draw(fe, ui, m);
        if (--m.fade_frames < 0) {
            fe.screen.strip.active = false;
            m.help_alive = false;
            ui_music_stop(ui);
            task_header_set(fe, 0);
            fe.screen.step = step_done;
        }
        break;
    default:
        media_header_draw(fe, ui, m);
        break;
    }
    children(fe, ui, m);
}

void media_destroy(FrontEnd& fe, Ui& ui) {
    // media_playlist_screen_destroy, on every way out (rename too): the lists, the
    // playing one and RANDOM written back, every song bought since the last visit marked seen,
    // the battle's copy rebuilt
    if (fe.screen.step == step_load) {
        return; // never loaded: nothing to write back
    }
    MediaScreen& m = fe.screen.media;
    Music& music = fe.world->profile.music;
    Story& story = fe.world->story;
    music.playlist_selected = m.list_before;
    music.playlist_random = m.random != 0;
    for (int j = 0; j < playlist_count; ++j) {
        for (int k = 0; k < playlist_length; ++k) {
            music.playlists[j][k] = m.tracks[j][k];
        }
    }
    for (std::uint8_t& owned : story.music_owned) {
        if (owned == 1) {
            owned = 2;
        }
    }
    music_playlist_compact(fe.music, story, music);
    ui.clear_colour = 0xff000000;
}
