// The AppState's own states (decomp app_state_entry_table and the state_X_update
// functions): which screen each state runs, its step and end, and where its answers go. The
// menus and the later screens, VS and the day round answer through their own dispatch files
// (menu_dispatch.cpp, vs_setup.cpp, campaign_dispatch.cpp).
#include "screens/flow/front_end.hpp"

#include "audio/audio.hpp"
#include "cook/sexp_read.hpp"
#include "input/menu_input.hpp"
#include "save/save.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>

// the entry (state_X_enter): menu_request_state, menu_input_mark, the screen spawned
void front_end_enter_state(FrontEnd& fe, Ui& ui, int state) {
    fe.previous = fe.state;
    fe.state = state;
    fe.result = -1;
    std::printf("state 0x%x %s (update %d)\n", state,
                fe.states[static_cast<std::size_t>(state)].name.c_str(), fe.updates);
    menu_input_mark(ui);
    fe.screen = Screen{};
    fe.screen_alive = true;
    fe.screen_new = true;
    switch (state) {
    case app_phantagram_logo:
        movie_state_spawn(fe, ui, MovieScreenKind::phantagram_logo);
        break;
    case app_dolby_logo:
        movie_state_spawn(fe, ui, MovieScreenKind::dolby_logo);
        break;
    case app_attract_movie:
        movie_state_spawn(fe, ui, MovieScreenKind::attract);
        break;
    case app_opening_movie:
        movie_state_spawn(fe, ui, MovieScreenKind::opening);
        break;
    case app_logo:
        logo_spawn(fe, ui);
        break;
    case app_title_return:
        title_return_spawn(fe, ui);
        break;
    case app_title:
        title_spawn(fe, ui);
        break;
    case app_options:
        options_spawn(fe, ui);
        break;
    case app_name_entry:
        name_entry_spawn(fe, ui);
        break;
    case app_garage:
        garage_hub_spawn(fe, ui);
        break;
    case app_stock_module:
        stock_spawn(fe, ui);
        break;
    case app_wild_machines:
        wild_spawn(fe, ui);
        break;
    case app_plus_tech:
        plus_tech_spawn(fe, ui);
        break;
    case app_spec_scoobee:
        spec_spawn(fe, ui);
        break;
    case app_assemble:
        assemble_spawn(fe, ui);
        break;
    case app_pc_config:
        pc_settings_spawn(fe, ui);
        break;
    case app_new_game:
        // new_game_spawn: once the cabinet cache is idle (always, here) the inner AppState starts
        // in 9 by app_state_set_raw, without a transition to resolve
        fe.screen_alive = false;
        fe.nested = true;
        fe.entering = app_records_check;
        break;
    case app_attract_demo:
    case app_records_check:
    case app_save_files:
    case app_save_load:
    case app_sound_config:
        fe.screen.save.from = fe.previous;
        break;
    // the day round (screens/campaign/campaign.hpp)
    case app_town_map:
        town_map_spawn(fe, ui);
        break;
    case app_club_wired:
        club_wired_spawn(fe, ui);
        break;
    case app_day_advance:
        fe.screen.campaign.night = NightScreen{};
        break;
    case app_area_select:
        area_select_spawn(fe, ui);
        break;
    case app_battle: // the battle, campaign or VS (main's battle session)
        battle_screen_spawn(fe, ui);
        break;
    case app_result:
        result_screen_spawn(fe, ui);
        break;
    default:
        if (app_is_talk(state)) {
            talk_state_spawn(fe, ui);
        } else if (!vs_screens_enter(fe, ui, state) && !menu_screens_enter(fe, ui, state) &&
                   !campaign_screens_enter(fe, ui, state)) {
            placeholder_spawn(fe, ui);
        }
        break;
    }
}

