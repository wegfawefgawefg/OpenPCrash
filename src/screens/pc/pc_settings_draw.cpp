// PC CONFIG drawn (pc_settings_screen.hpp): the sound config's frame (over the pause the dimmed
// fight in its place), the column of option buttons, the key config's bars for the rows and the
// help line.
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"
#include "screens/pc/pc_settings_layout.hpp"
#include "screens/ui/screen_text.hpp"

#include <algorithm>
#include <cmath>

namespace {

// the sound config's backdrop and footer, the key config's bars and arrows
constexpr int backdrop = 2514, footer = 2520;
constexpr int bar_grey = 1597, bar_lit = 1598;
constexpr int arrow_left = 1588, arrow_left_lit = 1589, arrow_right = 1590, arrow_right_lit = 1591;
constexpr int scroll_up = 3171, scroll_down = 3170;
// the sound config's volume bar (2509) and its fill (0x9DB stretched)
constexpr int volume_bar = 2509, volume_fill = 0x9db;
// the options' EXIT button, normal and lit
constexpr int exit_button = 2108, exit_button_lit = 2109;
// the column's words by page; CHEATS is GAME's (the buttons' letters have no H)
constexpr const char* page_words[page_count] = {"SCREEN",   "PICTURE", "SOUND", "INPUT",
                                                "BINDINGS", "GAME",    "EXIT",  "OPTIONS",
                                                "MUSIC",    "SONGS",   "GAME"};
constexpr float keys_x = rows_x + 166, pad_x = rows_x + 306; // BINDINGS' two columns

// the help line: what the page does in the column, the row's own in the rows, BINDINGS' word
// on what a bind did
void help_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    std::string text = s.music_from_sound && s.page == page_exit ? "Return to SOUND."
                       : s.paused && s.page == page_exit         ? "Return to the pause."
                       : s.page == page_music || s.page == page_playlist
                           ? music_page_help(fe, s.page)
                           : pc_page_help(s.page);
    if (s.calibration.open) {
        text = pc_calibrate_help(fe);
    } else if (s.in_rows) {
        text = pc_page_row_view(fe, s.page, s.row).help;
        if (s.page == page_bindings && !s.note.empty()) {
            text = s.note;
        }
    }
    if (text != s.help_text) {
        s.help_text = text;
        help_strip_init(ui, s.help, pc_help_text(text, help_lead_sound_config));
    }
}

// a song's title or a playlist's name in the 18 dot font between the arrows: narrowed to fit, down
// to two thirds, then cut short
void glyphs_draw(Ui& ui, GlyphText text, float left, float right, float y, std::uint32_t colour) {
    const float room = right - left;
    float scale = 1.0f;
    int width = text_width(ui, text);
    if (static_cast<float>(width) > room) {
        scale = std::max(room / static_cast<float>(width), 0.67f);
    }
    while (!text.empty() && static_cast<float>(width) * scale > room) {
        text.pop_back();
        width = text_width(ui, text);
    }
    const int glyph = static_cast<int>(std::lround(18.0f * scale));
    const float x = std::floor((left + right - static_cast<float>(width) * scale) * 0.5f);
    text_draw(ui, text, static_cast<int>(x), static_cast<int>(y), 2, glyph, 18, colour,
              static_cast<int>(text.size()));
}

void draw_value(FrontEnd& fe, Ui& ui, const PcRowView& view, float y, bool current) {
    const PcArt& art = fe.pc_art;
    const std::uint32_t colour = view.dim ? 0xff808080 : 0xffffffff;
    const bool lit = current && fe.screen.pc.in_rows;
    const float value_x = row_value_x(view);
    if (view.arrows) {
        Sprite left = sprite_of(ui, lit ? arrow_left_lit : arrow_left);
        left.pos[0] = value_x;
        left.pos[1] = y + 8;
        left.blend = lit ? 0x105 : 0x104;
        left.flags |= lit ? 0x2000 : 0;
        sprite_submit(ui, left);
        Sprite right = sprite_of(ui, lit ? arrow_right_lit : arrow_right);
        right.pos[0] = value_right + 4;
        right.pos[1] = y + 8;
        right.blend = left.blend;
        right.flags = right.flags | (lit ? 0x2000 : 0);
        sprite_submit(ui, right);
    }
    if (view.bar >= 0) {
        // the number, then the sound config's bar filled to the value
        pc_text_draw(ui, art, view.value, value_x + 22, y + 6, 0x103, colour);
        const float bar_x = value_x + 58, bar_w = value_right - bar_x - 4;
        pc_bar_draw(ui, volume_bar, bar_x, y + 8, bar_w, 0x103, 0xffffffff);
        Sprite fill = sprite_of(ui, volume_fill);
        fill.pos[0] = bar_x + 4;
        fill.pos[1] = y + 12;
        fill.scale[0] =
            (bar_w - 8) / 8.0f * static_cast<float>(std::clamp(view.bar, 0, 100)) / 100.0f;
        fill.blend = 0x104;
        sprite_submit(ui, fill);
        return;
    }
    if (!view.title.empty() || !view.glyphs.empty()) {
        const GlyphText text =
            view.glyphs.empty() ? utf8_to_glyphs(ui.data, view.title) : view.glyphs;
        glyphs_draw(ui, text, value_x + 18, value_right - 2, y + 7, colour);
        return;
    }
    const float width = static_cast<float>(pc_text_width(art, view.value));
    const float x = std::floor((value_x + 16 + value_right - width) * 0.5f);
    pc_text_draw(ui, art, view.value, x, y + 6, 0x103, colour);
}

