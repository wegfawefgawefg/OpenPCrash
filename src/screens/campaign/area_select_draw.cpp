// Area select's pages (decomp src/o1/u_012ff5.c area_select_frame_draw, job_panel_draw,
// area_select_draw, job_menu_draw, job_gate_screen_draw,
// job_gates_draw; docs/port/screens.md "The area page", "The job page", "Gate select").
// With area_strip_draw and area_plate_play (the TV-static switch between area
// pictures), job_area_map_draw and job_route_marks_draw. The cards and the map's
// fighters are job_cards.cpp's.
#include "screens/campaign/area_select.hpp"

#include "screens/campaign/area_plate.hpp"

#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

// tab slides (area_tab_start_x / area_tab_x / area_bar_start_x): from, to; the bar 857
constexpr float tab_from[4] = {-520, -378, -236, -94};
constexpr float tab_to[4] = {50, 152, 254, 356};
constexpr int tab_bar = 857;
constexpr float tab_y = 371;
// the panel's slide: x = t x 292 over 8 ticks
constexpr float panel_slide = 292;
// the TV static over an area with no tournament today
constexpr int static_sprite = 0x35e;

float clamp01(float v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

void put(Ui& ui, int id, float x, float y, int blend, std::uint32_t color = 0xffffffff,
         std::uint32_t flags = 0, float alpha = 1.0f) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.blend = blend;
    s.color = color;
    s.flags |= flags;
    s.z = alpha;
    sprite_submit(ui, s);
}

int value_at(const std::vector<double>& v, int i) {
    return i >= 0 && i < static_cast<int>(v.size()) ? static_cast<int>(v[static_cast<std::size_t>(i)])
                                                    : 0;
}

// the slide toward the job page: 0 the area page still, 1 the job page still
float page_t(const Ui& ui, const AreaSelectScreen& a) {
    const float moved =
        clamp01(static_cast<float>(ui.task_timer - static_cast<std::uint32_t>(a.page_change)) / 8.0f);
    if (a.mode == 1) {
        return moved;
    }
    if (a.mode == 2) {
        return 1.0f - moved;
    }
    return a.page == 1 ? 1.0f : 0.0f;
}

// a tournament's or rule's name (cut at 40 bytes of Shift-JIS: 20 full-width glyphs)
GlyphText name_glyphs(Ui& ui, const std::string& text) {
    GlyphText g = utf8_to_glyphs(ui.data, text);
    if (g.size() > 20) {
        g.resize(20);
    }
    g.push_back(-1);
    return g;
}

// decomp: area_select_frame_draw 0x13902
void frame_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    put(ui, 0x348, 0, 0, 1);
    put(ui, 0x36c, 0, 0, 0x101);
    put(ui, 0x349, 0, 376, 0x101);
    garage_banner_draw(ui, *fe.world, a.banner_timer, 0x14f, 0x108, true);
}

