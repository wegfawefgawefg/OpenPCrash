// The menus' entries in the AppState machine (decomp app_state_entry_table and the
// state_X_update functions): which screen each state runs and where its answers go. The system
// menu's screens, and the later ones: Sonic Amp state_sonic_amp_enter, the media player
// state_media_playlist_enter, the dealer state_dealer_enter, the ending's movie states, free play
// state_free_play_enter.
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"

#include "screens/system/save_slots.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>

bool menu_screens_enter(FrontEnd& fe, Ui& ui, int state) {
    switch (state) {
    case app_system:
        system_spawn(fe, ui);
        return true;
    case app_save_menu:
        save_menu_spawn(fe, ui);
        return true;
    case app_key_config:
        return true;
    case app_adjust:
        adjust_spawn(fe, ui);
        return true;
    case app_record:
        record_spawn(fe, ui);
        return true;
    case app_pepper_talk:
        help_spawn(fe, ui);
        return true;
    case app_sonic_amp:
        sonic_amp_spawn(fe, ui);
        return true;
    case app_media_playlist:
        media_spawn(fe, ui);
        return true;
    case app_dealer:
        dealer_spawn(fe, ui);
        return true;
    case app_ending_data:
        ending_scene_spawn(fe, ui);
        return true;
    case app_ending:
    case app_credits:
    case app_secret_movies:
        ending_spawn(fe, ui, state);
        return true;
    case app_free_play:
        free_play_spawn(fe, ui);
        return true;
    default:
        return false;
    }
}

bool menu_screens_step(FrontEnd& fe, Ui& ui) {
    switch (fe.state) {
    case app_system:
        system_step(fe, ui);
        return true;
    case app_save_menu:
        save_menu_step(fe, ui);
        return true;
    case app_key_config:
        key_config_step(fe, ui);
        return true;
    case app_adjust:
        adjust_step(fe, ui);
        return true;
    case app_record:
        record_step(fe, ui);
        return true;
    case app_pepper_talk:
        help_step(fe, ui);
        return true;
    case app_sonic_amp:
        sonic_amp_step(fe, ui);
        return true;
    case app_media_playlist:
        media_step(fe, ui);
        return true;
    case app_dealer:
        dealer_step(fe, ui);
        return true;
    case app_ending_data:
        ending_scene_step(fe, ui);
        return true;
    case app_ending:
    case app_credits:
    case app_secret_movies:
        ending_step(fe, ui);
        return true;
    case app_free_play:
        free_play_step(fe, ui);
        return true;
    default:
        return false;
    }
}

bool menu_screens_destroy(FrontEnd& fe, Ui& ui) {
    switch (fe.state) {
    case app_system:
        system_destroy(fe, ui);
        return true;
    case app_save_menu:
        save_menu_destroy(fe, ui);
        return true;
    case app_key_config:
        key_config_destroy(fe, ui);
        return true;
    case app_adjust:
        adjust_destroy(fe, ui);
        return true;
    case app_record:
        record_destroy(fe, ui);
        return true;
    case app_pepper_talk:
        help_destroy(fe, ui);
        return true;
    case app_sonic_amp:
        sonic_amp_destroy(fe, ui);
        return true;
    case app_media_playlist:
        media_destroy(fe, ui);
        return true;
    case app_dealer:
        dealer_destroy(fe, ui);
        return true;
    case app_ending_data:
        ending_scene_destroy(fe, ui);
        return true;
    case app_ending:
    case app_credits:
    case app_secret_movies:
        ending_destroy(fe, ui);
        return true;
    case app_free_play:
        battle_screen_destroy(fe, ui); // the fight stops with the state
        return true;
    default:
        return false;
    }
}

bool menu_screens_next(const FrontEnd& fe, int r, int& next) {
    switch (fe.state) {
    case app_system: { // state_system_update: save, load, keep SV, options, the garage
        const int states[5] = {app_save_menu, app_save_load, app_keep_sv, app_options, app_garage};
        if (r >= 0 && r < 5) {
            next = states[r];
            return true;
        }
        return false;
    }
    case app_save_menu: // the save menu's only answer
        next = app_system;
        return true;
    case app_key_config: // key config and screen adjust go back to the options
    case app_adjust:
        next = app_options;
        return true;
    case 0x69: // the help topics (state_talk_69_update..): back to the help menu
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
    case 0x7c:
    case 0x7d:
    case 0x7e:
    case 0x7f:
    case 0x80:
    case 0x9d:
        if (r == 0x9f && fe.menu.help_came_from != 0) {
            next = app_pepper_talk;
            return true;
        }
        return false;
    case app_record: // state_record_update: the garage, or Club Wired
        next = r == 1 ? 8 : 0xd;
        return true;
    case app_sonic_amp: // state_sonic_amp_update: the town map
        next = app_town_map;
        return true;
    case app_media_playlist: // state_media_playlist_update: the hub, or name entry for a playlist
        next = r == 1 ? 0x16 : 0xd;
        return true;
    case app_dealer: { // state_dealer_update: the town map, the spec page, name entry for the Chip
        const int states[3] = {app_town_map, app_spec_scoobee, app_name_entry};
        next = r >= 0 && r < 3 ? states[r] : 0x29;
        return true;
    }
    case app_ending_data: // state_ending_data_update: the ending movie
        next = app_ending;
        return true;
    case app_ending: // the ending movie: the credits
        next = app_credits;
        return true;
    case app_credits: // the credits: the night, or the secret movies after 30 s
        next = r == 0 ? 0x18 : 0xc;
        return true;
    case app_secret_movies: // the secret movies: the night
        next = app_day_advance;
        return true;
    case app_free_play: // free play's only answer: the help menu
        next = app_pepper_talk;
        return true;
    default:
        return false;
    }
}

void menu_screens_frame(FrontEnd& fe, Ui& ui) {
    // port-only, for headless runs: PHANTOMCRASH_LOAD_SLOT=N loads that slot before the first
    // state (--state then opens a screen on a played game)
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (const char* slot = std::getenv("PHANTOMCRASH_LOAD_SLOT")) {
            SaveImage image;
            std::string error;
            const int n = std::atoi(slot);
            if (save_slot_read(fe.save_dir, n, image, error) == SaveRead::Ok) {
                save_commit_load(fe, ui, n, image);
                std::printf("loaded slot %d\n", n);
            } else {
                std::fprintf(stderr, "load slot %d: %s\n", n, error.c_str());
            }
        }
    }
    ui.screen_shift[0] = game_options(fe).screen_shift_x;
    ui.screen_shift[1] = game_options(fe).screen_shift_y;
}
