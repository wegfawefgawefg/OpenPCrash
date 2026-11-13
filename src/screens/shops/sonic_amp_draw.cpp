// Sonic Amp's drawing (decomp src/o1/u_028e51.c: sonic_amp_screen_draw, the rank panel
// sonic_amp_rank_panel_draw, the zoom panel sonic_amp_panel_zoom_draw, the tabs
// sonic_amp_tabs_draw, the picks sonic_amp_picks_draw, the charts sonic_amp_chart_draw, the message
// panel sonic_amp_message_draw, the listen view sonic_amp_listen_draw, the arrows
// sonic_amp_arrows_draw, the banner sonic_amp_banner_draw).
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr const char* sa = "sonic_amp";

SpriteSlot row(Ui& ui, const char* table, int i) {
    return slot_at(ui, sa, table, i);
}

int quarter(const Ui& ui) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.25f);
}

int half(const Ui& ui) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
}

std::uint32_t alpha_white(int a) {
    return static_cast<std::uint32_t>(a) << 24 | 0xffffffu;
}

const MusicRecord* record(FrontEnd& fe, int id) {
    return music_record(fe.music, id);
}

// the ranked song's jacket fading in and out over its 11 s,
// the lamps around it lit down to the rank shown
// decomp: sonic_amp_rank_panel_draw 0x29D67
void rank_panel_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    int alpha = 255;
    const int f = s.tip_frames;
    if (f <= 15) {
        alpha = static_cast<int>(static_cast<float>(f) * 0.06666667f * 255.0f);
    } else {
        const int end = ui.frame_cadence * 11;
        if (f >= end - 15) {
            // retail's (int)(1 - t * 255) wraps (1, 240, 223 .. 19): the fade it meant
            const float t = static_cast<float>(f - end + 15) * 0.06666667f;
            alpha = static_cast<int>((1.0f - t) * 255.0f);
        }
    }
    SpriteSlot art = row(ui, "sonic_amp_sprites_2", 1);
    const MusicRecord* r = record(fe, s.picks[s.tip]);
    art.sprite = r ? r->jacket_sprite : -1;
    art.color = alpha_white(alpha);
    row_scaled(ui, art, 1.23f, 1.23f);
    sprite_row_draw(ui, row(ui, "sonic_amp_sprites_2", 2));
    sprite_row_draw(ui, row(ui, "sonic_amp_sprites_2", 3));
    SpriteSlot speaker = row(ui, "sonic_amp_sprites_2", 0);
    speaker.sprite = ui.task_timer % 20 < 10 ? 0x98d : 0x98e;
    sprite_row_draw(ui, speaker);
    const int period = static_cast<int>(static_cast<float>(ui.frame_cadence) * 5.0f);
    sprite_row_offset(ui, row(ui, "sonic_amp_sprites_2", 4), 0.0f,
                      static_cast<float>(ui.task_timer % static_cast<unsigned>(period)) *
                          (154.0f / static_cast<float>(period)));
    for (int i = s.tip; i < 10; ++i) {
        SpriteSlot lamp = row(ui, "sonic_amp_sprites_2", 6 + i);
        lamp.color = i == s.tip ? 0xfffff05a : 0xff5afac8;
        lamp.flags = i == s.tip ? 0x2000 : 0;
        sprite_row_draw(ui, lamp);
    }
}

// sprite 0x971 flying to the middle at 1.7x with a trail
// while the tab cursor is on leave, and back
// decomp: sonic_amp_panel_zoom_draw 0x29A0A
void zoom_draw(Ui& ui, SonicAmpScreen& s) {
    int n = s.zoom;
    const int len = quarter(ui);
    int count;
    if (n < len / 2) {
        const float u = static_cast<float>(n) / static_cast<float>(len);
        count = static_cast<int>((u + u) * 10.0f);
    } else {
        const float u = (static_cast<float>(n) - static_cast<float>(len) * 0.5f) /
                        static_cast<float>(len);
        count = static_cast<int>((1.0f - (u + u)) * 10.0f);
    }
    count = count < 1 ? 1 : count > 10 ? 10 : count;
    float alpha = 1.0f;
    int blend = 0x105;
    for (int i = 0; i < count; ++i) {
        const float fn = static_cast<float>(n);
        const float kf = static_cast<float>(len);
        const int fade = (n > len - 1 || n < len - static_cast<int>(kf * 0.8f)) ? 0 : 5;
        if (!s.on_leave && n <= 0) {
            blend = 3;
        }
        Sprite sp = sprite_of(ui, 0x971);
        sp.pos[0] = fn * 139.0f / kf + 181.0f;
        sp.pos[1] = fn * 103.0f / kf + 137.0f;
        sp.blend = blend;
        sp.scale[0] = fn * 0.703125f / kf + 1.0f;
        sp.scale[1] = fn * (39.0f / 56.0f) / kf + 1.0f;
        sp.color = alpha_white(static_cast<int>(alpha * 255.0f));
        sp.z = 1.0f;
        sp.flags |= static_cast<std::uint32_t>(fade) | 0x18;
        sprite_submit(ui, sp);
        alpha *= 0.7f;
        --n;
        if (blend > 1) {
            --blend;
        }
        if (n < 0) {
            break;
        }
    }
    if (s.on_leave) {
        if (s.zoom < len) {
            ++s.zoom;
        }
    } else if (s.zoom > 0) {
        --s.zoom;
    }
}