// the right panel and the area's picture (area_strip_draw), its
// tournament. t is the slide toward the job page, x = t x 292
// decomp: job_panel_draw 0x15C44
void panel_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a, float t) {
    const World& world = *fe.world;
    const float x = t * panel_slide;
    const std::vector<double> ids = ui_values(ui.data, "job", "area_ids", 16);
    const int shown = a.strip.shown;
    const int was = a.previous_area;
    put(ui, 0x352, 316 + x, 219, 0x103);
    const int job = shown < 3 ? world.story.lineup[shown] : -1;
    const int night = job >= 0 ? fe.story_tables.tournaments[static_cast<std::size_t>(job)].night : 0;
    const int was_job = was >= 0 && was < 3 ? world.story.lineup[was] : -1;
    const int was_night =
        was_job >= 0 ? fe.story_tables.tournaments[static_cast<std::size_t>(was_job)].night : 0;
    put(ui, 0x354, 21 - x, 97, 0x104);
    const int name = value_at(ids, shown * 4 + 2), title = value_at(ids, shown * 4 + 3);
    a.strip.playing = area_plate_play(ui, value_at(ids, shown * 4 + night),
                                 value_at(ids, was * 4 + was_night), a.strip.start, 0x47, 0x73, 3,
                                 -static_cast<int>(x));
    if (a.strip.playing) {
        // the names cross-fade old to new over 21 ticks
        const float fade =
            static_cast<float>(ui.task_timer - static_cast<std::uint32_t>(a.strip.start)) / 21.0f;
        put(ui, name, 41, 250, 0x105, 0xffffffff, 0, fade);
        put(ui, title, 316, 111, 0x102, 0xffffffff, 0, fade);
        put(ui, value_at(ids, was * 4 + 2), 41, 250, 0x106, 0xffffffff, 0, 1.0f - fade);
        put(ui, value_at(ids, was * 4 + 3), 316, 111, 0x102, 0xffffffff, 0, 1.0f - fade);
    } else if (t != 0.0f && t != 1.0f) {
        // sliding: ten copies of the title trail it, fading by halves
        float alpha = 1.0f;
        for (int k = 0; k < 10; ++k) {
            float u = static_cast<float>(ui.task_timer + static_cast<std::uint32_t>(k) -
                                         static_cast<std::uint32_t>(a.page_change)) *
                      0.125f;
            if (a.mode == 2) {
                u = 1.0f - u;
            }
            u = clamp01(u);
            float px = 316.0f - u * 292.0f;
            px = px < 24.0f ? 24.0f : px > 316.0f ? 316.0f : px;
            put(ui, title, static_cast<float>(sprite_width(ui, title)) * 0.5f + px,
                static_cast<float>(sprite_height(ui, title) / 2 + 111), 0x102, 0xffffffff, 8,
                1.0f - alpha);
            alpha *= 0.5f;
        }
    } else {
        const float px = 316.0f - x * t;
        put(ui, name, px - 275.0f, 250, 0x105);
        put(ui, title, static_cast<float>(sprite_width(ui, title)) / 2 + px,
            static_cast<float>(sprite_height(ui, title) / 2 + 111), 0x102, 0xffffffff, 8);
    }
    const EntryStatus status = area_entry_status(world.story, fe.story_tables, shown);
    if (status == EntryStatus::NoTournament) {
        put(ui, 0x34b, 71 - x, 115, 0x106);
        put(ui, static_sprite, 71 - x, 115, 0x105, 0xffffffff, 0, 0.5f);
    } else if (status != EntryStatus::Shop && job >= 0) {
        const int rule = tournament_rule(fe.story_tables, job);
        const RosterTables& rt = fe.campaign.roster;
        const GlyphText tname = name_glyphs(ui, rt.tournament_names[static_cast<std::size_t>(job)]);
        const GlyphText rule_name = name_glyphs(ui, rt.rule_names[static_cast<std::size_t>(rule)]);
        text_draw(ui, tname, static_cast<int>(575 + x) - text_width(ui, tname), 235, 1, 18, 18,
                  0xfff0f0f0, 40);
        text_draw(ui, rule_name, static_cast<int>(575 + x) - text_width(ui, rule_name), 271, 1, 18,
                  18, 0xfff0f0f0, 40);
        money_amount_draw(ui, fe.story_tables.rules[static_cast<std::size_t>(rule)].entry_fee,
                          static_cast<int>(552 + x), 308, 0x102, 9, 0xfff0f0f0);
        const bool chrome = job >= 52 && job <= 54;
        if (tournament_won(world.story, job) && !chrome) {
            put(ui, 0x34a, 127 - x, 243 - x, 4);
        }
    }
}