void front_end_step_screen(FrontEnd& fe, Ui& ui) {
    switch (fe.state) {
    case app_phantagram_logo:
    case app_dolby_logo:
    case app_attract_movie:
    case app_opening_movie:
        movie_state_step(fe, ui);
        break;
    case app_logo:
        logo_step(fe, ui);
        break;
    case app_title_return:
        title_return_step(fe, ui);
        break;
    case app_title:
        title_step(fe, ui);
        break;
    case app_options:
        options_step(fe, ui);
        break;
    case app_name_entry:
        name_entry_step(fe, ui);
        break;
    case app_garage:
        garage_hub_step(fe, ui);
        break;
    case app_stock_module:
        stock_step(fe, ui);
        break;
    case app_wild_machines:
        wild_step(fe, ui);
        break;
    case app_plus_tech:
        plus_tech_step(fe, ui);
        break;
    case app_spec_scoobee:
        spec_step(fe, ui);
        break;
    case app_assemble:
        assemble_step(fe, ui);
        break;
    case app_pc_config:
        pc_settings_step(fe, ui);
        break;
    case app_sound_config:
        sound_config_step(fe, ui);
        break;
    case app_records_check:
        records_check_step(fe, ui);
        break;
    case app_save_files:
        save_files_step(fe, ui);
        break;
    case app_save_load:
        save_load_step(fe, ui);
        break;
    case app_attract_demo:
        attract_demo_step(fe, ui);
        break;
    case app_town_map:
        town_map_step(fe, ui);
        break;
    case app_club_wired:
        club_wired_step(fe, ui);
        break;
    case app_day_advance:
        night_step(fe, ui);
        break;
    case app_area_select:
        area_select_step(fe, ui);
        break;
    case app_battle: // the battle, campaign or VS (main's battle session)
        battle_screen_step(fe, ui);
        break;
    case app_result:
        result_screen_step(fe, ui);
        break;
    default:
        if (app_is_talk(fe.state)) {
            talk_state_step(fe, ui);
        } else if (!vs_screens_step(fe, ui) && !menu_screens_step(fe, ui) &&
                   !campaign_screens_step(fe, ui)) {
            placeholder_step(fe, ui);
        }
        break;
    }
}

// the screen's destroy (task_kill)
void front_end_destroy_screen(FrontEnd& fe, Ui& ui) {
    if (!fe.screen_alive) {
        return;
    }
    fe.screen_alive = false;
    // model_viewer_stop: the stand's loader done before any pack closes
    stand_settle(ui, fe.shop.stand);
    // texture_set_release: the screen's packs go (what it put on the GPU stays cached)
    ui_close_screen_packs(ui.data);
    switch (fe.state) {
    case app_phantagram_logo:
    case app_dolby_logo:
    case app_attract_movie:
    case app_opening_movie:
        movie_state_destroy(fe, ui);
        break;
    case app_logo:
        logo_destroy(fe, ui);
        break;
    case app_title_return:
        title_return_destroy(fe, ui);
        break;
    case app_attract_demo:
        attract_demo_destroy(fe, ui);
        break;
    case app_title:
        title_destroy(fe, ui);
        break;
    case app_options:
        options_destroy(fe, ui);
        break;
    case app_name_entry:
        name_entry_destroy(fe, ui);
        break;
    case app_garage:
        garage_hub_destroy(fe, ui);
        break;
    case app_stock_module:
        stock_destroy(fe, ui);
        break;
    case app_wild_machines:
        wild_destroy(fe, ui);
        break;
    case app_plus_tech:
        plus_tech_destroy(fe, ui);
        break;
    case app_spec_scoobee:
        spec_destroy(fe, ui);
        break;
    case app_assemble:
        assemble_destroy(fe, ui);
        break;
    case app_pc_config:
        pc_settings_destroy(fe, ui);
        break;
    case app_sound_config:
        sound_config_destroy(fe, ui);
        break;
    case app_save_load:
        save_load_destroy(fe, ui);
        break;
    case app_town_map:
        town_map_destroy(fe, ui);
        break;
    case app_club_wired:
        club_wired_destroy(fe, ui);
        break;
    case app_day_advance:
        night_destroy(fe, ui);
        break;
    case app_area_select:
        area_select_destroy(fe, ui);
        break;
    case app_battle: // the battle, campaign or VS (main's battle session)
        battle_screen_destroy(fe, ui);
        break;
    case app_result:
        result_screen_destroy(fe, ui);
        break;
    default:
        if (app_is_talk(fe.state)) {
            talk_state_destroy(fe, ui);
        } else if (!vs_screens_destroy(fe, ui) && !menu_screens_destroy(fe, ui)) {
            campaign_screens_destroy(fe, ui);
        }
        break;
    }
}

