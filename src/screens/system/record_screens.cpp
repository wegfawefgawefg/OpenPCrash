// Records (record_* state 0x1D, decomp src/o1/u_02edc5.c 0x32E18..0x34F17): the menu, the rumble and
// game pages with their row timing, the leave, the tables the pages name. The tournament pages
// are record_grids.cpp.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>

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

constexpr int common_asset = 0x690;
constexpr int record_asset = 0x67e; // record_tex.bin
constexpr int record_track = 0xc;   // 5GARAGE.WAV (music_play(0xC, 1, 1.0))
constexpr std::uint32_t gold = 0xffffd300;

int cadence(const Ui& ui) {
    return ui.frame_cadence;
}

// row i's colour at page clock t: in 0.2 s after the last row, the
// tick sound as it starts, alpha over 1.5 s, gold to white over 5 s, the flash bar while faint
// decomp: record_row_color 0x33AAC
std::uint32_t row_color(FrontEnd& fe, Ui& ui, int index, int t) {
    const RecordScreen& r = fe.screen.record;
    const float c = static_cast<float>(cadence(ui));
    int v = static_cast<int>(c * -0.3f) - static_cast<int>(0.2f * c) * index + t;
    if (v == 0) {
        ui_sound(ui, bank_sys, se_text_tick);
    }
    v = v < 0 ? 0 : v;
    const float tf = static_cast<float>(v);
    int a = static_cast<int>(tf / static_cast<float>(static_cast<int>(5.0f * c)) * 255.0f);
    a = a > 255 ? 255 : a;
    int alpha = static_cast<int>(tf / static_cast<float>(static_cast<int>(c * 1.5f)) * 255.0f);
    alpha = alpha > 255 ? 255 : alpha;
    if (alpha > 0 && alpha < 0x40) {
        float sx = 1.0f;
        int k = 255 - alpha * 4;
        if (k > 0x80) {
            k = 255;
        }
        if (k < 0x60) {
            sx = static_cast<float>(k) * 0.010416667f;
        }
        const float y = r.menu == 0 ? static_cast<float>(index * 0x16 + 0x75)
                                    : static_cast<float>(index * 0x18 + 0xbe);
        Sprite flash = sprite_of(ui, 0x8b4);
        flash.pos[0] = static_cast<float>(sprite_width(ui, 0x8b4)) * 0.5f + 76.0f;
        flash.pos[1] = static_cast<float>(sprite_height(ui, 0x8b4)) * 0.5f + y;
        flash.blend = 0x109;
        flash.color = record_color_lerp(gold, static_cast<float>(alpha) * 0.003921569f, 0xffffffff) &
                      ((static_cast<std::uint32_t>(static_cast<int>(static_cast<float>(k) * 0.5f))
                        << 24) |
                       0xffffffu);
        flash.scale[0] = sx; // setScale: centred (flag 8)
        flash.flags |= 8;
        sprite_submit(ui, flash);
    }
    return record_color_lerp(gold, static_cast<float>(a) * 0.003921569f, 0xffffffff) &
           ((static_cast<std::uint32_t>(alpha) << 24) | 0xffffffu);
}