// the map's backdrop rising from half to full in 10 ticks, the four
// overlay frames cross-fading every 10 ticks (the fourth stays), then job_route_marks_draw:
// the area's route marks
// decomp: job_area_map_draw 0x14AF3
void map_draw(Ui& ui, AreaSelectScreen& a, float x) {
    const unsigned elapsed = ui.task_timer - static_cast<std::uint32_t>(a.map_clock);
    const float e = static_cast<float>(elapsed);
    const float back = e * 0.05f + 0.5f;
    put(ui, 0x387, x + 263.0f, 96, 3, 0xffffffff, 0, back > 1.0f ? 1.0f : back);
    const std::vector<double> overlays = ui_values(ui.data, "job", "job_map_overlays", 16);
    const auto frame = [&](int k, float alpha) {
        const int id = value_at(overlays, a.area * 4 + k);
        if (id >= 0) {
            put(ui, id, 234, 73, 4, 0xff6dfff1, 1, alpha);
        }
    };
    if (elapsed < 10) {
        frame(0, clamp01(e * 0.1f));
    } else {
        const int k = elapsed < 20 ? 0 : elapsed < 30 ? 1 : 2;
        const float f =
            clamp01(static_cast<float>(elapsed - static_cast<unsigned>(10 + 10 * k)) * 0.1f);
        frame(k, 1.0f - f);
        frame(k + 1, f);
    }
    // stage_route_count_get: Shinjuku 3, Shibuya 2, Aqualine 2; their points at job_route_points
    const std::vector<double> points = ui_values(ui.data, "job", "job_route_mark_points", 26);
    for (int i = 0; i < area_gate_count(a.area); ++i) {
        const int at = 8 + (a.area * 3 + i) * 2;
        put(ui, 0x381, static_cast<float>(value_at(points, at)) + x,
            static_cast<float>(value_at(points, at + 1)), 0x101, 0xff808080);
    }
}

// the area tabs: sliding in over 24 frames on the area page, out over 8 to the job page
void tabs_draw(Ui& ui, AreaSelectScreen& a, float t) {
    const std::vector<double> ids = ui_values(ui.data, "area_select", "area_tab_sprites", 8);
    const float in = clamp01(static_cast<float>(ui.task_timer - static_cast<std::uint32_t>(a.slide_start)) /
                             24.0f);
    for (int i = 0; i < 4; ++i) {
        float x = tab_from[i] + (tab_to[i] - tab_from[i]) * in;
        if (i < 3) {
            x = x + (tab_from[i] - x) * t; // the three area tabs leave toward the job page
        }
        put(ui, value_at(ids, 4 + i), x, tab_y, 0x102);
        a.tab_rects[i] = sprite_rect(ui, value_at(ids, 4 + i), x, tab_y);
        if (i == a.area && t < 1.0f) {
            put(ui, value_at(ids, i), x, tab_y, 0x103, 0xffffffff, 0x2000);
        }
    }
    const float bar_in = clamp01(static_cast<float>(ui.task_timer - static_cast<std::uint32_t>(a.slide_start)) /
                                 23.0f);
    put(ui, tab_bar, -450 + 450 * bar_in, 385, 0x101);
}

// the six items fanning in from the left as the page slides
// decomp: job_menu_draw 0x14EDC
void items_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a, float t) {
    const World& world = *fe.world;
    const std::vector<double> ids = ui_values(ui.data, "job", "job_item_sprites", 12);
    const std::vector<double> at = ui_values(ui.data, "job", "job_item_positions", 12);
    const int g = world.garages.selected;
    const int rule = a.jobs[a.area] >= 0 ? tournament_rule(fe.story_tables, a.jobs[a.area]) : -1;
    const bool rental = rule == 0 || rule == 4 || rule == rule_rental_only;
    for (int k = 0; k < 6; ++k) {
        if (k == 5 && t < 1.0f) {
            continue; // the area page's Return tab stands in for it while sliding
        }
        const float x = static_cast<float>(value_at(at, k * 2)) - (1.0f - t) * (panel_slide + 2.0f * static_cast<float>(k));
        const float y = static_cast<float>(value_at(at, k * 2 + 1));
        std::uint32_t color = 0xffffffff;
        if ((k == 0 && (g < 0 || !a.entry_ok || rule == rule_rental_only)) || (k == 1 && !rental)) {
            color = 0xff404040;
        }
        put(ui, value_at(ids, k * 2), x, y, 0x102, color);
        a.item_rects[k] = sprite_rect(ui, value_at(ids, k * 2), x, y);
        if (k == a.item && t >= 1.0f && a.mode == 0) {
            put(ui, value_at(ids, k * 2 + 1), x, y, 0x103, color, 0x2000);
        }
    }
    if (t >= 1.0f) {
        const std::vector<double> area = ui_values(ui.data, "job", "area_ids", 16);
        put(ui, value_at(area, a.area * 4 + 3), 24, 111, 0x102);
        put(ui, tab_bar, 0, 385, 0x101);
    }
    if (a.area < 3) {
        map_draw(ui, a, (1.0f - t) * panel_slide);
    }
}

} // namespace