// the states' updates (state_X_update): the screen's answer to the next state
void front_end_update_state(FrontEnd& fe, Ui& ui) {
    const int r = fe.result;
    if (r < 0) {
        return;
    }
    auto go = [&](int state) { menu_set_state(fe, ui, state); };
    switch (fe.state) {
    case app_phantagram_logo: // phantagram logo
        go(app_logo);
        break;
    case app_logo: // logo
        go(r == 0 ? app_dolby_logo : app_attract_movie);
        break;
    case app_dolby_logo: // dolby logo
        go(app_attract_movie);
        break;
    case app_attract_movie: // attract movie
        go(app_title_return);
        break;
    case app_title_return: // title return
        go(r == 0 ? app_attract_demo : app_title);
        break;
    case app_attract_demo: // attract demo
        go(r == 0 ? app_phantagram_logo : app_title_return);
        break;
    case app_title: { // title
        const int next[5] = {app_new_game, app_save_load, app_vs_area_select, app_options,
                             app_title_return};
        if (r < 5) {
            go(next[r]);
        }
        break;
    }
    case app_records_check: // records check
        go(r == 0 ? app_save_files : app_title_return);
        break;
    case app_save_files: // save files
        go(r == 0 ? app_opening_movie : app_system);
        break;
    case app_opening_movie: // opening movie; 0 aims the name entry at the pilot
                            // (name_entry_target_pilot)
        if (r != 1) {
            name_target_pilot(fe);
        }
        go(r == 1 ? app_title_return : app_name_entry);
        break;
    case app_name_entry: { // name entry (state_name_entry_update)
        const int next[7] = {app_club_wired,    app_pepper_talk, app_assemble,      app_dealer,
                             app_wild_machines, app_keep_sv,     app_media_playlist};
        go(r < 7 ? next[r] : app_club_wired);
        break;
    }
    case app_assemble: // assemble: the hangar, the spec page, or name entry for the garage
        if (r == 2) {
            name_target_garage(fe);
        }
        go(r == 0 ? app_hangar : r == 1 ? app_spec_scoobee : app_name_entry);
        break;
    case app_spec_scoobee: { // the spec page, back where it was opened from
        const int next[5] = {app_hangar, app_assemble, app_dealer, app_wild_machines, app_keep_sv};
        go(r < 5 ? next[r] : app_dealer);
        break;
    }
    case app_plus_tech: // Plus Tech: the town map
        go(app_town_map);
        break;
    case app_wild_machines: // Wild Machines: the town map, the spec page, name entry
        go(r == 0 ? app_town_map : r == 1 ? app_spec_scoobee : app_name_entry);
        break;
    case app_stock_module: // stock: the hub
        go(app_garage);
        break;
    case app_garage: { // the garage hub
        const int next[8] = {app_hangar,   app_media_playlist, app_stock_module, app_record,
                             app_schedule, app_day_advance,    app_system,       app_town_map};
        if (r < 8) {
            go(next[r]);
        }
        break;
    }
    case app_options: { // options; 5 is the port's PC CONFIG
        const int next[6] = {app_sound_config, app_key_config, app_adjust,
                             app_title,        app_system,     app_pc_config};
        if (r < 6) {
            go(next[r]);
        }
        break;
    }
    case app_sound_config: // sound config
    case app_pc_config:
        go(app_options);
        break;
    case app_save_load: { // save load
        const int next[3] = {app_system, app_title, app_save_files};
        if (r < 3) {
            go(next[r]);
        }
        break;
    }
    case app_town_map: { // town map: garage, Club Wired, the shops, the dealers
        const int next[6] = {app_garage,    app_club_wired, app_wild_machines,
                             app_plus_tech, app_sonic_amp,  app_dealer};
        if (r < 6) {
            go(next[r]);
        }
        break;
    }
    case app_club_wired: { // Club Wired: area select, help, record, schedule, name entry, town map
        const int next[6] = {app_area_select, app_pepper_talk, app_record,
                             app_schedule,    app_name_entry,  app_town_map};
        if (r < 6) {
            go(next[r]);
        }
        break;
    }
    case app_area_select: // area select: the hangar, Club Wired, the battle
        go(r == 0 ? app_hangar : r == 1 ? app_club_wired : app_battle);
        break;
    case app_battle: { // the battle: the result, (VS) rematch, area page, machine select, title,
                       // ending
        const int next[6] = {app_result,         app_battle,       app_vs_area_select,
                             app_vs_area_select, app_title_return, app_ending_data};
        if (r < 6) {
            go(next[r]);
        }
        break;
    }
    case app_result: // result: the replay, the night
        go(r == 0 ? app_replay : app_day_advance);
        break;
    case app_day_advance: // the night: the garage
        go(app_garage);
        break;
    default: {
        // a conversation asks for 0x9F (the resolver then goes on to where it was going); the
        // help menu and the placeholders answer with the state itself
        int next = r;
        if (!vs_screens_next(fe, r, next) && !menu_screens_next(fe, r, next)) {
            campaign_screens_next(fe, r, next);
        }
        go(next);
        break;
    }
    }
}