// ten digits with a comma before every third, leading zeroes and
// commas as spaces, right-aligned on record_value_right; flag 4 money (clamped, times ten, "　NY"
// after), 8 "％" after, 0x10 the empty unit
// decomp: record_number_draw 0x33140
void number_draw(FrontEnd& fe, Ui& ui, unsigned value, int y, std::uint32_t color,
                 unsigned flags) {
    const RecordScreen& r = fe.screen.record;
    auto glyph0 = [](const GlyphText& g) { return g.empty() ? std::int16_t(-1) : g[0]; };
    if (flags & 4) {
        value = value > 99999999u ? 99999999u : value;
        value *= 10;
    }
    std::int16_t digits[10];
    for (int i = 0; i < 10; ++i) {
        const unsigned p10 = static_cast<unsigned>(std::pow(10.0, i));
        digits[9 - i] = glyph0(r.digits[(value / p10) % 10]);
    }
    GlyphText out;
    const std::int16_t comma = glyph0(r.units[3]);
    for (int i = 10, s = 0; i > 0; --i, ++s) {
        if (i % 3 == 0) {
            out.push_back(comma);
        }
        out.push_back(digits[s]);
    }
    for (std::size_t j = 0; j < 12 && j < out.size(); ++j) {
        if (out[j] != glyph0(r.digits[0]) && out[j] != comma) {
            break;
        }
        out[j] = glyph0(r.units[0]);
    }
    GlyphText suffix;
    if (flags & 4) {
        suffix = r.units[7];
    } else if (flags & 8) {
        suffix = r.units[2];
    } else if (flags & 0x10) {
        suffix = r.units[1];
    } else if (flags & 0x60) {
        // "Vehicles" and "Days": retail converts them but has no branch for 0x20 or 0x40, so it
        // shows bare numbers (docs/bugs.md); the port draws them after a space
        suffix = r.units[flags & 0x20 ? 5 : 6];
        suffix.insert(suffix.begin(), glyph0(r.units[0]));
    }
    while (!suffix.empty() && suffix.back() == -1) {
        suffix.pop_back();
    }
    const int suffix_w = text_width(ui, suffix);
    out.insert(out.end(), suffix.begin(), suffix.end());
    const int w = text_width(ui, out) - suffix_w;
    const int right = table_int(ui, "record", "record_value_right", 0, 522);
    text_draw(ui, out, right - w, y, 1, 18, 18, color, 0x80);
}

// vblanks as hh:mm:ss (hours to 99) at record_time_right - 100
// decomp: record_time_draw 0x32EF6
void time_draw(FrontEnd& fe, Ui& ui, unsigned frames, int y, std::uint32_t color) {
    const RecordScreen& r = fe.screen.record;
    const unsigned rate = 60; // video_rate_nominal
    unsigned hours = frames / (rate * 3600);
    frames -= hours * rate * 3600;
    hours = hours > 99 ? 99 : hours;
    const unsigned minutes = frames / (rate * 60);
    const unsigned seconds = (frames - minutes * rate * 60) / rate;
    const GlyphText colon = r.units[4];
    auto digit = [&](unsigned d) { return r.digits[d].empty() ? std::int16_t(-1) : r.digits[d][0]; };
    const std::int16_t sep = colon.empty() ? std::int16_t(-1) : colon[0];
    const GlyphText out = {digit(hours / 10),   digit(hours % 10),   sep,
                           digit(minutes / 10), digit(minutes % 10), sep,
                           digit(seconds / 10), digit(seconds % 10), -1};
    text_draw(ui, out, table_int(ui, "record", "record_time_right", 0, 522) - 100, y, 1, 18, 18,
              color, 0x80);
}

void label_draw(Ui& ui, int x, int y, const std::string& text, std::uint32_t color) {
    text_draw(ui, utf8_to_glyphs(ui.data, text), x, y, 1, 18, 18, color, 0x80);
}

float open_fade(const Ui& ui, int clock) {
    const float f =
        static_cast<float>(clock) / static_cast<float>(static_cast<int>(0.3f * cadence(ui)));
    return f > 1.0f ? 1.0f : f;
}

// the menu panel and its labels faint behind a page
// decomp: record_page_frame_draw 0x33A6D
void page_frame_draw(Ui& ui) {
    const SpriteSlot* panel = ui_slots(ui.data, "record", "record_sprites", 3);
    if (panel) {
        SpriteSlot row = *panel;
        row.color = (row.color & 0xffffffu) | 0x40000000u;
        row.blend = 2;
        sprite_row_draw(ui, row);
    }
    const SpriteSlot* labels = ui_slots(ui.data, "record", "record_sprites_2");
    for (int i = 0; labels && i < 4; ++i) {
        SpriteSlot row = labels[i];
        row.color = (row.color & 0xffffffu) | 0x40000000u;
        row.blend = 3;
        sprite_row_draw(ui, row);
    }
}

void menu_rects(Ui& ui, PointerRect out[4]) {
    const SpriteSlot* labels = ui_slots(ui.data, "record", "record_sprites_2");
    for (int i = 0; i < 4; ++i) {
        out[i] = labels ? sprite_rect(ui, labels[i].sprite, labels[i].x, labels[i].y)
                        : PointerRect{};
    }
}

