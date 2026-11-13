// The job page's cards and its map's fighters (decomp src/o1/u_012ff5.c: job_characters_update
// and job_character_card_draw, job_contest_info_update and
// job_contest_info_draw, rule_lines_draw, job_map_fighters_draw,
// src/hand/u_043c80.c job_wanderer_draw and job_wanderer_icon_draw; docs/port/
// screens.md "CHARACTERS", "CONTEST INFO").
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/area_select.hpp"
#include "screens/flow/front_end.hpp"
#include "story/roster_tables.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

// the fixed opponents of the area rankers' jobs (kind 1) by area; the First Ranker's is entry 0
constexpr int ranker_entries[4] = {1, 10, 19, -1};
// D_001D70D0: the map's scale per area; job_map_transforms: (a, b, c, d, angle)
constexpr float map_scale[3] = {1.037872f, 1.387872f, 0.787872f};
constexpr float map_offset[3][4] = {{167, 134, 228, 181}, {232, 167, 0, 0}, {226, 160, 0, 0}};
constexpr float map_angle[3] = {0.785398f, 0.0f, 0.0f};
constexpr std::uint32_t cyan = 0xff6dfff1, yellow = 0xffffd300, white = 0xfff0f0f0;

// a table string cut at 40 bytes (20 full-width glyphs)
// decomp: job_text_draw 0x13C6F
void card_text(Ui& ui, const std::string& text, int x, int y, int layer) {
    GlyphText g = glyphs_of(ui, text);
    if (!g.empty() && g.back() == -1) {
        g.pop_back();
    }
    if (g.size() > 20) {
        g.resize(20);
    }
    g.push_back(-1);
    text_draw(ui, g, x, y, layer - 0x101, 18, 18, white, 0x14);
}

const std::string& name_at(const std::vector<std::string>& names, int i) {
    static const std::string none;
    return i >= 0 && i < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(i)] : none;
}

// the fee, its width measured first, the sign left of it
void fee_draw(Ui& ui, long long amount, int x, int y, int layer) {
    const int width = number_draw_ex(ui, 0x104, amount, x, y, layer, white, 9, 10, 3);
    number_draw_ex(ui, 4, amount, x, y, layer, white, 9, 10, 3);
    sprite_draw_at(ui, 0xeaa, static_cast<float>(x + 2 - width), static_cast<float>(y), layer);
}

// job_character_card_draw. In a ranker job the card shows the fixed opponent at its base
// level and the pay at that level. Retail writes both into the roster entry under the cursor,
// which is the ranker's own, so a look at the card took a post-game ranker's level bonus away;
// the port only shows them (docs/bugs.md)
// decomp: job_character_card_draw 0x13F5A
void character_card_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a, const RosterEntry* r) {
    if (!r) {
        return;
    }
    const RosterTables& t = fe.campaign.roster;
    RosterPilot pilot = r->pilot;
    std::string build_name = r->name;
    int pay_shown = r->destruction_value;
    const int kind = tournament_kind(fe.story_tables, a.area, a.jobs[a.area]);
    const int fixed = kind == 1 ? ranker_entries[a.area] : kind == 2 ? 0 : -1;
    if (fixed >= 0) {
        const OpponentEntry& o = opponent_entry(t, fixed);
        pilot = pilot_row(t, o.pilot);
        build_name = name_at(t.build_names, o.loadout);
        const int level = static_cast<std::int16_t>(o.level);
        CurveRow pay;
        pay.base = static_cast<float>(static_cast<unsigned>(pilot.value_base));
        pay.at_mid = static_cast<float>(static_cast<unsigned>(pilot.value_at_mid));
        pay.at_max = static_cast<float>(static_cast<unsigned>(pilot.value_at_max));
        pay.mid = static_cast<std::uint8_t>(pilot.value_mid);
        pay.negative = pilot.value_negative;
        pay_shown = curve_unsigned(pay, static_cast<std::int8_t>(level));
        if (level > 0) {
            const float scale = static_cast<float>(level) * 0.015625f + 1.0f;
            pay_shown =
                static_cast<int>(static_cast<float>(static_cast<unsigned>(pay_shown)) * scale);
        }
    }
    a.card.sprite = 0x39b;
    if (pop_timer_draw(ui, a.card, sprite_width(ui, 0x39b) / 2 + 0x29,
                       sprite_height(ui, 0x39b) / 2 + 0x71, 0x109)) {
        return;
    }
    icon_on_plate_draw(ui, 0xf1, 0x176, 0x10a, 0xc3e, 0x1a);
    const bool named = pilot.identity != -1;
    const bool machine = pilot.scoobee_identity != -1;
    if (named) {
        card_text(ui, name_at(t.identity_names, pilot.identity), 0x3c, 0xd5, 0x10a);
    }
    card_text(ui, build_name, 0x3c, 0xf8, 0x10a);
    if (machine && named) {
        card_text(ui, name_at(t.identity_names, pilot.scoobee_identity), 0x3c, 0x11b, 0x10a);
    }
    // an empty team or "None" shows retail's Japanese "unaffiliated" (docs/bugs.md)
    const std::string& team = name_at(t.team_names, pilot.team + 1);
    const std::string none = name_at(t.team_names, 0);
    card_text(ui, !team.empty() && team != none ? team : text_string(ui, "str.17bd40"), 0x3c, 0x13e,
              0x10a);
    fee_draw(ui, static_cast<unsigned>(pay_shown), 0x3c, 0x161, 0x10a);
    if (named) {
        sprite_draw_at(ui, t.identity_sprites[static_cast<std::size_t>(pilot.identity)], 180, 126,
                       0x10b);
    }
    if (machine && pilot.scoobee_identity < static_cast<int>(t.identity_sprites.size())) {
        sprite_draw_at(ui, t.identity_sprites[static_cast<std::size_t>(pilot.scoobee_identity)],
                       252, 126, 0x10b);
    }
}

