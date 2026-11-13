// The result screen (state 0x1E, decomp src/od/u_0933f0.c result_screen_* 0x94E50..0x96C6C;
// docs/port/screens.md "Result screen", battle.md "What the battle hands back"). It runs at 30 Hz:
// `clock`, `trophy_clock` and the fade timers step by 2 ticks an update. The settlement moves the
// money as the figures roll; leaving by EXIT commits the records. Behind the sheet main runs the
// demo battle (Campaign::battle_wanted 2, src/battle_run.cpp); over it the trophy, in 3D as render
// pass 13 (src/screens/system/trophy_models.hpp), with its caption and the veil.
#include "assets/pack_sexp.hpp"
#include "audio/sound_cues.hpp"
#include "cook/pack_library.hpp"
#include "cook/sexp_read.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// the trophies' poses (the dynamic initializers 0x11E550..0x11E780), the original's frame: for
// trophies 0..3, 4 and 5..7 the start place and turns (x, y, z), then the end's
struct TrophyPose {
    float start[3], start_turn[3], end[3], end_turn[3];
};
constexpr TrophyPose trophy_poses[3] = {
    {{0, -2, -3}, {-0.39f, 0, 0}, {4.6f, 0.1f, 3.6f}, {-0.4f, 0, 0.11f}},
    {{0, -0.6f, -3}, {-0.25f, 0, 0}, {4.3f, 0.6f, 2.3f}, {-0.33f, 0, 0.07f}},
    {{0, -0.8f, -4}, {-0.33f, 0, 0}, {3.2f, 0.2f, -0.7f}, {-0.33f, 0, 0.07f}}};
// the trophy's moment: result_screen_trophy_wait (the port's step after the fade in)
constexpr int step_trophy_wait = 8;

// result_screen_setup's trophy: with the fixed opponent destroyed, today's tournament of the area
// fought looked up in battle_tournament_trophy_map (tables/trophies.sexp), if it wasn't already
// won when the fight began; -1 none
int trophy_of(Ui& ui, const World& world, const BattleResult& result) {
    if (!result.fixed_opponent_destroyed || result.tournament_won_at_start) {
        return -1;
    }
    const int today = world.story.lineup[rumble_slot_of_selector(world.story.rumble_selected)];
    gsexp::ParseResult parsed;
    std::string error;
    if (!pack_sexp(ui.data.packs->core, "tables/trophies.sexp", parsed, error)) {
        std::fprintf(stderr, "result: %s (pc_extract --only scenes)\n", error.c_str());
        return -1;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || !form.head().is_atom("trophy-map")) {
            continue;
        }
        for (gsexp::Node pair : form.children()) {
            if (pair.is_list() && atom_int(pair.child_at(0)) == today) {
                return atom_int(pair.child_at(1));
            }
        }
    }
    return -1;
}

// the pose by trophy_clock (the start, then 15 updates to the end,
// turning 0.02 an update from 0 on), the caption sliding in and out (pass 14), and the veil over
// the demo battle fading out over 100 ticks once below 0
// decomp: result_trophy_draw 0x963E6
void trophy_draw_all(Ui& ui, ResultScreen& r) {
    if (r.trophy < 0 || r.trophy > 7) {
        return;
    }
    const float c = r.trophy_clock;
    if (c < 0.0f) {
        r.trophy_yaw += 0.02f;
    }
    const TrophyPose& pose = trophy_poses[r.trophy <= 3 ? 0 : r.trophy == 4 ? 1 : 2];
    const float t = c >= 0.0f ? 0.0f : c > -30.0f ? -c / 30.0f : 1.0f;
    float at[3], turn[3];
    for (int k = 0; k < 3; ++k) {
        at[k] = pose.start[k] + (pose.end[k] - pose.start[k]) * t;
        turn[k] = pose.start_turn[k] + (pose.end_turn[k] - pose.start_turn[k]) * t;
    }
    const std::vector<double> slots = ui_values(ui.data, "result", "battle_trophy_model_slots", 8);
    trophy_draw(ui, r.models, static_cast<int>(slots[static_cast<std::size_t>(r.trophy)]), at,
                turn, r.trophy_yaw, 13);
    if (const UiTable* captions = ui_table(ui.data, "result", "result_trophy_captions")) {
        if (r.trophy < static_cast<int>(captions->slots.size())) {
            SpriteSlot row = captions->slots[static_cast<std::size_t>(r.trophy)];
            const float slide_x = c >= 150.0f ? (c - 150.0f) * 30.0f : c < 0.0f ? c * 30.0f : 0.0f;
            row.x += std::max(-640.0f, std::min(640.0f, slide_x));
            sprite_row_draw(ui, row);
        }
    }
    if (c > -100.0f) {
        Sprite veil = sprite_of(ui, 0xe7c);
        veil.pos[0] = veil.pos[1] = -32.0f;
        veil.scale[0] = 22.0f;
        veil.scale[1] = 17.0f;
        veil.blend = 0x101;
        veil.flags |= quad_full_window; // over the demo battle's whole window
        const float alpha = 160.0f * std::min(1.0f, 1.0f + c / 100.0f);
        veil.color = static_cast<std::uint32_t>(alpha) << 24;
        sprite_submit(ui, veil);
    }
}