// a bindings column ("A/B/C") cut back a bind at a time until it fits, a + for what's left out
std::string fit_binds(const PcArt& art, std::string text, float width) {
    bool cut = false;
    while (static_cast<float>(pc_text_width(art, text + (cut ? "+" : ""))) > width) {
        const std::size_t slash = text.rfind('/');
        if (slash == std::string::npos) {
            break;
        }
        text.erase(slash);
        cut = true;
    }
    return cut ? text + "+" : text;
}

void draw_row(FrontEnd& fe, Ui& ui, int row, int shown) {
    PcSettingsScreen& s = fe.screen.pc;
    const PcArt& art = fe.pc_art;
    const PcRowView view = pc_page_row_view(fe, s.page, row);
    const float y = rows_y + row_pitch * static_cast<float>(shown);
    const bool current = row == s.row;
    // the bar grey, the current one again lit and pulsing (key_config_screen_draw)
    pc_bar_draw(ui, bar_grey, rows_x, y, rows_w, 2, 0xffa0a0a0);
    if (current && s.in_rows) {
        pc_bar_draw(ui, bar_lit, rows_x, y, rows_w, 3, 0xffffffff, 0x2000);
    }
    float label_x = rows_x + 8;
    if (view.icon >= 0) {
        Sprite icon = sprite_of(ui, view.icon);
        icon.pos[0] = label_x;
        icon.pos[1] = y + 8;
        icon.blend = 0x103;
        sprite_submit(ui, icon);
        label_x += 20;
    }
    pc_text_draw(ui, art, view.label, label_x, y + 6, 0x103, view.dim ? 0xff808080 : 0xffffffff);
    if (s.page != page_bindings || view.arrows) {
        draw_value(fe, ui, view, y, current);
        return;
    }
    // BINDINGS: the keys and mouse, then the pad; PRESS blinking while it listens
    if (current && s.listening) {
        pc_text_draw(ui, art, "PRESS", keys_x, y + 6, 0x103, 0xffffffff, 0x4000);
        return;
    }
    pc_text_draw(ui, art, fit_binds(art, view.value, pad_x - keys_x - 8), keys_x, y + 6, 0x103,
                 0xffffffff);
    pc_text_draw(ui, art, fit_binds(art, view.value_2, rows_x + rows_w - pad_x - 8), pad_x, y + 6,
                 0x103, 0xffffffff);
}

} // namespace

void pc_settings_draw(FrontEnd& fe, Ui& ui, bool pulse) {
    PcSettingsScreen& s = fe.screen.pc;
    // the frame: backdrop, header and footer as sound_config_sprites places them; over the pause
    // the fight shows dimmed instead of the backdrop, under the header alone
    if (s.paused) {
        Sprite dim = sprite_of(ui, 0xe7c); // the fade's solid
        dim.pos[0] = -32.0f;
        dim.pos[1] = -32.0f;
        dim.scale[0] = 22.0f;
        dim.scale[1] = 17.0f;
        dim.blend = 0;
        dim.color = 0xa0000000;
        dim.flags |= quad_full_window;
        sprite_submit(ui, dim);
    } else {
        Sprite back = sprite_of(ui, backdrop);
        back.blend = 1;
        sprite_submit(ui, back);
    }
    pc_title_draw(ui, 4);
    // day and money as the sound config draws them, unless the options came from the title
    if (!s.paused && fe.options_entry_state != app_title && fe.world) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    if (!s.paused) {
        Sprite foot = sprite_of(ui, footer);
        foot.pos[1] = 376;
        foot.blend = 4;
        sprite_submit(ui, foot);
    }
    help_update(fe, ui);
    text_strip_draw_scroll(ui, s.help, 0xfff0f0f0);

    // the column: every button, the chosen one again lit (pulsing while it has the cursor)
    for (int i = 0; i < pc_column_size(s); ++i) {
        const int page = pc_column_page(s, i);
        const float y = column_y + column_pitch * static_cast<float>(i);
        for (const bool lit : {false, true}) {
            if (lit && page != (s.page == page_cheats ? page_game : s.page)) {
                continue;
            }
            const int blend = lit ? 4 : 3;
            const std::uint32_t flags = lit && pulse && !s.in_rows ? 0x2000 : 0;
            if (page == page_exit) {
                Sprite exit = sprite_of(ui, lit ? exit_button_lit : exit_button);
                exit.pos[0] = column_x;
                exit.pos[1] = y;
                exit.blend = blend;
                exit.flags |= flags;
                sprite_submit(ui, exit);
            } else {
                pc_button_draw(ui, fe.pc_art, page_words[page], column_x, y, lit, blend, flags);
            }
        }
    }

    // STICK CALIBRATION's panel in the rows' place, or the page's rows, ten at a time from the
    // top one
    if (s.calibration.open) {
        pc_calibrate_draw(fe, ui);
    }
    const int count = s.calibration.open ? 0 : pc_page_rows(s.page);
    for (int shown = 0; shown < rows_shown && s.top + shown < count; ++shown) {
        draw_row(fe, ui, s.top + shown, shown);
    }
    if (count > 0 && s.top > 0) {
        Sprite up = sprite_of(ui, scroll_up);
        up.pos[0] = rows_x + rows_w * 0.5f - 8;
        up.pos[1] = rows_y - 14;
        up.blend = 0x104;
        sprite_submit(ui, up);
    }
    if (s.top + rows_shown < count) {
        Sprite down = sprite_of(ui, scroll_down);
        down.pos[0] = rows_x + rows_w * 0.5f - 8;
        down.pos[1] = rows_y + row_pitch * rows_shown;
        down.blend = 0x104;
        sprite_submit(ui, down);
    }
    // the footer's meter and now-playing line (not over the pause, which has no footer)
    if (!s.paused) {
        level_meter_update(ui, fe.screen.meter);
        now_playing_update(ui, fe.screen.strip);
    }
}
