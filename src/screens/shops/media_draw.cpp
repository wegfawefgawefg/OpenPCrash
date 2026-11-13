// The media player's drawing (decomp src/o1/u_0357d7.c: media_header_draw, the playlist
// panel media_playlist_panel_draw with its name media_playlist_name_draw, the menu media_menu_draw,
// the catalogue media_catalogue_draw with its page media_page_draw, the preview media_preview_draw,
// the list cursor media_list_cursor_draw).
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr const char* mp = "media_playlist";

SpriteSlot row(Ui& ui, const char* table, int i) {
    return slot_at(ui, mp, table, i);
}

int cad(const Ui& ui, float f) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * f);
}

std::uint32_t alpha_white(float f) {
    return static_cast<std::uint32_t>(static_cast<int>(f * 255.0f)) << 24 | 0xffffffu;
}

// three digits with leading zeroes, number font 3, layer 0x106
// decomp: media_number_draw 0x36A01
void number(Ui& ui, int value, int x, int y, std::uint32_t color) {
    number_draw_full(ui, 0x10, value, static_cast<float>(value), x, y, 0x106, color, 3, 0, 1, 3);
}

// decomp: media_track_index 0x36BE5
int track_index(int page, int cell) {
    return cell >= 15 ? -1 : page * 15 + cell;
}

// the jacket of the cell under the cursor, -1 none
// decomp: media_cursor_track_sprite 0x372D6
int cursor_jacket(FrontEnd& fe, const MediaScreen& m) {
    const int at = track_index(m.page, m.row * 5 + m.col);
    const int id = m.row == 3 || at < 0 || at >= 81 ? -1 : m.ids[at];
    const MusicRecord* r = id >= 0 ? music_record(fe.music, id) : nullptr;
    return r ? r->jacket_sprite : -1;
}

// the list's name growing in after the panel, centred on x
// 311 by the last glyph that isn't blank; false (the label pops in instead) for an empty name
// decomp: media_playlist_name_draw 0x36A3A
bool name_draw(FrontEnd& fe, Ui& ui, const MediaScreen& m) {
    const int start = cad(ui, 0.5f) - cad(ui, -0.1f);
    const int t = m.panel_clock;
    if (t <= start) {
        return false;
    }
    const int full = start - cad(ui, -0.15f);
    const float x = t > full ? static_cast<float>(full)
                             : static_cast<float>(t + cad(ui, -0.5f) - cad(ui, 0.1f));
    const float h = t < full ? x * (1.0f / static_cast<float>(cad(ui, 0.15f))) : 1.0f;
    const std::int16_t blank = utf8_to_glyphs(ui.data, "\xe3\x80\x80").front();
    const GlyphName& name = fe.world->profile.music.playlist_names[m.list];
    bool any = false;
    for (int i = 0; i < 8 && name[i] != -1; ++i) {
        if (name[i] != blank) {
            any = true;
            break;
        }
    }
    int last = 7;
    for (; last >= 0; --last) {
        if (name[last] != -1 && name[last] != blank) {
            any = true;
            break;
        }
    }
    if (!any) {
        return false;
    }
    text_draw(ui, glyph_name_text(name, 8), 0x137 - last * 9,
              0x6f - static_cast<int>(h * -18.0f) / -2, 1, 18, static_cast<int>(h * 18.0f),
              0xffffba0d, 8);
    return true;
}

} // namespace

// decomp: media_header_draw 0x36C93
void media_header_draw(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_header_sprites"));
    sprite_row_draw(ui, row(ui, "media_header_sprites_2", m.random == 0 ? 0 : 1));
}