constexpr int common_asset = 0x690;
constexpr int result_asset = 0x680; // result_tex
constexpr int money_max = 99999999;
constexpr float clock_end = 3600.0f;
constexpr float rolls_start = 140.0f;

int ticks(const Ui& ui) {
    return 60 / ui.frame_cadence;
}

// target / 15 an update, at least 1
// decomp: result_figure_roll 0x9691B
void figure_roll(int& value, int target, const Ui& ui) {
    unsigned step = static_cast<unsigned>(static_cast<float>(static_cast<unsigned>(target)) /
                                          (static_cast<float>(ui.frame_cadence) * 0.5f));
    step = step < 1 ? 1 : step > static_cast<unsigned>(target) ? static_cast<unsigned>(target) : step;
    unsigned v = static_cast<unsigned>(value) + step;
    value = static_cast<int>(v > static_cast<unsigned>(target) ? static_cast<unsigned>(target) : v);
}

int money_after(const ResultScreen& r, const BattleResult& result) {
    const long long m = static_cast<long long>(result.money_before) + r.settlement;
    return static_cast<int>(m < 0 ? 0 : m > money_max ? money_max : m);
}

// decomp: result_figures_finish 0x969C2
void figures_finish(FrontEnd& fe, ResultScreen& r) {
    const BattleResult& result = fe.world->battle_state.result;
    r.shown_destruction = result.destruction_count;
    r.shown_elapsed = result.elapsed;
    r.shown_prize = result.prize;
    for (int i = 0; i < 4; ++i) {
        r.shown_weapon_repair[i] = result.weapon_repair[i];
    }
    r.shown_body_leg_repair = result.body_leg_repair;
    r.shown_repair_total = result.repair_total;
    if (r.from != app_replay) {
        fe.world->story.money = money_after(r, result);
    }
    r.clock = clock_end;
}

// decomp: result_rows_fill 0x96B2C
void rows_fill(FrontEnd& fe, ResultScreen& r) {
    const BattleResult& result = fe.world->battle_state.result;
    r.rows[1] = result.money_before;
    r.rows[2] = r.shown_prize;
    for (int i = 0; i < 4; ++i) {
        r.rows[4 + i] = r.shown_weapon_repair[i];
    }
    r.rows[8] = r.shown_body_leg_repair;
    r.rows[9] = r.shown_repair_total;
    r.rows[10] = fe.world->story.money;
}

void row_draw_scaled(Ui& ui, const SpriteSlot& row, float sx, float sy) {
    Sprite s = sprite_of(ui, row.sprite);
    s.pos[0] = row.x;
    s.pos[1] = row.y;
    s.blend = row.blend;
    s.color = row.color;
    s.z = row.z;
    s.flags |= row.flags;
    s.scale[0] = sx;
    s.scale[1] = sy;
    sprite_submit(ui, s);
}

// decomp: result_slide_offset 0x96B00
float slide(float clock, float step) {
    return clock < step ? (step - clock) * 30.0f : 0.0f;
}