// decomp: job_contest_info_draw 0x13D07
void contest_card_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    const int job = a.jobs[a.area];
    if (job < 0) {
        return;
    }
    a.card.sprite = 0x382;
    if (pop_timer_draw(ui, a.card, sprite_width(ui, 0x382) / 2 + 0x28,
                       sprite_height(ui, 0x382) / 2 + 0xa6, 0x109)) {
        return;
    }
    icon_on_plate_draw(ui, 0xe7, 0x176, 0x10a, 0xc3e, 0x1a);
    card_text(ui, name_at(fe.campaign.roster.tournament_names, job), 0x3c, 0xd5, 0x10a);
    rule_lines_draw(fe, ui, job, 0x3c, 0xf8, 0x10a);
    const int rule = tournament_rule(fe.story_tables, job);
    fee_draw(ui, fe.story_tables.rules[static_cast<std::size_t>(rule)].entry_fee, 0x3c, 0x161,
             0x10a);
}

// by body class (8 rows) and leg kind (9 columns)
// decomp: job_map_icon_sprite 0x442F8
int icon_sprite(Ui& ui, int body, int leg) {
    const std::vector<double> ids = ui_values(ui.data, "job", "job_map_icon_sprites", 72);
    const int i = body * 9 + leg;
    return i >= 0 && i < static_cast<int>(ids.size()) ? static_cast<int>(ids[static_cast<std::size_t>(i)])
                                                      : -1;
}

// the icon centred, turned to its heading; the CHARACTERS pick
// ringed
// decomp: job_wanderer_icon_draw 0x4430D
void icon_draw(Ui& ui, int id, float x, float y, int blend, std::uint32_t color, float rot,
               std::uint32_t flags, bool ring) {
    if (id < 0) {
        return;
    }
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.blend = blend;
    s.rot[2] = rot;
    s.color = color;
    s.flags |= flags | 0x10 | 8;
    sprite_submit(ui, s);
    if (ring) {
        Sprite r = sprite_of(ui, 0x390);
        r.pos[0] = x;
        r.pos[1] = y;
        r.blend = 0x103;
        r.rot[0] = r.rot[1] = -0.5f;
        r.rot[2] = static_cast<float>(ui.task_timer) / 10.0f;
        r.color = 0xff81f834;
        r.flags |= 8;
        sprite_submit(ui, r);
    }
}

} // namespace

// decomp: rule_lines_draw 0x1469D
void rule_lines_draw(FrontEnd& fe, Ui& ui, int tournament, int x, int y, int layer) {
    const int id = tournament_rule(fe.story_tables, tournament);
    if (id < 0) {
        return;
    }
    const RuleRow& rule = fe.story_tables.rules[static_cast<std::size_t>(id)];
    const std::string limits = text_string(ui, "str.17bd28"); // "No limits."
    const std::string& subtitle = name_at(fe.campaign.roster.rule_subtitles, id);
    bool legs = false;
    for (const int c : rule.leg_classes) {
        legs = legs || (c != 8 && c >= 0);
    }
    card_text(ui, rule.body_class == 7 ? limits : subtitle, x, y, layer);
    card_text(ui, legs ? subtitle : limits, x, y + 35, layer);
    card_text(ui, rule.extra == 0 || rule.extra == 1 ? subtitle : limits, x, y + 70, layer);
}

