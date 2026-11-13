// The campaign's remaining states in the AppState machine (decomp src/od/u_0a1e10.c
// state_hangar_update, state_schedule_update, state_test_drive_update,
// the replay's 5, state_keep_sv_update):
// which screen each runs and where its answers go.
#include "screens/campaign/area_select.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/campaign/keep_sv.hpp"
#include "screens/flow/front_end.hpp"

#include "audio/audio.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

namespace {

constexpr int test_drive_tournament = 0x2f; // rule 0, two opponents from reserve pool 269

// test_drive_spawn / test_drive_start: phase 0, rumble_selected 6, rumble[3] on
// tournament 47 with the hangar's garage as roster entry 0 (team 0), then the battle entry
// decomp: test_drive_spawn 0x970A0, test_drive_start 0x97112
void test_drive_spawn(FrontEnd& fe, Ui& ui) {
    World& world = *fe.world;
    world.story.rumble_selected = 6;
    const int slot = rumble_slot_of_selector(6);
    fill_opponent_queues(world, fe.story_tables, fe.campaign.roster, slot, test_drive_tournament);
    RosterEntry entry;
    entry.pilot = pilot_row(fe.campaign.roster, 0);
    entry.pilot.team = 0;
    const int garage = fe.shop.editing >= 0 && fe.shop.editing < garage_count
                           ? fe.shop.editing
                           : world.garages.selected;
    entry.build = garage_build(world.garages, garage);
    entry.player = 0;
    world.rumble[slot].roster[0] = entry;
    BattleState& b = world.battle_state;
    b.result = BattleResult{};
    fe.campaign.rental_build = -1;
    fe.campaign.battle_phase = 0;
    battle_screen_spawn(fe, ui);
}

// the battle music (5) once 5 s have gone
// and nothing plays; the door or a death sets phase 6, then the fade and answer 0
// decomp: test_drive_update 0x97260, test_drive_end 0x972E8
void test_drive_step(FrontEnd& fe, Ui& ui) {
    // (the battle entry's step 1 has already waited the 5 s)
    if (fe.campaign.battle_running && !fe.campaign.paused && fe.screen.step == 2) {
        if (ui_music_current(ui) < 0) {
            ui_music(ui, 5, false);
        }
        if (fe.world->battle_phase == 6) {
            battle_screen_quit(fe, ui);
            return;
        }
    }
    battle_screen_step(fe, ui);
}

} // namespace

bool campaign_screens_enter(FrontEnd& fe, Ui& ui, int state) {
    switch (state) {
    case app_hangar:
        hangar_spawn(fe, ui);
        return true;
    case app_schedule:
        schedule_spawn(fe, ui);
        return true;
    case app_test_drive:
        test_drive_spawn(fe, ui);
        return true;
    case app_replay:
        replay_screen_spawn(fe, ui);
        return true;
    case app_keep_sv:
        keep_sv_spawn(fe, ui);
        return true;
    default:
        return false;
    }
}

bool campaign_screens_step(FrontEnd& fe, Ui& ui) {
    switch (fe.state) {
    case app_hangar:
        hangar_step(fe, ui);
        return true;
    case app_schedule:
        schedule_step(fe, ui);
        return true;
    case app_test_drive:
        test_drive_step(fe, ui);
        return true;
    case app_replay:
        replay_screen_step(fe, ui);
        return true;
    case app_keep_sv:
        keep_sv_step(fe, ui);
        return true;
    default:
        return false;
    }
}

bool campaign_screens_destroy(FrontEnd& fe, Ui& ui) {
    switch (fe.state) {
    case app_hangar:
        hangar_destroy(fe, ui);
        return true;
    case app_schedule:
        schedule_destroy(fe, ui);
        return true;
    case app_test_drive:
        battle_screen_destroy(fe, ui); // the fight stops with the state
        return true;
    case app_replay:
        replay_screen_destroy(fe, ui);
        return true;
    case app_keep_sv:
        keep_sv_destroy(fe, ui);
        return true;
    default:
        return false;
    }
}

bool campaign_screens_next(const FrontEnd& fe, int r, int& next) {
    switch (fe.state) {
    case app_hangar: { // the hub, the area select, assemble, the spec page, keep SV, the test drive
        const int states[7] = {app_garage,  app_area_select, app_assemble, app_spec_scoobee,
                               app_keep_sv, app_test_drive,  app_keep_sv};
        next = r >= 0 && r < 7 ? states[r] : 0xd;
        return true;
    }
    case app_schedule: // the hub, or Club Wired
        next = r == 1 ? 0x8 : 0xd;
        return true;
    case app_test_drive: // back to the hangar
        next = app_hangar;
        return true;
    case app_replay: // state_replay_update: back to the result screen
        next = app_result;
        return true;
    case app_keep_sv: { // state_keep_sv_update: the system menu, the hangar, name entry, the spec
                        // page
        const int states[4] = {app_system, app_hangar, app_name_entry, app_spec_scoobee};
        next = r >= 0 && r < 4 ? states[r] : 0x27;
        return true;
    }
    default:
        return false;
    }
}