// decomp: result_sheet_draw 0x95C4C
void sheet_draw(FrontEnd& fe, Ui& ui, ResultScreen& r) {
    const BattleResult& result = fe.world->battle_state.result;
    Sprite top = sprite_of(ui, 0x8e7);
    top.blend = 0x102;
    sprite_submit(ui, top);
    Sprite bottom = sprite_of(ui, 0x8d2);
    bottom.pos[1] = 376.0f;
    bottom.blend = 0x102;
    sprite_submit(ui, bottom);
    text_strip_draw_scroll(ui, r.text, 0xfff0f0f0);
    const UiTable* header = ui_table(ui.data, "result", "result_header_sprites");
    for (int i = 0; i < 11 && header && i < static_cast<int>(header->slots.size()); ++i) {
        const float at = static_cast<float>(i) * 10.0f + 30.0f;
        if (r.clock < at) {
            break;
        }
        float sy = (r.clock - at) / 10.0f;
        sy = sy < 0.0f ? 0.0f : sy > 1.0f ? 1.0f : sy;
        const float sx = i == 0 ? sy : 1.0f;
        const SpriteSlot& row = header->slots[static_cast<std::size_t>(i)];
        row_draw_scaled(ui, row, sx, sy);
        if (i == 2 && result.rented && header->slots.size() > 13) {
            // RENTAL alternating each second of the demo battle (Battle.frame / 30 % 2 updates;
            // the port's frame counts steps), the screen's own clock while it isn't up
            const BattleState& demo = fe.world->battle_state;
            const int alt = demo.live && fe.world->battle_phase == 1
                                ? demo.frame / 60 % 2
                                : static_cast<int>(ui.task_timer / 60u % 2u);
            row_draw_scaled(ui, header->slots[static_cast<std::size_t>(12 + alt)], sx, sy);
        }
        const int y = static_cast<int>(row.y);
        if (i == 0 || i == 3) {
            continue;
        }
        const unsigned value = static_cast<unsigned>(r.rows[i]);
        if (i >= 4 && i <= 9 && value > 0) {
            number_draw_ex(ui, 0x4c, value, 312, y, 0x104, 0xffff0000, 9, 1, 3);
            int digits = 1;
            for (unsigned v = value / 10; v; v /= 10) {
                ++digits;
            }
            Sprite minus = sprite_of(ui, 0xea4);
            minus.pos[0] = 312.0f - static_cast<float>(digits + 3) * 11.0f;
            minus.pos[1] = static_cast<float>(y);
            minus.blend = 0x103;
            minus.color = 0xffff0000;
            sprite_submit(ui, minus);
        } else if (i == 2) {
            int digits = 1;
            for (unsigned v = value / 10; v; v /= 10) {
                ++digits;
            }
            const std::uint32_t color = value > 0 ? 0xff84ff00 : 0xffffffff;
            Sprite plus = sprite_of(ui, 0xea5);
            plus.pos[0] = 312.0f - static_cast<float>(digits + 3) * 11.0f;
            plus.pos[1] = static_cast<float>(y);
            plus.blend = 0x103;
            plus.color = color;
            sprite_submit(ui, plus);
            number_draw_ex(ui, 0x4c, value, 312, y, 0x104, color, 9, 1, 3);
        } else {
            number_draw_ex(ui, 0x4c, value, 312, y, 0x104, 0xffffffff, 9, 1, 3);
        }
    }
    // the grade sliding in from the right over the first 30 ticks
    const std::vector<double> grades = ui_values(ui.data, "result", "battle_result_sprites", 7);
    if (r.rank >= 0) {
        Sprite grade = sprite_of(ui, static_cast<int>(grades[static_cast<std::size_t>(r.rank)]));
        grade.pos[0] = 357.0f + slide(r.clock, 30.0f);
        grade.pos[1] = 249.0f;
        grade.blend = 0x103;
        sprite_submit(ui, grade);
    }
    const float s10 = slide(r.clock, 10.0f);
    Sprite kills = sprite_of(ui, 0x8e6);
    kills.pos[0] = 336.0f + s10;
    kills.pos[1] = 311.0f;
    kills.blend = 0x103;
    sprite_submit(ui, kills);
    number_draw_ex(ui, 0x58, r.shown_destruction, static_cast<int>(578.0f + s10), 0x127, 0x104,
                      0xffffffff, 4, 1, 4);
    const float s20 = slide(r.clock, 20.0f);
    Sprite time = sprite_of(ui, 0x8dc);
    time.pos[0] = 336.0f + s20;
    time.pos[1] = 336.0f;
    time.blend = 0x103;
    sprite_submit(ui, time);
    time_draw_seconds(ui, r.shown_elapsed, static_cast<int>(487.0f + s20), 0x14e, 0x104,
                      0xffffffff, 2);
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
}