// decomp: job_map_fighter_count 0x1476F
int job_map_fighter_count(const FrontEnd& fe, const AreaSelectScreen& a) {
    if (a.area < 0 || a.area >= 3) {
        return 0;
    }
    const RumbleSlot& r = fe.world->rumble[a.area];
    int count = 0;
    for (int i = 1; i < r.entrants; ++i) {
        count += r.roster[i].player == -1 ? 1 : 0;
    }
    return count;
}

// decomp: job_map_fighters_draw 0x1492B
void job_map_fighters_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a, int selected) {
    if (a.area < 0 || a.area >= 3) {
        return;
    }
    const RosterTables& t = fe.campaign.roster;
    const int kind = tournament_kind(fe.story_tables, a.area, a.jobs[a.area]);
    if (kind != 0) {
        // a ranker job: only the ranker, yellow, at the rolled gate
        const int fixed = kind == 1 ? ranker_entries[a.area] : 0;
        if (fixed < 0) {
            return;
        }
        const RosterBuild& b = roster_build_row(t, opponent_entry(t, fixed).loadout);
        const std::vector<double> points = ui_values(ui.data, "job", "job_gate_points", 18);
        const int at = (a.area * 3 + a.ranker_gate[a.area]) * 2;
        Sprite s = sprite_of(ui, icon_sprite(ui, build_body_class_or_7(t, b), build_leg_kind_or_8(t, b)));
        s.flags |= 8;
        s.pos[0] = static_cast<float>(points[static_cast<std::size_t>(at)]);
        s.pos[1] = static_cast<float>(points[static_cast<std::size_t>(at + 1)]);
        s.blend = 0x102;
        s.color = yellow;
        sprite_submit(ui, s);
        return;
    }
    const RumbleSlot& r = fe.world->rumble[a.area];
    const float scale = map_scale[a.area];
    const float* off = map_offset[a.area];
    const float angle = map_angle[a.area];
    for (int i = 1; i < r.entrants && i < roster_size; ++i) {
        if (r.roster[i].player == -2) {
            continue;
        }
        const JobWanderer& w = a.wanderers[i];
        // job_wanderer_draw: the original's x and -z (the port's z) scaled, moved by
        // (a, b) - (c, d), turned about the vertical by the area's angle, then (234 + c, 73 + d)
        float x = w.pos[0] * scale + off[0] - off[2];
        float z = w.pos[2] * scale + off[1] - off[3];
        const float c = std::cos(angle), s = std::sin(angle);
        const float rx = x * c + z * s;
        const float rz = -x * s + z * c;
        x = 234.0f + off[2] + rx;
        z = 73.0f + off[3] + rz;
        // the heading: the walk's direction in the original's frame (z negated back)
        float dx = w.dir[0], dz = -w.dir[2];
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len > 0.0f) {
            dx /= len, dz /= len;
        }
        const float rot = -angle - 1.5707964f - std::atan2(dz, dx);
        const bool on = i == selected;
        icon_draw(ui, icon_sprite(ui, w.body_class, w.leg_kind), x, z, 0x102 + i, on ? yellow : cyan,
                  rot, on ? 0x2000 : 0, on);
    }
}

// decomp: job_characters_update 0x15AA2, job_contest_info_update 0x15BCB
void job_characters_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    RumbleSlot& r = fe.world->rumble[a.area];
    const int entrants = r.entrants;
    const int before = a.character;
    // a silent ring over 1..entrants-1 that wraps even held, then on past entries not on the map
    int step = menu_cursor_move(ui.pad, a.character, 1, entrants - 1, 0x1f);
    if (step == 0) {
        step = 1;
    }
    RosterEntry* at = nullptr;
    for (int k = 1; k < entrants && entrants > 1; ++k) {
        at = &r.roster[a.character];
        if (at->player == -1) {
            break;
        }
        a.character = (a.character + entrants + step) % entrants;
        if (a.character == 0) {
            a.character = (step + entrants) % entrants;
        }
    }
    if (before != a.character) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
    job_map_fighters_draw(fe, ui, a, a.character);
    area_select_draw(fe, ui, a);
    character_card_draw(fe, ui, a, at);
    if (menu_cancel(ui)) {
        // retail plays 8 twice here (two starts of one cue); once in the port (docs/bugs.md)
        ui_sound(ui, bank_sys, se_menu_cancel);
        a.card_up = 0;
        fe.screen.step = 4;
    }
}

void job_contest_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    area_select_draw(fe, ui, a);
    job_map_fighters_draw(fe, ui, a, -1);
    contest_card_draw(fe, ui, a);
    if (menu_cancel(ui)) {
        // retail also plays 0xC and re-arms a box nothing draws (docs/bugs.md)
        ui_sound(ui, bank_sys, se_menu_cancel);
        a.card_up = 0;
        fe.screen.step = 4;
    }
}