void media_panel_draw(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    const int a = cad(ui, 0.5f) - cad(ui, -0.1f); // 36: the panel in, the name's start
    float sx = 1.0f, sy = 1.0f;
    if (m.panel_clock < a) {
        const float t = static_cast<float>(m.panel_clock);
        const float inv = 1.0f / static_cast<float>(a);
        sx = t * (inv * (1.0f - 0.6f)) + 0.6f;
        sy = t * (inv * (1.0f - 0.8f)) + 0.8f;
    }
    row_anchored(ui, row(ui, "media_sprites", 1), sx, sy);
    if (!name_draw(fe, ui, m) && m.panel_clock > a) {
        // the list's label pops in instead of a name
        SpriteSlot label = row(ui, "media_playlist_panel_sprites", 0);
        label.sprite = table_int(ui, mp, "media_playlist_label_sprites", m.list, label.sprite);
        const int end = a - cad(ui, -0.15f);
        const int now = m.panel_clock;
        const float v = now > end ? static_cast<float>(end) : static_cast<float>(now - a);
        float b = 1.0f, h = 1.0f;
        if (now < end) {
            const float inv = 1.0f / static_cast<float>(cad(ui, 0.15f));
            b = v * (inv * (1.0f - 1.2f)) + 1.2f;
            h = inv * v;
        }
        row_anchored(ui, label, b, h);
    }
    // twelve slot plates spreading out from the middle
    const int lead = cad(ui, 0.1f);
    if (m.panel_clock > lead) {
        const float t = m.panel_clock > a ? static_cast<float>(a)
                                          : static_cast<float>(m.panel_clock - lead);
        const float step = 225.0f / static_cast<float>(cad(ui, 0.5f));
        for (int i = 0; i < 12; ++i) {
            float w, x;
            int blend;
            if (i < 6) {
                w = -((static_cast<float>(5 - i) + 0.5f) * 45.0f);
                x = -(t * step);
                x = x < w ? w : x;
                blend = i + 0x100;
            } else {
                w = (static_cast<float>(i - 5) - 0.5f) * 45.0f;
                x = t * step;
                x = x > w ? w : x;
                blend = 0x10b - i;
            }
            Sprite s = sprite_of(ui, 0x69c);
            s.pos[0] = x + 295.5f;
            s.pos[1] = 125.0f;
            s.blend = blend;
            s.color = alpha_white(x / w);
            sprite_submit(ui, s);
        }
    }
    // the list's track numbers, one after another
    for (int k = 0; k < m.counts[m.list]; ++k) {
        if (m.panel_clock > a - (cad(ui, -0.05f) * k + cad(ui, -0.15f))) {
            number(ui, m.tracks[m.list][k], static_cast<int>(static_cast<float>(k) * 45.0f + 56.0f) + 1,
                   0x93, 0xffffffff);
        }
    }
    if (m.panel_clock < cad(ui, 0.5f) - cad(ui, -0.15f) * 12 - cad(ui, -0.1f)) {
        ++m.panel_clock;
    }
}

// decomp: media_menu_draw 0x37CEF
void media_menu_draw(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_panel_draw(fe, ui, m);
    if (m.transition != 0) {
        return;
    }
    sprite_row_draw(ui, row(ui, "media_sprites_2", 0));
    sprite_row_draw(ui, row(ui, "media_sprites_2", 4));
    if (m.menu == 4) {
        sprite_row_draw(ui, row(ui, "media_sprites_2", 5));
    } else {
        const std::vector<double> bar = ui_values(ui.data, mp, "media_menu_bar_y", 4);
        const std::vector<double> marker = ui_values(ui.data, mp, "media_menu_marker_y", 4);
        sprite_row_offset(ui, row(ui, "media_sprites_2", 1), 0.0f,
                          static_cast<float>(bar[static_cast<std::size_t>(m.menu)] - bar[0]));
        float y = static_cast<float>(marker[static_cast<std::size_t>(m.menu)] - marker[0]);
        // the marker trails in and overshoots 7.4 px; not on the wraps' far ends
        const int n = m.nudge;
        float d = 0.0f;
        if (n != 0 && (m.nudge_down || m.menu != 3) && (!m.nudge_down || m.menu != 0)) {
            d = n < 8 ? static_cast<float>(n) * 5.55f - 37.0f
                      : (1.0f - static_cast<float>(n - 8) * (1.0f / 3.0f)) * (5.55f * 8.0f - 37.0f);
        }
        y += m.nudge_down ? d : -d;
        sprite_row_offset(ui, row(ui, "media_sprites_2", 2), 0.0f, y);
    }
    if (m.nudge > 0 && ++m.nudge > 11) {
        m.nudge = 0;
    }
    const float fade = 1.0f - static_cast<float>(m.label_pop--) / static_cast<float>(cad(ui, 0.2f));
    if (m.label_pop < 0) {
        m.label_pop = 0;
    }
    // the item's label growing in
    int label = -1;
    switch (m.menu) {
    case 0:
        label = table_int(ui, mp, "media_label_choose", 0);
        break;
    case 1:
        label = table_int(ui, mp, "media_label_edit", 0);
        break;
    case 2:
        label = !m.random_box ? table_int(ui, mp, "media_label_random", 0)
                : m.random_choice == 0 ? table_int(ui, mp, "media_label_random_off", 0)
                                       : table_int(ui, mp, "media_label_random_on", 0);
        break;
    case 3:
        label = table_int(ui, mp, "media_label_rename", 0);
        break;
    default:
        break;
    }
    if (label >= 0) {
        const SpriteSlot at = row(ui, "media_sprites_5", 0);
        Sprite s = sprite_of(ui, label);
        s.pos[0] = at.x;
        s.pos[1] = (1.0f - fade) * 19.0f + at.y;
        s.blend = at.blend;
        s.scale[0] = 1.0f;
        s.scale[1] = fade;
        sprite_submit(ui, s);
    }
    sprite_row_draw(ui, row(ui, "media_sprites_5", 1));
    const int period = cad(ui, 4.0f) / 20;
    SpriteSlot blink = row(ui, "media_sprites_5", 2);
    blink.sprite = m.sweep % period > period / 2 ? table_int(ui, mp, "media_blink_sprite_a", 0)
                                                 : table_int(ui, mp, "media_blink_sprite_b", 0);
    sprite_row_draw(ui, blink);
    sprite_row_offset(ui, row(ui, "media_sprites_5", 3), 0.0f,
                      (116.0f / static_cast<float>(cad(ui, 4.0f))) * static_cast<float>(m.sweep));
    const int step = cad(ui, 0.1f);
    const char* bars[3] = {"media_bars_sprite_1", "media_bars_sprite_2", "media_bars_sprite_3"};
    if (m.bars < step * 3) {
        SpriteSlot b = row(ui, "media_sprites_6", 0);
        b.sprite = table_int(ui, mp, bars[m.bars / step], 0);
        sprite_row_draw(ui, b);
    }
    ++m.sweep;
    ++m.bars;
    if (m.sweep > cad(ui, 4.0f)) {
        m.sweep = 0;
    }
    if (m.bars > cad(ui, 0.7f) + cad(ui, 0.1f) * 3) {
        m.bars = 0;
    }
}