// REPLAY and EXIT, the chosen one lit
// decomp: result_menu_draw 0x96387
void menu_draw(Ui& ui, ResultScreen& r) {
    const UiTable* items = ui_table(ui.data, "result", "result_menu_sprites");
    const UiTable* lit = ui_table(ui.data, "result", "result_menu_sprites_2");
    for (int i = 0; i < 2; ++i) {
        if (items && i < static_cast<int>(items->slots.size())) {
            sprite_row_draw(ui, items->slots[static_cast<std::size_t>(i)]);
        }
        if (r.choice == i && lit && i < static_cast<int>(lit->slots.size())) {
            sprite_row_draw(ui, lit->slots[static_cast<std::size_t>(i)]);
        }
    }
}

// the grade (result_screen_setup): -1 alive with no kills, 0 dead, 1..3 by kills, 4..6
// with the fixed opponent destroyed
// decomp: result_screen_setup 0x94F6E
int grade_of(const BattleResult& r) {
    if (!r.won) {
        return 0;
    }
    const unsigned kills = static_cast<unsigned>(r.destruction_count);
    if (r.fixed_opponent_destroyed) {
        return kills >= 40 ? 6 : kills >= 20 ? 5 : 4;
    }
    return kills >= 20 ? 3 : kills >= 5 ? 2 : kills == 0 ? -1 : 1;
}

} // namespace

// decomp: result_screen_spawn 0x94E50
void result_screen_spawn(FrontEnd& fe, Ui&) {
    ResultScreen& r = fe.screen.campaign.result;
    r = ResultScreen{};
    r.from = fe.previous;
    r.countdown = 16;
}