// the two bars and the five plates sliding in during the intro
// decomp: sonic_amp_tabs_draw 0x2B42A
void tabs_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const float c = static_cast<float>(ui.frame_cadence);
    const int tenth = static_cast<int>(c * 0.1f);
    if (s.intro_frames < tenth) {
        return;
    }
    if (s.intro_frames == tenth) {
        ui_sound(ui, bank_sys, se_panel_slide_in);
    }
    const float span = static_cast<float>(static_cast<int>(c * 0.4f));
    const float w = 640.0f;
    float x = static_cast<float>(s.intro_frames - tenth) / span * w;
    x = x > w ? w : x;
    sprite_row_offset(ui, row(ui, "sonic_amp_tabs_sprites", 0), w - x, 0.0f);
    sprite_row_offset(ui, row(ui, "sonic_amp_tabs_sprites", 1), x - w, 0.0f);
    const int fifth = static_cast<int>(0.2f * c);
    const int neg_tenth = static_cast<int>(c * -0.1f);
    for (int k = 8; k > 3; --k) {
        float px = static_cast<float>(neg_tenth - fifth * k + s.intro_frames) / span * w;
        px = px > w ? w : px;
        sprite_row_offset(ui, row(ui, "sonic_amp_sprites", 6 + (8 - k)), px - w, 0.0f);
    }
    const int i = s.tab;
    float px = static_cast<float>(neg_tenth - fifth * (8 - i) + s.intro_frames) / span * w;
    px = px > w ? w : px;
    SpriteSlot lit = row(ui, "sonic_amp_sprites", 12 + i);
    if (fe.screen.step != 2) {
        // off the tab cursor (the grid, the menu) retail draws a plain Sprite at the row's place,
        // without the row's pulse flag 0x2000: the picked tab stays lit, steady
        lit.flags = 0;
        lit.z = 1.0f;
        sprite_row_draw(ui, lit);
    } else {
        sprite_row_offset(ui, lit, px - w, 0.0f);
    }
}

// the shop girl's panel and the message's three lines
// decomp: sonic_amp_message_draw 0x2A694
void message_draw(Ui& ui, SonicAmpScreen& s) {
    sprite_row_draw(ui, row(ui, "sonic_amp_chart_rows_sprites", 0));
    sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_sprites_5", 7));
    if (s.message < 0 || s.message > 3) {
        return;
    }
    sprite_row_draw(ui, row(ui, "sonic_amp_chart_rows_sprites", s.message == 1 ? 3 : 1));
    for (int i = 0; i < 3; ++i) {
        // sonic_amp_message_lines, three per message
        const unsigned at = 0x1de1c0u + static_cast<unsigned>((s.message * 3 + i) * 4);
        text_draw(ui, utf8_to_glyphs(ui.data, string_at(ui, at)), 0x8b, 0xd4 + 0x14 * i, 7, 18,
                  18, 0xffffffff, 0x1e);
    }
}

// the buy / listen / back items and the message panel, with the menu open
void menu_draw(Ui& ui, SonicAmpScreen& s) {
    for (int j = 0; j < 3; ++j) {
        if (s.menu == j) {
            sprite_row_draw(ui, row(ui, "sonic_amp_sprites_3", 4 + j));
        }
        sprite_row_draw(ui, row(ui, "sonic_amp_sprites_3", j));
    }
    message_draw(ui, s);
}