int area_gate_count(int area) {
    // stage_entry_points by the area's day stage: Shinjuku 3, Shibuya 2, Aqualine 2
    return area == 0 ? 3 : area == 1 || area == 2 ? 2 : 0;
}

std::string area_help_text(Ui& ui, int area) {
    return string_at_data(ui, 0x1daafc + 4u * static_cast<unsigned>(area));
}

std::string item_help_text(Ui& ui, int item) {
    return string_at_data(ui, 0x1dab0c + 4u * static_cast<unsigned>(item));
}

void area_help_init(Ui& ui, AreaSelectScreen& a) {
    for (int i = 0; i < 4; ++i) {
        help_strip_init(ui, a.area_help[i], area_help_text(ui, i));
    }
    for (int i = 0; i < 6; ++i) {
        help_strip_init(ui, a.item_help[i], item_help_text(ui, i));
    }
    help_strip_init(ui, a.gate_help, string_at_data(ui, 0x1dab24));
    help_strip_init(ui, a.character_help, text_string(ui, "str.17ace8"));
    text_strip_init(ui, a.contest_help, 0, 0x4a, text_string(ui, "str.17ac38"), 0, 0, 640, 480);
}

// decomp: area_select_draw 0x15FCE, job_menu_draw 0x14EDC
void area_select_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    const float t = page_t(ui, a);
    frame_draw(fe, ui, a);
    if (t < 1.0f) {
        panel_draw(fe, ui, a, t);
        tabs_draw(ui, a, t);
    }
    if (t > 0.0f) {
        items_draw(fe, ui, a, t);
    }
    // the help line of the page up
    TextStrip& help = a.card_up == 1                   ? a.character_help
                      : a.card_up == 2                 ? a.contest_help
                      : t >= 1.0f && a.page == 1 ? a.item_help[a.item]
                                                 : a.area_help[a.area];
    text_strip_draw_scroll(ui, help, 0xfff0f0f0);
}

// decomp: job_gate_screen_draw 0x150EA
void gate_screen_draw(FrontEnd&, Ui& ui, AreaSelectScreen& a) {
    put(ui, 0x359, 0, 385, 0x101);
    put(ui, 0x37c, 60, 159, 0x104);
    // icon_on_plate_draw(0xA7, 0xE5, 0x105, 0xC3E, 0x1A)
    put(ui, 0xc32, 0xa7, 0xe5, 0x105);
    put(ui, 0xc3e, 0xa7 + 0x1a, 0xe5 + 2, 0x105);
    const int count = area_gate_count(a.area);
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>((count == 3 ? 81 : 111) + 59 * i);
        put(ui, 0x37d + i, x, 193, 0x105, 0xff808080);
        if (i == a.gate) {
            put(ui, 0x37d + i, x, 193, 0x106, 0xffffd300, 0x2000);
        }
    }
    // job_gates_draw: the entry points on the map, the chosen one ringed
    const std::vector<double> points = ui_values(ui.data, "job", "job_gate_points", 18);
    for (int i = 0; i < count; ++i) {
        const float x = static_cast<float>(value_at(points, (a.area * 3 + i) * 2));
        const float y = static_cast<float>(value_at(points, (a.area * 3 + i) * 2 + 1));
        put(ui, 0x380, x, y, 0x101, 0xff808080, 8);
        a.gate_rects[i] = sprite_rect(ui, 0x380, x, y, true);
        if (i == a.gate) {
            Sprite ring = sprite_of(ui, 0x390);
            ring.pos[0] = x;
            ring.pos[1] = y;
            ring.blend = 0x103;
            ring.color = 0xfffeb90e;
            ring.flags |= 8;
            ring.rot[2] = static_cast<float>(ui.task_timer) * 0.1f;
            sprite_submit(ui, ring);
            put(ui, 0x380, x, y, 0x102, 0xffffcd00, 0x2008);
        }
    }
    text_strip_draw_scroll(ui, a.gate_help, 0xfff0f0f0);
}