void result_screen_step(FrontEnd& fe, Ui& ui) {
    ResultScreen& r = fe.screen.campaign.result;
    World& world = *fe.world;
    const BattleResult& result = world.battle_state.result;
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    const float tk = static_cast<float>(ticks(ui));
    switch (fe.screen.step) {
    case 0: // result_screen_wait: 17 updates
        if (--r.countdown < 0) {
            fe.screen.step = 1;
        }
        break;
    case 1: // result_screen_setup
        r.choice = 0;
        texture_set_load(ui, common_asset);
        texture_set_load(ui, result_asset);
        r.clock = 0.0f;
        r.settlement = battle_result_settlement(result, world.story);
        r.trophy_clock = 180.0f;
        r.rank = grade_of(result);
        r.shown_destruction = result.destruction_count;
        r.shown_elapsed = result.elapsed;
        r.trophy = trophy_of(ui, world, result);
        // PHANTOMCRASH_DEV_TROPHY=N (headless captures): trophy N whatever the fight was
        if (const char* dev = SDL_getenv("PHANTOMCRASH_DEV_TROPHY")) {
            r.trophy = std::atoi(dev);
        }
        r.trophy_yaw = 0.0f;
        // the demo battle behind the sheet (main's battle session: src/battle_run.cpp)
        fe.campaign.battle_wanted = 2;
        help_strip_init(ui, r.text, string_at_data(ui, 0x249840));
        std::printf("result: %s prize %d repairs %d settlement %d money %d\n",
                    result.won ? "returned" : "destroyed", result.prize, result.repair_total,
                    r.settlement, world.story.money);
        fe.screen.step = 2;
        break;
    case 2: // result_screen_wait_battle
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        level_meter_spawn(ui, fe.screen.meter);
        r.timer = static_cast<float>(ui.frame_cadence) * 0.5f;
        fade_in_start(ui, r.timer);
        if (r.from == app_replay) {
            r.trophy_clock = -30.0f;
            figures_finish(fe, r);
        }
        fe.screen.step = 3;
        break;
    case 3: // result_screen_fade_in: then WIN or LOSE looped
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        r.timer -= tk;
        if (r.timer < 0.0f) {
            ui_music(ui, result.won ? 3 : 4, true);
            r.timer = 0.0f;
            if (r.from != app_replay) {
                fe.screen.step = r.trophy >= 0 ? step_trophy_wait : 4;
            } else {
                figures_finish(fe, r);
                fe.screen.step = 5;
            }
        }
        break;
    case 4: { // result_screen_count: the rows open, the figures roll one at a time, the money
        if (menu_accept(ui) || menu_cancel(ui)) {
            ui_sound(ui, bank_sys, menu_accept(ui) ? se_menu_accept : se_menu_cancel);
            figures_finish(fe, r);
            fe.screen.step = 5;
        }
        if (r.clock < clock_end) {
            r.clock += tk;
        }
        r.trophy_clock -= tk;
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        if (fe.screen.step != 4 || r.clock < rolls_start) {
            break;
        }
        const int target = money_after(r, result);
        if (r.shown_prize != result.prize) {
            figure_roll(r.shown_prize, result.prize, ui);
            break;
        }
        bool rolled = false;
        for (int i = 0; i < 4 && !rolled; ++i) {
            if (r.shown_weapon_repair[i] != result.weapon_repair[i]) {
                figure_roll(r.shown_weapon_repair[i], result.weapon_repair[i], ui);
                rolled = true;
            }
        }
        if (rolled) {
            break;
        }
        if (r.shown_body_leg_repair != result.body_leg_repair) {
            figure_roll(r.shown_body_leg_repair, result.body_leg_repair, ui);
            break;
        }
        if (r.shown_repair_total != result.repair_total) {
            figure_roll(r.shown_repair_total, result.repair_total, ui);
            break;
        }
        if (world.story.money != target) {
            // the money rolls by a fifteenth of the take each update; retail rolls only a gain
            // and lands a loss in one update (docs/bugs.md: both roll here)
            int step = r.settlement;
            const float half = static_cast<float>(ui.frame_cadence) * 0.5f;
            if (static_cast<float>(step < 0 ? -step : step) > half) {
                step = static_cast<int>(static_cast<float>(step) / half);
            }
            long long m = static_cast<long long>(world.story.money) + step;
            m = r.settlement >= 0 ? (m > target ? target : m) : (m < target ? target : m);
            world.story.money = static_cast<int>(m);
            break;
        }
        figures_finish(fe, r);
        fe.screen.step = 5;
        break;
    }
    case 5: { // result_screen_menu
        PointerRect rects[2];
        const UiTable* items = ui_table(ui.data, "result", "result_menu_sprites");
        for (int i = 0; i < 2 && items && i < static_cast<int>(items->slots.size()); ++i) {
            const SpriteSlot& row = items->slots[static_cast<std::size_t>(i)];
            rects[i] = sprite_rect(ui, row.sprite, row.x, row.y);
        }
        if (menu_cursor(ui, r.choice, 0, 2, cursor_menu_horizontal, 0, 4, rects) == 0) {
            menu_wheel(ui, r.choice, 0, 2, true, 0, 4);
        }
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        menu_draw(ui, r);
        if (menu_accept(ui, &rects[r.choice])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            r.timer = static_cast<float>(ui.frame_cadence) * 0.5f;
            fade_out_start(ui, r.timer);
            fe.screen.step = 6;
        } else if (menu_cancel(ui) && r.choice != 1) {
            r.choice = 1;
            ui_sound(ui, bank_sys, se_menu_cancel);
        }
        break;
    }
    case 6: // result_screen_leave
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        menu_draw(ui, r);
        r.timer -= tk;
        if (r.timer < 0.0f) {
            ui_music_stop(ui);
            fe.screen.strip.active = false;
            task_header_set(fe, r.choice == 1 ? 1 : 0);
            fe.screen.step = 7;
        }
        break;
    case step_trophy_wait: // result_screen_trophy_wait: no input, the clock stands still
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        r.trophy_clock -= tk;
        if (r.trophy_clock < 0.0f) {
            fe.screen.step = 4;
        }
        break;
    default:
        rows_fill(fe, r);
        sheet_draw(fe, ui, r);
        menu_draw(ui, r);
        break;
    }
    // the trophy over the page, from the fade in on
    if (fe.screen.step >= 3) {
        trophy_draw_all(ui, r);
    }
}

// decomp: result_screen_destroy 0x94EC3
void result_screen_destroy(FrontEnd& fe, Ui& ui) {
    ResultScreen& r = fe.screen.campaign.result;
    ui_music_stop(ui);
    // the demo battle stops and the trophy model goes (result_screen_destroy, demo_battle_destroy)
    fe.campaign.battle_stop = true;
    trophy_models_release(ui, r.models);
    // EXIT commits the records (battle_result_commit_stats) and clears the result
    if (r.choice == 1) {
        World& world = *fe.world;
        battle_result_commit(world.battle_state.result, world.profile, world.story, r.settlement);
    }
}
