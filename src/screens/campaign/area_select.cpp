// Area select and the job menu (state 3, decomp src/o1/u_012ff5.c area_select_* and job_*
// 0x137C3..0x16D0E; docs/port/screens.md "Area select and the job menu", progression.md "Gates").
// The area page picks Shinjuku, Shibuya or Aqualine (or Return); the job page enters the day's
// tournament there with the boarded Scoobee (ENTRY) or a rental (RENTAL), sends the player to the
// hangar, or shows the contest; gate select then pays the fee, builds the player's roster entry
// and starts the battle. Drawing is area_select_draw.cpp.
#include "screens/campaign/area_select.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/music_player.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

namespace {

constexpr int sheets[4] = {0x690, 0x667, 0x668, 0x683}; // scr_common, area_select, situation, schedule
// tournament_kind: 1 the area's ranker, 2 the First Ranker
// the rentals: builds 0x54..0x57, one by CRT rand (the world's generator here)
constexpr int rental_builds[4] = {0x54, 0x55, 0x56, 0x57};

int job_rule(const FrontEnd& fe, const AreaSelectScreen& a) {
    const int job = a.jobs[a.area];
    return job >= 0 ? tournament_rule(fe.story_tables, job) : -1;
}

void box_open(AreaSelectScreen& a, int box, int step_back) {
    a.box = box;
    a.box_return = step_back;
    msg_box_open(a.boxes[box]);
}

// job_rental_allowed: rules 0, 4 and 25
bool rental_allowed(const FrontEnd& fe, const AreaSelectScreen& a) {
    const int rule = job_rule(fe, a);
    return rule == 0 || rule == 4 || rule == rule_rental_only;
}

// area_select_leave's start (area_select_fade_out)
// decomp: area_select_fade_out 0x1639D
void leave(Ui& ui, AreaSelectScreen& a, int page) {
    a.leave_page = page;
    a.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
    fade_out_start(ui, static_cast<float>(a.frames));
}

} // namespace

RosterBuild garage_build(const Garages& g, int garage) {
    RosterBuild b;
    b.body = b.leg = b.chip = -1;
    for (RosterWeapon& w : b.weapon) {
        w.id = -1;
    }
    for (std::int16_t& option : b.option) {
        option = -1;
    }
    if (garage < 0 || garage >= garage_count) {
        return b;
    }
    const Garage& l = g.garages[garage];
    const auto item = [&](int index) -> const InventoryItem* {
        return index >= 0 && index < inventory_size ? &g.inventory[index] : nullptr;
    };
    if (const InventoryItem* i = item(l.body)) {
        b.body = i->part;
        b.body_tune = i->tune;
    }
    if (const InventoryItem* i = item(l.leg)) {
        b.leg = i->part;
        b.leg_tune = i->tune;
    }
    if (const InventoryItem* i = item(l.chip)) {
        b.chip = i->part;
        b.chip_tune = i->tune;
    }
    const std::int16_t mounts[4] = {l.arm[0], l.arm[1], l.back[0], l.back[1]};
    for (int m = 0; m < 4; ++m) {
        if (const InventoryItem* i = item(mounts[m])) {
            b.weapon[m].id = i->part;
            b.weapon[m].tune = i->tune;
        }
    }
    for (int k = 0; k < 10; ++k) {
        if (const InventoryItem* i = item(l.option[k])) {
            b.option[k] = i->part;
        }
    }
    b.color = l.color;
    b.sticker = l.sticker;
    return b;
}

// decomp: job_entry_allowed 0x137D6
bool job_entry_allowed(const FrontEnd& fe, const AreaSelectScreen& a) {
    const World& world = *fe.world;
    const int job = a.jobs[a.area];
    if (job < 0) {
        return false;
    }
    const RuleRow& rule =
        fe.story_tables.rules[static_cast<std::size_t>(tournament_rule(fe.story_tables, job))];
    if (static_cast<unsigned>(rule.entry_fee) > static_cast<unsigned>(world.story.money)) {
        return false;
    }
    // retail reads the boarded garage's records before testing one is boarded, and through a
    // missing body or legs; the index first here, a missing part refused (docs/bugs.md)
    const int g = world.garages.selected;
    if (g < 0 || g >= garage_count) {
        return false;
    }
    const RosterBuild build = garage_build(world.garages, g);
    const RosterTables& roster = fe.campaign.roster;
    const int body_class = build_body_class(roster, build);
    const int leg_class = build_leg_class(roster, build);
    if (body_class < 0 || leg_class < 0) {
        return false;
    }
    if (rule.body_class >= 0 && rule.body_class <= 2 && body_class != rule.body_class) {
        return false;
    }
    bool ok = false;
    for (const int want : rule.leg_classes) {
        if (want == 8 || (want >= 0 && want <= 7 && leg_kind_group_of(leg_class) == want)) {
            ok = true;
        }
    }
    if (!ok) {
        return false;
    }
    // what the hangar refuses (garage_status): no weapon, or 0.05 over the legs'
    // capacity; retail's entry check works the weight out and drops it (docs/bugs.md)
    bool armed = false;
    for (const RosterWeapon& w : build.weapon) {
        armed = armed || w.id != -1;
    }
    const PartRow* legs = leg_row(roster, build.leg);
    if (!armed || (legs && build_weight_float(roster, build) -
                                   curve_float(legs->capacity, build.leg_tune) >=
                               0.05f)) {
        return false;
    }
    if (rule.extra == 0) {
        return false;
    }
    if (rule.extra == 1 && build_camo_frames(roster, build) > 0) {
        return false;
    }
    return true;
}