// the 2443 band, its alpha ramped by title_frames once the zoom panel is home
void band_draw(Ui& ui, SonicAmpScreen& s) {
    if (s.zoom > 0) {
        return;
    }
    const float fade = static_cast<float>(s.title_frames) / static_cast<float>(half(ui));
    if (s.title_frames < half(ui)) {
        ++s.title_frames;
    }
    SpriteSlot band = row(ui, "sonic_amp_sprites_4", 8);
    band.color = alpha_white(static_cast<int>(fade * 255.0f));
    row_scaled(ui, band, 80.0f, 1.0f);
}

} // namespace

// decomp: sonic_amp_banner_draw 0x29EDE
void sonic_amp_banner_draw(Ui& ui, SonicAmpScreen& s) {
    text_strip_draw_scroll(ui, s.banner, 0xffffffff, 0x102);
    float a = 255.0f - static_cast<float>(s.zoom) / static_cast<float>(quarter(ui)) * 255.0f;
    if (a < 20.0f) {
        a = 0.0f;
    }
    if (s.tab != 4) {
        text_strip_draw_scroll(ui, s.ticker, alpha_white(static_cast<int>(a)), 0x102);
    }
}

// decomp: sonic_amp_screen_draw 0x2B6B2
void sonic_amp_screen_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    // day_draw and money_draw land at their own places whatever the caller passes
    day_draw(ui, *fe.world, 0x109);
    money_draw(ui, *fe.world, 0x109);
    sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_sprites"));
    rank_panel_draw(fe, ui, s);
    zoom_draw(ui, s);
    tabs_draw(fe, ui, s);
}

// decomp: sonic_amp_picks_draw 0x2AC91
void sonic_amp_picks_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const Story& story = fe.world->story;
    sprite_row_draw(ui, row(ui, "sonic_amp_sprites_2", 17));
    band_draw(ui, s);
    if (s.in_list) {
        sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_sprites_5"));
    }
    sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_picks_sprites_2"));
    for (int i = 0; i < 10; ++i) {
        if (s.row * 5 + s.col == i && s.in_list) {
            sprite_row_offset(ui, row(ui, "sonic_amp_sprites_5", 3),
                              static_cast<float>((s.col * 5 + 5) * 16),
                              static_cast<float>(s.row * 0x4f));
        }
        const SpriteSlot cell = row(ui, "sonic_amp_picks_sprites", i);
        const int id = s.picks[i];
        if (id >= 0 && id < music_owned_count_max && story.music_owned[id] != 0) {
            SpriteSlot stamp = row(ui, "sonic_amp_picks_sprites_2", 11);
            stamp.x = cell.x;
            stamp.y = cell.y;
            sprite_row_draw(ui, stamp);
        }
        SpriteSlot art = cell;
        const MusicRecord* r = record(fe, id);
        art.sprite = r ? r->chart_sprite : -1;
        if (art.sprite >= 0) {
            sprite_row_draw(ui, art);
        }
    }
    if (!s.in_list) {
        menu_draw(ui, s);
    }
}

// decomp: sonic_amp_chart_draw 0x2A970
void sonic_amp_chart_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    const Story& story = fe.world->story;
    if (s.in_list) {
        sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_sprites_5"));
    }
    // the page plate sliding in, then the page number and count
    sprite_row_offset(ui, row(ui, "sonic_amp_sprites_5", 5),
                      (1.0f - static_cast<float>(s.title_frames) / static_cast<float>(half(ui))) *
                          200.0f,
                      0.0f);
    if (s.title_frames >= half(ui)) {
        number_draw_full(ui, 0, s.page + 1, static_cast<float>(s.page + 1), 0x227, 0xa9, 0x109,
                         0xffffffff, 1, 0, 1, 3);
        sprite_draw_at(ui, 0xea6, 561.0f, 169.0f, 0x109);
        const int pages = (s.tab == 3) + 2;
        number_draw_full(ui, 0, pages, static_cast<float>(pages), 0x240, 0xa9, 0x109, 0xffffffff,
                         1, 0, 1, 3);
    }
    band_draw(ui, s);
    const int count = s.tab == 3 && s.page == 2 ? 6 : 12;
    const float dy = count == 6 ? 39.0f : 0.0f;
    const int base = (s.page + s.chart * 2) * 12;
    for (int i = 0; i < count; ++i) {
        sprite_row_offset(ui, row(ui, "sonic_amp_chart_sprites_2", i), 0.0f, dy);
        if (s.row * 6 + s.col == i && s.in_list) {
            sprite_row_offset(ui, row(ui, "sonic_amp_sprites_5", 3),
                              static_cast<float>(s.col * 5 * 16),
                              static_cast<float>(s.row * 0x4f) + dy);
        }
        const SpriteSlot cell = row(ui, "sonic_amp_chart_sprites", i);
        if (base + i < music_owned_count_max && story.music_owned[base + i]) {
            SpriteSlot stamp = row(ui, "sonic_amp_picks_sprites_2", 11);
            stamp.x = cell.x;
            stamp.y = cell.y;
            sprite_row_offset(ui, stamp, 0.0f, dy);
        }
        SpriteSlot art = cell;
        const MusicRecord* r = record(fe, base + i);
        art.sprite = r ? r->chart_sprite : -1;
        if (art.sprite >= 0) {
            sprite_row_offset(ui, art, 0.0f, dy);
        }
    }
    if (!s.in_list) {
        menu_draw(ui, s);
    }
    sprite_row_draw(ui, row(ui, "sonic_amp_chart_sprites_3", s.arrow_left ? 1 : 0));
    sprite_row_draw(ui, row(ui, "sonic_amp_chart_sprites_3", s.arrow_right ? 3 : 2));
}