// decomp: record_menu_draw 0x3358F
void menu_draw(FrontEnd& fe, Ui& ui) {
    const RecordScreen& r = fe.screen.record;
    record_header_draw(fe, ui);
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_sprites", 3));
    const SpriteSlot* lit = ui_slots(ui.data, "record", "record_menu_sprites", r.menu);
    if (lit) {
        sprite_row_draw(ui, *lit);
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_sprites_2"));
}

// decomp: record_rumble_page 0x33D19
void rumble_page(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    const Records& g = fe.world->profile.records;
    record_unroll_draw(ui, 0x8b3, 57, 93, open_fade(ui, r.clock), 4);
    page_frame_draw(ui);
    record_header_draw(fe, ui);
    if (r.clock < 0xffffff) {
        ++r.clock;
    }
    if (r.clock <= static_cast<int>(0.3f * cadence(ui))) {
        return;
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_rumble_page_sprites"));
    const int t = r.clock;
    number_draw(fe, ui, static_cast<unsigned>(g.battles), 0x7c, row_color(fe, ui, 0, t), 0x10);
    number_draw(fe, ui, static_cast<unsigned>(g.wins), 0x92, row_color(fe, ui, 1, t), 0x10);
    float pct = 0.0f;
    if (g.battles != 0) {
        pct = static_cast<float>(static_cast<unsigned>(g.wins)) /
              static_cast<float>(static_cast<unsigned>(g.battles)) * 100.0f;
    }
    // truncated, not rounded, as retail
    number_draw(fe, ui, static_cast<unsigned>(static_cast<int>(pct)), 0xa8, row_color(fe, ui, 2, t),
                8);
    time_draw(fe, ui, static_cast<unsigned>(g.longest_battle), 0xbe, row_color(fe, ui, 3, t));
    time_draw(fe, ui, g.battles != 0 ? static_cast<unsigned>(g.shortest_battle) : 0u, 0xd4,
              row_color(fe, ui, 4, t));
    number_draw(fe, ui, static_cast<unsigned>(g.best_payout), 0xea, row_color(fe, ui, 5, t), 4);
    number_draw(fe, ui, static_cast<unsigned>(g.kills), 0x100, row_color(fe, ui, 6, t), 0x20);
    number_draw(fe, ui, static_cast<unsigned>(g.drones_shot_down), 0x116, row_color(fe, ui, 7, t),
                0x20);
    number_draw(fe, ui, static_cast<unsigned>(g.enemy_weapons_destroyed), 300,
                row_color(fe, ui, 8, t), 0x10);
    number_draw(fe, ui, static_cast<unsigned>(g.weapons_destroyed), 0x142, row_color(fe, ui, 9, t),
                0x10);
    number_draw(fe, ui, static_cast<unsigned>(g.total_earned), 0x158, row_color(fe, ui, 10, t), 4);
    for (int i = 0, y = 0x7c; i < 11; ++i, y += 0x16) {
        label_draw(ui, 0x54, y, table_text(ui, "record", "record_rumble_labels", i),
                   row_color(fe, ui, i, t));
    }
    if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = record_step_menu;
    }
}

// decomp: record_game_page 0x340A3
void game_page(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    const World& w = *fe.world;
    const Records& g = w.profile.records;
    record_unroll_draw(ui, 0x89f, 68, 169, open_fade(ui, r.clock), 4);
    record_header_draw(fe, ui);
    page_frame_draw(ui);
    if (r.clock < 0xffffff) {
        ++r.clock;
    }
    if (r.clock <= static_cast<int>(0.3f * cadence(ui))) {
        return;
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_game_page_sprites"));
    const int t = r.clock;
    number_draw(fe, ui, static_cast<unsigned>(w.story.days_played), 0xc5, row_color(fe, ui, 0, t),
                0x40);
    number_draw(fe, ui, static_cast<unsigned>(g.save_count), 0xdd, row_color(fe, ui, 1, t), 0x10);
    time_draw(fe, ui, static_cast<unsigned>(g.play_time), 0xf5, row_color(fe, ui, 2, t));
    number_draw(fe, ui, static_cast<unsigned>(static_cast<int>(r.percent)), 0x10d,
                row_color(fe, ui, 3, t), 8);
    for (int i = 0, y = 0xc5; i < 4; ++i, y += 0x18) {
        label_draw(ui, 0x54, y, table_text(ui, "record", "record_game_labels", i),
                   row_color(fe, ui, i, t));
    }
    if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = record_step_menu;
    }
}

// the cursor's move eats the frame
// decomp: record_menu_update 0x342B6
void menu_update(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    menu_draw(fe, ui);
    PointerRect rects[4];
    menu_rects(ui, rects);
    if (menu_cursor(ui, r.menu, 0, 4, 0x16, 0, 4, rects) != 0) {
        return;
    }
    if (menu_cancel(ui)) {
        if (r.menu != 3) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        r.menu = 3;
    } else if (menu_accept(ui, &rects[r.menu])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        r.clock = 0;
        switch (r.menu) {
        case 0:
            fe.screen.step = record_step_rumble;
            break;
        case 1:
            r.page = 0;
            fe.screen.step = record_step_tournaments;
            break;
        case 2:
            fe.screen.step = record_step_game;
            break;
        default:
            r.leave = static_cast<int>(static_cast<float>(cadence(ui)) * 0.5f);
            fade_out_start(ui, static_cast<float>(r.leave));
            fe.screen.step = record_step_leave;
            break;
        }
    }
}

// the screen's children: the shuffle (from Club Wired), the help line, the meter, the strip
void children_update(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    if (r.from == app_club_wired) {
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
    }
    if (r.help_alive) {
        text_strip_draw_scroll(ui, r.help, 0xffffffff, 0x102); // text_strip_scroll_102
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

} // namespace

void record_tables_load(FrontEnd& fe, Ui& ui, RecordScreen& r) {
    r.names.assign(tournament_count, GlyphText{});
    r.portrait.assign(tournament_count, -1);
    r.scoobee.assign(tournament_count, -1);
    std::vector<int> opponent_pilot, pilot_identity, pilot_scoobee, identity_sprite;
    auto read = [&](const char* path, const char* head, auto&& row) {
        gsexp::ParseResult parsed;
        std::string error;
        if (!ui_sexp(ui.data, path, parsed, error)) {
            std::fprintf(stderr, "records: %s\n", error.c_str());
            return;
        }
        for (std::size_t i = 0; i < parsed.root_count(); ++i) {
            const gsexp::FormView form(parsed.root(i));
            if (form.head().is_atom(head)) {
                row(form);
            }
        }
    };
    auto put = [](std::vector<int>& v, int id, int value) {
        if (id >= 0) {
            if (static_cast<int>(v.size()) <= id) {
                v.resize(static_cast<std::size_t>(id) + 1, -1);
            }
            v[static_cast<std::size_t>(id)] = value;
        }
    };
    read("tables/tournaments.sexp", "tournament", [&](const gsexp::FormView& f) {
        const int id = f.get_int("id").value_or(-1);
        if (id >= 0 && id < tournament_count) {
            r.names[static_cast<std::size_t>(id)] =
                utf8_to_glyphs(ui.data, f.get_string("name").value_or(""));
        }
    });
    read("tables/opponents.sexp", "opponent", [&](const gsexp::FormView& f) {
        put(opponent_pilot, f.get_int("id").value_or(-1), f.get_int("pilot").value_or(-1));
    });
    read("tables/pilots.sexp", "pilot", [&](const gsexp::FormView& f) {
        const int id = f.get_int("id").value_or(-1);
        put(pilot_identity, id, f.get_int("identity").value_or(-1));
        put(pilot_scoobee, id, f.get_int("scoobee_identity").value_or(-1));
    });
    read("tables/identities.sexp", "identity", [&](const gsexp::FormView& f) {
        put(identity_sprite, f.get_int("id").value_or(-1), f.get_int("sprite").value_or(-1));
    });
    auto at = [](const std::vector<int>& v, int i) {
        return i >= 0 && i < static_cast<int>(v.size()) ? v[static_cast<std::size_t>(i)] : -1;
    };
    // tournament_opponent_portrait, _scoobee_portrait 0x330B2
    for (int t = 0; t < tournament_count; ++t) {
        const int fixed = fe.story_tables.tournaments[static_cast<std::size_t>(t)].fixed_opponent;
        if (fixed == -1) {
            continue;
        }
        const int pilot = at(opponent_pilot, fixed);
        r.portrait[static_cast<std::size_t>(t)] = at(identity_sprite, at(pilot_identity, pilot));
        r.scoobee[static_cast<std::size_t>(t)] = at(identity_sprite, at(pilot_scoobee, pilot));
    }
}

// decomp: record_header_draw 0x32E18
void record_header_draw(FrontEnd& fe, Ui& ui) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, "record", "record_sprites"));
}