void area_select_spawn(FrontEnd& fe, Ui&) {
    AreaSelectScreen& a = fe.screen.campaign.area;
    a = AreaSelectScreen{};
    a.from = fe.previous;
}

void area_select_step(FrontEnd& fe, Ui& ui) {
    AreaSelectScreen& a = fe.screen.campaign.area;
    World& world = *fe.world;
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    switch (fe.screen.step) {
    case 0: { // area_select_screen_update
        for (const int sheet : sheets) {
            texture_set_load(ui, sheet);
        }
        level_meter_spawn(ui, fe.screen.meter);
        // the ranker gate rolls for each area's day stage (rand_float_crt: the world's here)
        for (int s = 0; s < 3; ++s) {
            const int gates = area_gate_count(s);
            int roll = rand_int(world.random, gates);
            a.ranker_gate[s] = roll < 0 ? 0 : roll >= gates ? gates - 1 : roll;
        }
        for (int s = 0; s < 4; ++s) {
            a.jobs[s] = world.story.lineup[s];
            const int rule = a.jobs[s] >= 0 ? tournament_rule(fe.story_tables, a.jobs[s]) : -1;
            a.fee_ok[s] = rule >= 0 && static_cast<unsigned>(world.story.money) >=
                                           static_cast<unsigned>(fe.story_tables.rules[static_cast<std::size_t>(rule)].entry_fee);
        }
        area_help_init(ui, a);
        pop_timer_init(a.card, 0x39b, 5, 10);
        for (int i = 0; i < 7; ++i) {
            a.boxes[i] = msg_box_from(ui, "job", "job_refusal_boxes", i);
        }
        a.frames = std::max(static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f), 24) + 4;
        a.entry_ok = job_entry_allowed(fe, a);
        a.slide_start = static_cast<int>(ui.task_timer);
        if (a.from == app_hangar) {
            // back from the hangar: on the job page, "Change the Scoobee"
            a.area = a.previous_area = fe.campaign.hangar_return_area;
            area_pick(world.story, fe.story_tables, a.area);
            job_wanderers_start(fe, ui, a);
            a.item = 2;
            a.mode = 0;
            a.page = 1;
        }
        a.strip.shown = a.area;
        music_shuffle_catalogue(fe.music);
        fade_in_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
        fe.screen.step = 1;
        break;
    }
    case 1: // area_select_fade_in and _end: the strip under 24, the banner at the end
        area_select_draw(fe, ui, a);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (a.frames == 24) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            a.slide_start = static_cast<int>(ui.task_timer);
        }
        if (--a.frames < -24) {
            a.banner_timer = static_cast<int>(ui.task_timer);
            if (a.page == 1) {
                job_menu_enter(fe, ui, a);
                fe.screen.step = 4;
            } else {
                fe.screen.step = 2;
            }
        }
        break;
    case 2: { // area_select_cursor_update (the shuffle once a step: retail's second pump
              // here is dropped, docs/bugs.md)
        if (menu_cursor(ui, a.area, 0, 4, cursor_menu_horizontal, 0, 4, a.tab_rects)) {
            a.previous_area = a.strip.shown;
            a.strip.shown = a.area;
            a.strip.start = static_cast<int>(ui.task_timer);
            help_strip_init(ui, a.area_help[a.area], area_help_text(ui, a.area));
        }
        area_select_draw(fe, ui, a);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (menu_accept(ui)) {
            switch (area_entry_status(world.story, fe.story_tables, a.area)) {
            case EntryStatus::Shop: // Return
                ui_sound(ui, bank_sys, se_menu_accept);
                fe.screen.strip.active = false;
                a.result = 1;
                leave(ui, a, 1);
                fe.screen.step = 8;
                break;
            case EntryStatus::NoTournament:
                ui_sound(ui, bank_sys, se_menu_cancel);
                break;
            case EntryStatus::NoFee:
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 0, 2);
                fe.screen.step = 7;
                break;
            case EntryStatus::RankerLocked:
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 5, 2);
                fe.screen.step = 7;
                break;
            case EntryStatus::FirstRankerLocked:
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 6, 2);
                fe.screen.step = 7;
                break;
            case EntryStatus::Allowed:
                // job_area_open: rumble_selected = area * 2 + night; the slide to the job page
                ui_sound(ui, bank_sys, se_menu_accept);
                a.item = 0;
                area_pick(world.story, fe.story_tables, a.area); // job_area_open
                job_wanderers_start(fe, ui, a);
                a.page_change = static_cast<int>(ui.task_timer);
                a.mode = 1;
                a.frames = 8;
                fe.screen.step = 3;
                break;
            }
        } else if (menu_cancel(ui) && a.area != 3) {
            a.area = 3;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            help_strip_init(ui, a.area_help[3], area_help_text(ui, 3));
        }
        break;
    }
    case 3: // area_select_to_jobs: 8 frames of the slide
        area_select_draw(fe, ui, a);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (--a.frames < 0) {
            job_menu_enter(fe, ui, a);
            fe.screen.step = 4;
        }
        break;
    case 4: // job_menu_update
        job_menu_step(fe, ui, a);
        break;
    case 5: // job_menu_to_areas: back to the area page
        area_select_draw(fe, ui, a);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (--a.frames < 0) {
            a.mode = 0;
            a.page = 0;
            fe.screen.step = 2;
        }
        break;
    case 6: // job_gate_select_update
        gate_select_step(fe, ui, a);
        break;
    case 7: // a refusal box over its page (the job page's walkers keep walking)
        if (a.box_return == 4) {
            job_wanderers_update(fe, ui, a);
            job_map_fighters_draw(fe, ui, a, -1);
        }
        area_select_draw(fe, ui, a);
        msg_box_update_ok(ui, a.boxes[a.box]);
        if (a.boxes[a.box].row.done && menu_accept(ui)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            ui_sound(ui, bank_sys, se_message_box);
            msg_box_open(a.boxes[a.box]);
            a.box = -1;
            fe.screen.step = a.box_return;
        }
        break;
    case 10: // job_characters_update
        job_characters_step(fe, ui, a);
        break;
    case 11: // job_contest_info_update
        job_contest_step(fe, ui, a);
        break;
    case 8: // area_select_leave
        area_select_draw(fe, ui, a);
        if (a.leave_page != 1) {
            job_map_fighters_draw(fe, ui, a, -1);
        }
        if (a.page == 1 && a.leave_page == 2) {
            gate_screen_draw(fe, ui, a);
        }
        if (--a.frames < 0 && fade_is_black(ui)) {
            fe.screen.strip.active = false;
            task_header_set(fe, a.result);
            fe.screen.step = 9;
        }
        break;
    default:
        area_select_draw(fe, ui, a);
        break;
    }
}