// decomp: sonic_amp_listen_draw 0x29F5C
void sonic_amp_listen_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s) {
    sprite_rows_draw(ui, ui_slots(ui.data, sa, "sonic_amp_sprites_4", 12));
    const MusicRecord* r = record(fe, s.playing);
    for (int i = 0; i < 7; ++i) {
        SpriteSlot slot = row(ui, "sonic_amp_sprites_4", i);
        if (i == 4) {
            slot.sprite = r ? r->listen_sprite_a : -1;
        } else if (i == 5) {
            slot.sprite = r ? r->listen_sprite_b : -1;
        }
        if (slot.sprite >= 0) {
            sprite_row_draw(ui, slot);
        }
    }
    day_draw(ui, *fe.world, 0x109);
    money_draw(ui, *fe.world, 0x109);
    SpriteSlot jacket = row(ui, "sonic_amp_sprites_4", 10);
    jacket.sprite = r ? r->jacket_sprite : -1;
    if (jacket.sprite >= 0) {
        sprite_row_draw(ui, jacket);
    }
    text_strip_draw_scroll(ui, s.banner, 0xffffffff, 0x102);
}

// decomp: sonic_amp_arrows_draw 0x2A19B
void sonic_amp_arrows_draw(Ui& ui, SonicAmpScreen& s) {
    const float c = static_cast<float>(ui.frame_cadence);
    const float h = static_cast<float>(half(ui));
    const float a = static_cast<float>(s.arrows_b) / h;
    for (int i = 0; i < 3; ++i) {
        Sprite sp = sprite_of(ui, 0x97d);
        sp.pos[0] = static_cast<float>(i) * 75.0f + 136.0f;
        sp.pos[1] = 8.0f / h * static_cast<float>(s.arrows_b) + 332.0f;
        sp.blend = 0x101;
        sp.color = alpha_white(static_cast<int>(a * 255.0f));
        sprite_submit(ui, sp);
    }
    const int sec = static_cast<int>(c);
    const int n = s.arrows_a;
    float t;
    if (n < sec - static_cast<int>(-0.3f * c)) {
        t = static_cast<float>(n) / static_cast<float>(sec);
        t = t > 1.0f ? 1.0f : t;
    } else {
        t = static_cast<float>(n - static_cast<int>(c * 0.3f) - sec) / static_cast<float>(sec);
        t = t > 1.0f ? 1.0f : t;
        t = 1.0f - t;
    }
    const float u = 1.0f - t;
    Sprite sp = sprite_of(ui, 0x989);
    sp.pos[0] = 104.0f;
    sp.pos[1] = 310.0f;
    sp.blend = t > u ? 0x101 : 0x102;
    sp.color = alpha_white(static_cast<int>(u * 255.0f));
    sprite_submit(ui, sp);
    ++s.arrows_a;
    ++s.arrows_b;
    if (s.arrows_a > (sec - static_cast<int>(-0.3f * c)) * 2) {
        s.arrows_a = 0;
    }
    if (s.arrows_b > half(ui)) {
        s.arrows_b = 0;
    }
}