void record_spawn(FrontEnd& fe, Ui&) {
    fe.screen.record.from = fe.previous;
}

void record_step(FrontEnd& fe, Ui& ui) {
    RecordScreen& r = fe.screen.record;
    r.trophies.clear();
    switch (fe.screen.step) {
    case record_step_load: { // record_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, record_asset);
        level_meter_spawn(ui, fe.screen.meter);
        r.menu = r.page = r.col = r.row = r.side_col = r.side_row = r.clock = r.flip = 0;
        r.side_a = true;
        text_strip_init(ui, r.help, 0, 0x48, text_string(ui, "str.19c608"), 0, 0, 0x280, 0x1e0);
        r.help_alive = true;
        for (int i = 0; i < 8; ++i) {
            r.units[i] = utf8_to_glyphs(ui.data, table_text(ui, "record", "record_unit_strings", i));
        }
        for (int i = 0; i < 10; ++i) {
            r.digits[i] =
                utf8_to_glyphs(ui.data, table_text(ui, "record", "record_digit_strings", i));
        }
        record_tables_load(fe, ui, r);
        r.percent = record_percent(fe, ui);
        // from Club Wired the menus' shuffle, else 5GARAGE.WAV (not the state's row)
        if (r.from != app_club_wired) {
            ui_music(ui, record_track, true);
        }
        r.spin = 0;
        r.leave = static_cast<int>(static_cast<float>(cadence(ui)) * 0.5f);
        fade_in_start(ui, static_cast<float>(r.leave));
        fe.screen.step = record_step_fade_in;
        break;
    }
    case record_step_fade_in: // record_screen_fade_in
        menu_input_mark(ui);
        menu_draw(fe, ui);
        if (fade_is_idle(ui)) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = record_step_menu;
        }
        break;
    case record_step_menu:
        menu_update(fe, ui);
        break;
    case record_step_rumble:
        rumble_page(fe, ui);
        break;
    case record_step_game:
        game_page(fe, ui);
        break;
    case record_step_tournaments:
        record_tournament_page(fe, ui);
        break;
    case record_step_area_grid:
        record_area_grid(fe, ui);
        break;
    case record_step_side_grid:
        record_side_grid(fe, ui);
        break;
    case record_step_leave: // record_screen_leave
        menu_input_mark(ui);
        menu_draw(fe, ui);
        if (--r.leave < 0) {
            fe.screen.strip.active = false;
            r.help_alive = false;
            ui_music_stop(ui);
            task_header_set(fe, r.from == app_club_wired ? 1 : 0);
            fe.screen.step = record_step_done;
        }
        break;
    default:
        menu_draw(fe, ui);
        break;
    }
    children_update(fe, ui);
    // record_model_draw: this update's trophies in 3D, the model of record_trophy_models at its
    // point, its pitch, all turned by the shared spin; `model_render_submit(.., 2)`, render pass 2,
    // so the cards and grids' 2D (pass 12) draws over them, not pass 13 as the result's
    const std::vector<double> models = ui_values(ui.data, "record", "record_trophy_models", 8);
    for (const RecordTrophy& t : r.trophies) {
        if (t.model >= 0 && t.model < 8) {
            const float turn[3] = {t.pitch, 0.0f, 0.0f};
            trophy_draw(ui, r.models, static_cast<int>(models[static_cast<std::size_t>(t.model)]),
                        t.at, turn, t.spin, 2);
        }
    }
}

void record_destroy(FrontEnd& fe, Ui& ui) {
    ui_music_stop(ui);
    fe.screen.record.trophies.clear();
    trophy_models_release(ui, fe.screen.record.models);
    ui.clear_colour = 0xff000000;
}