// decomp: media_list_cursor_draw 0x369D1
void media_list_cursor_draw(Ui& ui, MediaScreen& m) {
    sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_sprites_3", 3));
    sprite_row_draw(ui, row(ui, "media_list_cursor_sprites", m.list));
}

// decomp: media_catalogue_draw 0x37650
void media_catalogue_draw(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    sprite_row_draw(ui, row(ui, "media_catalogue_sprites", 1));
    text_strip_draw_scroll(ui, m.text, 0xffffffff, 0x107);
    const int c3 = cad(ui, 0.3f), c4 = cad(ui, 0.4f), c15 = cad(ui, 0.15f), c6 = cad(ui, 0.6f);
    int clock = m.edit_clock;
    // the grid frame opening
    const float t = static_cast<float>(clock - c3 - c4);
    float sx = 1.0f, sy = 1.0f;
    if (clock < c3 + c4 + c15) {
        const float r = 1.0f / static_cast<float>(c15);
        sx = (r * (1.0f - 1.2f)) * t + 1.2f;
        sy = r * t;
    }
    if (clock > c3 + c4) {
        row_anchored(ui, row(ui, "media_sprites", 3), sx, sy);
    }
    // fifteen cells spreading from the middle column
    const float off = clock > c3 ? (clock < c3 + c4 ? static_cast<float>(clock - c3)
                                                    : static_cast<float>(c3 + c4))
                                 : 0.0f;
    float a = static_cast<float>(clock) / static_cast<float>(c3);
    a = a > 1.0f ? 1.0f : a;
    const float spread = 94.0f / static_cast<float>(c4);
    for (int i = 0; i < 15; ++i) {
        const int col = i % 5;
        int blend;
        float x;
        if (col < 2) {
            blend = col + 0x103;
            const float edge = -((2.0f - static_cast<float>(col)) * 47.0f);
            x = -(spread * off);
            x = x < edge ? edge : x;
        } else if (col == 2) {
            blend = 0x105;
            x = 0.0f;
        } else {
            blend = 0x107 - col;
            const float edge = (static_cast<float>(col) - 2.0f) * 47.0f;
            x = spread * off;
            x = x > edge ? edge : x;
        }
        Sprite s = sprite_of(ui, 0x69d);
        s.color = alpha_white(a);
        s.pos[0] = x + 183.0f;
        s.pos[1] = static_cast<float>((i / 5) * 30) + 236.0f;
        s.blend = blend;
        sprite_submit(ui, s);
    }
    // the page arrows moving out
    float x = -((136.5f / static_cast<float>(c4)) * off);
    x = x < -136.5f ? -136.5f : x;
    const std::uint32_t color = alpha_white(x * (-1.0f / 136.5f));
    sprite_draw_at(ui, m.arrow_left ? 0x6c1 : 0x6c0, x + 194.5f, 250.0f,
                   m.arrow_left ? 0x102 : 0x101, color);
    x = (136.5f / static_cast<float>(c4)) * off;
    x = x > 136.5f ? 136.5f : x;
    sprite_draw_at(ui, m.arrow_right ? 0x6c3 : 0x6c2, x + 194.5f, 250.0f,
                   m.arrow_right ? 0x102 : 0x101, color);
    // the cover of the song under the cursor
    float cover = 0.0f;
    if (clock > c3 + c4 + c15) {
        cover = static_cast<float>(clock - c3 - c15 - c4) / static_cast<float>(c3);
        cover = cover > 1.0f ? 1.0f : cover;
    }
    const int jacket = cursor_jacket(fe, m);
    if (jacket != -1) {
        SpriteSlot art = row(ui, "media_catalogue_sprites_3", 0);
        art.sprite = jacket;
        art.color = alpha_white(cover);
        sprite_row_draw(ui, art);
    }
    SpriteSlot frame = row(ui, "media_catalogue_sprites_3", 1);
    frame.color = alpha_white(cover);
    sprite_row_draw(ui, frame);
    media_panel_draw(fe, ui, m);
    // the button hints
    if (m.row != 3) {
        if (m.pages > 1) {
            sprite_row_draw(ui, row(ui, "media_catalogue_sprites_2", 4));
            sprite_row_draw(ui, row(ui, "media_sprites_4", 0));
            sprite_row_draw(ui, row(ui, "media_catalogue_sprites_2", 5));
            sprite_row_draw(ui, row(ui, "media_sprites_4", 1));
        }
        if (m.counts[m.list] != 0) {
            sprite_row_draw(ui, row(ui, "media_catalogue_sprites_2", 6));
            sprite_row_draw(ui, row(ui, "media_sprites_4", 2));
        }
        if (jacket != -1) {
            sprite_row_draw(ui, row(ui, "media_catalogue_sprites_2", 7));
            sprite_row_draw(ui, row(ui, "media_sprites_4", 3));
        }
    }
    if (m.edit_clock < c4 + c3 * 2 + c15 + c6) {
        ++m.edit_clock;
    }
    if (m.edit_clock <= c4 + c3 * 2 + c15) {
        return;
    }
    // the cursor, the insert mark over the list's next slot, the page's numbers
    if (m.row == 3) {
        sprite_row_draw(ui, row(ui, "media_catalogue_sprites_2", 0));
    } else {
        sprite_row_offset(ui, row(ui, "media_catalogue_sprites_2", 2),
                          static_cast<float>(m.col * 47), static_cast<float>(m.row * 30));
    }
    const int n = m.counts[m.list] > 11 ? 11 : m.counts[m.list];
    sprite_row_offset(ui, row(ui, "media_catalogue_sprites_2", 1), static_cast<float>(n) * 45.0f,
                      0.0f);
    // media_page_draw; retail's last page holds total % 15, which with `pages` being
    // total / 15 + 1 is an empty page when total is a multiple of 15: the port's pages are
    // whole, so the last holds what is left
    const int on_page = m.total - m.page * 15 > 15 ? 15 : m.total - m.page * 15;
    float fade = static_cast<float>(cad(ui, -0.15f) + cad(ui, -0.3f) * 2 - c4 + m.edit_clock) /
                 static_cast<float>(c6);
    fade = fade > 1.0f ? 1.0f : fade < 0.0f ? 0.0f : fade;
    const Story& story = fe.world->story;
    for (int i = 0; i < on_page; ++i) {
        const int at = track_index(m.page, i);
        const int id = at >= 0 && at < 81 ? m.ids[at] : -1;
        const int col = i % 5, r = i / 5;
        number(ui, id, static_cast<int>(static_cast<float>(col) * 47.0f + 99.0f),
               static_cast<int>(static_cast<float>(r) * 30.0f + 247.0f), alpha_white(fade));
        if (id >= 0 && id < music_owned_count_max && story.music_owned[id] == 1) {
            SpriteSlot fresh = row(ui, "media_sprites_4", 5);
            fresh.color = alpha_white(fade);
            sprite_row_offset(ui, fresh, static_cast<float>(col * 0x2f), static_cast<float>(r * 0x1e));
        }
    }
}

// decomp: media_preview_draw 0x380CC
void media_preview_draw(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    const int at = track_index(m.page, m.row * 5 + m.col);
    const int id = m.row == 3 || at < 0 ? -1 : m.ids[at];
    const MusicRecord* r = id >= 0 ? music_record(fe.music, id) : nullptr;
    for (int i = 2; i < 8; ++i) {
        SpriteSlot slot = row(ui, "media_sprites_6", i);
        if (i == 4) {
            slot.sprite = cursor_jacket(fe, m);
        } else if (i == 5) {
            slot.sprite = r ? r->listen_sprite_a : -1;
        } else if (i == 6) {
            slot.sprite = r ? r->listen_sprite_b : -1;
        }
        if (slot.sprite >= 0) {
            sprite_row_draw(ui, slot);
        }
    }
    sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_preview_sprites"));
}