// decomp: job_menu_enter 0x14895, job_menu_update 0x1671E, job_gate_select_update 0x169E5
void job_menu_enter(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    a.character = 1;
    a.entry_ok = job_entry_allowed(fe, a);
    a.map_clock = static_cast<int>(ui.task_timer);
    a.mode = 0;
    a.page = 1;
    help_strip_init(ui, a.item_help[a.item], item_help_text(ui, a.item));
}

void job_menu_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    World& world = *fe.world;
    if (menu_cursor(ui, a.item, 0, 6, cursor_menu_vertical, 0, 3, a.item_rects)) {
        // retail re-inits it at y 0x49, a pixel under the setup's 0x48 (docs/bugs.md)
        help_strip_init(ui, a.item_help[a.item], item_help_text(ui, a.item));
    }
    job_wanderers_update(fe, ui, a);
    job_map_fighters_draw(fe, ui, a, -1);
    area_select_draw(fe, ui, a);
    music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
    if (menu_accept(ui)) {
        switch (a.item) {
        case 0: { // ENTRY
            const int rule = job_rule(fe, a);
            const int g = world.garages.selected;
            if (rule == rule_rental_only) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 3, 4);
                fe.screen.step = 7;
            } else if (g < 0 || g >= garage_count) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 1, 4);
                fe.screen.step = 7;
            } else if (!a.entry_ok) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                const int extra = rule >= 0 ? fe.story_tables.rules[static_cast<std::size_t>(rule)].extra : -1;
                box_open(a, extra != 0 ? 2 : 3, 4);
                fe.screen.step = 7;
            } else {
                ui_sound(ui, bank_sys, se_menu_accept);
                a.rented = false;
                a.gate = 0;
                fe.screen.step = 6;
            }
            break;
        }
        case 1: // RENTAL
            if (rental_allowed(fe, a)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                a.rented = true;
                a.gate = 0;
                fe.screen.step = 6;
            } else {
                ui_sound(ui, bank_sys, se_menu_cancel);
                box_open(a, 4, 4);
                fe.screen.step = 7;
            }
            break;
        case 2: // Change the Scoobee: the hangar
            ui_sound(ui, bank_sys, se_menu_accept);
            fe.campaign.hangar_return_area = a.area;
            a.result = 0;
            leave(ui, a, 0);
            fe.screen.step = 8;
            break;
        case 3: // CHARACTERS: refused only with nobody on an ordinary job's map
            if (tournament_kind(fe.story_tables, a.area, a.jobs[a.area]) == 0 &&
                job_map_fighter_count(fe, a) == 0) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                break;
            }
            ui_sound(ui, bank_sys, se_menu_accept);
            a.character = 1;
            pop_timer_start(ui, a.card, 1.0f, 1.0f, false, 0, 0);
            a.card_up = 1;
            fe.screen.step = 10;
            break;
        case 4: // CONTEST INFO
            ui_sound(ui, bank_sys, se_menu_accept);
            pop_timer_start(ui, a.card, 1.0f, 1.0f, false, 0, 0);
            a.card_up = 2;
            fe.screen.step = 11;
            break;
        default: // RETURN: the tabs slide back (no accept sound)
            ui_sound(ui, bank_sys, se_panel_slide_in);
            a.mode = 2;
            a.page_change = static_cast<int>(ui.task_timer);
            a.frames = 8;
            fe.screen.step = 5;
            break;
        }
    } else if (menu_cancel(ui) && a.item != 5) {
        a.item = 5;
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    }
}

