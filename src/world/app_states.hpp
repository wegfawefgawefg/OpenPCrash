#pragma once
// The application states (AppState.state, decomp app_state_entry_table; names as the extractor
// writes them in data/tables/states.sexp). The front end runs one at a time
// (src/screens/flow/state_dispatch.cpp); a battle remembers the one it was started from
// (BattleState::menu_state). The numbers are the original's: triggers, music and cadence key on
// them.

enum AppState {
    app_adjust = 0x00,
    app_attract_demo = 0x01,
    app_attract_movie = 0x02,
    app_area_select = 0x03,
    app_assemble = 0x04,
    app_replay = 0x05,
    app_battle = 0x06,
    app_unused_battle = 0x07,
    app_club_wired = 0x08,
    app_records_check = 0x09,
    app_ending_data = 0x0a,
    app_ending = 0x0b,
    app_secret_movies = 0x0c,
    app_garage = 0x0d,
    app_hangar = 0x0e,
    app_wild_machines = 0x0f,
    app_key_config = 0x10,
    app_save_load = 0x11,
    app_logo = 0x12,
    app_pepper_talk = 0x13,
    app_sonic_amp = 0x14,
    app_title = 0x15,
    app_name_entry = 0x16,
    app_net_lobby = 0x17,
    app_day_advance = 0x18,
    app_opening_movie = 0x19,
    app_options = 0x1a,
    app_plus_tech = 0x1b,
    app_new_game = 0x1c,
    app_record = 0x1d,
    app_result = 0x1e,
    app_save_menu = 0x1f,
    app_schedule = 0x20,
    app_media_playlist = 0x21,
    app_dealer = 0x22,
    app_sound_config = 0x23,
    app_spec_scoobee = 0x24,
    app_credits = 0x25,
    app_stock_module = 0x26,
    app_system = 0x27,
    app_title_return = 0x28,
    app_town_map = 0x29,
    app_free_play = 0x2a,
    app_keep_sv = 0x2b,
    app_vs_area_select = 0x2c,
    app_dolby_logo = 0x2d,
    app_save_files = 0x2e,
    app_test_drive = 0x2f,
    app_talk_first = 0x30, // 0x30..0x9D: one conversation each (talk_30..talk_9d)
    app_talk_end = 0x9e,
    app_phantagram_logo = 0x9e, // app_state_boot: the state the game starts in
    app_none = 0x9f,
    app_pc_config = 0xa0, // the port's own: PC CONFIG (screens/pc/pc_settings_screen.hpp)
    app_state_count = 0xa1,
};

inline bool app_is_talk(int state) {
    return state >= app_talk_first && state < app_talk_end;
}