void gate_select_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a) {
    World& world = *fe.world;
    const int count = area_gate_count(a.area);
    int sel = a.gate;
    menu_cursor(ui, sel, 0, count, cursor_menu_horizontal, 0, 4, a.gate_rects);
    const int kind = tournament_kind(fe.story_tables, a.area, a.jobs[a.area]);
    if (kind != 0 && count > 1) {
        // the rolled ranker gate is never a choice
        if (sel == a.ranker_gate[a.area]) {
            sel = (sel + 1) % count;
        }
    }
    a.gate = sel;
    job_wanderers_update(fe, ui, a);
    job_map_fighters_draw(fe, ui, a, -1);
    area_select_draw(fe, ui, a);
    gate_screen_draw(fe, ui, a);
    if (menu_accept(ui)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        BattleState& b = world.battle_state;
        const int slot = a.area;
        if (kind != 0) {
            b.ranker_roll = a.ranker_gate[a.area];
            rumble_fixed_opponent_queue(world, fe.story_tables, slot);
        }
        // the fee and the at-entry counts (tournament_enter), the gate, the player's entry
        tournament_enter(world.story, fe.story_tables, a.area);
        // rumble_roster_starts_set: each entrant enters where its walk on the map was heading
        job_roster_starts_set(fe, a);
        b.player_gate[0] = a.gate;
        RosterEntry entry;
        entry.pilot = pilot_row(fe.campaign.roster, 0);
        if (a.rented) {
            const int pick = rental_builds[rand_int(world.random, 4) % 4];
            fe.campaign.rental_build = pick;
            entry.build = roster_build_row(fe.campaign.roster, pick);
        } else {
            entry.build = garage_build(world.garages, world.garages.selected);
        }
        // rumble_roster_put: player 0, no pay, no ticket, queue 0
        entry.player = 0;
        entry.destruction_value = 0;
        entry.queue = 0;
        entry.ticket = RumbleTicket{};
        world.rumble[slot].roster[0] = entry;
        b.result = BattleResult{};
        b.result.rented = a.rented;
        a.result = 2;
        std::printf("area select: tournament %d area %d gate %d %s\n", a.jobs[a.area], a.area,
                    a.gate, a.rented ? "rental" : "entry");
        leave(ui, a, 2);
        fe.screen.step = 8;
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = 4;
    }
}

void area_select_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
