#pragma once
// The SE banks and their cues by name (the names the extractor gives the cue files,
// tools/extract/audio_names.hpp; decomp docs/port/audio.md). A call names the bank and the cue:
// play_sound(audio, bank_battle_player, se_jump). Cues no caller explains keep their number.

// The SE banks the game loads at boot (se_bank_load_resource from game_state_task)
enum SoundBank : int {
    bank_sys = 0,           // sys.bin, asset 0x783
    bank_battle = 1,        // battle.bin, 0x780
    bank_battle_old = 2,    // battle_old.bin, 0x781
    bank_battle_player = 3, // battle_player.bin, 0x782
};

// bank 0, sys.bin: menus and screens
// scene_intro_update, sprite 0xE80 at 90 frames left
inline constexpr int se_battle_intro_first = 0x00;
// scene_intro_update, sprite 0xE7F at 30 frames left
inline constexpr int se_battle_intro_second = 0x01;
inline constexpr int se_menu_cursor_vertical = 0x03; // pads_cursor_move, vertical menus
// pads_cursor_move, lists, grids, yes/no
inline constexpr int se_menu_cursor_horizontal = 0x04;
inline constexpr int se_menu_accept = 0x05;     // input_accept_pressed_sound
inline constexpr int se_menu_cancel = 0x08;     // input_cancel_pressed_sound and refused accepts
inline constexpr int se_page_switch = 0x09;     // name entry tabs, spec page flip
inline constexpr int se_message_box = 0x0C;     // message box zoom-in, job box dismiss
inline constexpr int se_pause = 0x0D;           // pause_enter, pause_leave
inline constexpr int se_press_start = 0x0E;     // title_return_screen_step3
inline constexpr int se_text_tick = 0x10;       // wide_text_sound(0), record rows
inline constexpr int se_text_tick_alt = 0x12;   // wide_text_sound(1), no caller passes 1
inline constexpr int se_event_page_next = 0x13; // se_play, event_cmd_page_wait
inline constexpr int se_event_text_done = 0x14; // event_text_sound
inline constexpr int se_name_entry_backspace = 0x15; // name_entry_screen_input
inline constexpr int se_carousel_turn = 0x17;        // shop and garage carousels
inline constexpr int se_town_map_cursor = 0x18;      // town_map_screen_step3
inline constexpr int se_shop_arrive = 0x19;          // hub screens entered from the town map
inline constexpr int se_shop_leave = 0x1A;           // accept on a hub screen's exit item
inline constexpr int se_purchase = 0x1B;             // buy, sell, unlock confirmed
inline constexpr int se_hangar_wheel_turn = 0x1C;    // hangar_select_update
inline constexpr int se_garage_part_fit = 0x1D;      // garage_manual_select
inline constexpr int se_garage_strip = 0x29;         // garage_strip_confirm_update
inline constexpr int se_hangar_message_close = 0x2A; // hangar_message_not_ready
inline constexpr int se_day_advance = 0x2B;          // day_advance_screen_update
inline constexpr int se_panel_slide_in = 0x2C;       // sprite_slide_draw frame 1, dealer intro
inline constexpr int se_ending_shot = 0x2F;          // scene_ending_enter, shot row 8
// bank 1, battle.bin: AI fighters, impacts, props, stage ambience
inline constexpr int se_roller_run_start_ai = 0x00; // legs_sound_roller
inline constexpr int se_roller_drive_loop_ai = 0x01;
inline constexpr int se_footstep_biped_ai = 0x02; // legs_sound_two (step_cues_ai)
inline constexpr int se_footstep_multileg_ai = 0x03;
inline constexpr int se_hover_start_ai = 0x04;
inline constexpr int se_hover_loop_ai = 0x05;
inline constexpr int se_jump_ai = 0x06;         // actor_try_jump
inline constexpr int se_dodge_ground_ai = 0x07; // fighter_leg_sound, dodge clips
inline constexpr int se_landing_biped = 0x08;   // leg_sound_table, every fighter
inline constexpr int se_landing_multileg = 0x09;
inline constexpr int se_landing_roller = 0x0A;
inline constexpr int se_cockpit_hum = 0x0C;      // actor_sounds_update, 2D, every 5 s
inline constexpr int se_heavy_cannon_hit = 0x0E; // battle_shots_update
inline constexpr int se_missile_explosion = 0x0F;
inline constexpr int se_explosion_large = 0x10;     // props, fighter destroyed
inline constexpr int se_surface_splash = 0x11;      // actor_contact_sound, contact bit 0x2000
inline constexpr int se_beam_hit = 0x12;            // shot_hit_fighters
inline constexpr int se_money_tick = 0x13;          // score up, town map money spin
inline constexpr int se_shinjuku_ambience_a = 0x14; // city_ambience_*
inline constexpr int se_shinjuku_ambience_b = 0x15;
inline constexpr int se_shinjuku_ambience_c = 0x17;
inline constexpr int se_shibuya_room_overhead_loop = 0x18; // shibuya_ambience_update
inline constexpr int se_shibuya_room_point_loop = 0x19;
inline constexpr int se_aqualine_ambience_a = 0x1A; // aqua_ambience_update
inline constexpr int se_aqualine_ambience_b = 0x1B;
inline constexpr int se_aqualine_ambience_c = 0x1C;
inline constexpr int se_aqualine_ambience_loop = 0x1D; // aqua_ambience_enter
inline constexpr int se_prop_fire_loop = 0x1E;         // burning props
inline constexpr int se_red_lotus_fire_ai = 0x1F;      // weapon_fire_sound_17
inline constexpr int se_grenade_launch_ai = 0x20;      // weapon_flash_model_3f3
inline constexpr int se_back_cannon_fire_ai = 0x21;    // weapon_flash_model_39e
inline constexpr int se_back_heavy_cannon_fire_ai = 0x23;
inline constexpr int se_gatling_fire_ai = 0x24;
inline constexpr int se_railgun_fire_ai = 0x25;
inline constexpr int se_melee_hit_ai = 0x26;   // impact_profile_dispatch profile 4
inline constexpr int se_pile_bunker_ai = 0x28; // weapon_melee_controller
inline constexpr int se_blade_swing_ai = 0x2A;
inline constexpr int se_punch_swing_ai = 0x2B;
inline constexpr int se_radio_bark = 0x2D;               // battle_barks_update, 2D
inline constexpr int se_bullet_hit = 0x2E;               // battle_shots_update
inline constexpr int se_supply_craft_engine_loop = 0x2F; // stage_route_choose
// bank 2, battle_old.bin: AI versions of three player cues
inline constexpr int se_gun_fire_ai = 0x00;
inline constexpr int se_laser_beam_ai = 0x01; // weapon_laser_controller, stopped by code
inline constexpr int se_missile_launch_ai = 0x02;
// bank 3, battle_player.bin: the human fighter
inline constexpr int se_roller_run_start = 0x00;
inline constexpr int se_roller_drive_loop = 0x01;
inline constexpr int se_footstep_biped = 0x02;
inline constexpr int se_footstep_multileg = 0x03;
inline constexpr int se_hover_start = 0x04;
inline constexpr int se_hover_loop = 0x05;
inline constexpr int se_jump = 0x06;
inline constexpr int se_dodge_ground = 0x07;
inline constexpr int se_landing_biped_player = 0x08;    // leg_sound_table rows never indexed
inline constexpr int se_landing_multileg_player = 0x09; // (unused)
inline constexpr int se_landing_roller_player = 0x0A;   // (unused)
inline constexpr int se_lock_complete = 0x0B;           // hud_locks_update, 2D
inline constexpr int se_camo_on = 0x0C;                 // actor_camo_sound
inline constexpr int se_camo_break = 0x0D;
inline constexpr int se_turn_start = 0x0E; // fighter_turn_sound
inline constexpr int se_turn_stop = 0x0F;
inline constexpr int se_camo_off = 0x10;
inline constexpr int se_landing_surface_a = 0x11; // actor_landing_sound, bits 0x1400
inline constexpr int se_landing_surface_b = 0x12; // bit 0x800
inline constexpr int se_dodge_air = 0x13;
inline constexpr int se_dive = 0x14;        // actor_air_control
inline constexpr int se_weapon_open = 0x15; // weapon_anim_update
inline constexpr int se_fall_start = 0x16;  // actor_phase_fall_update
inline constexpr int se_red_lotus_fire = 0x17;
inline constexpr int se_grenade_launch = 0x18;
inline constexpr int se_back_cannon_fire = 0x19;
inline constexpr int se_back_heavy_cannon_fire = 0x1B;
inline constexpr int se_gatling_fire = 0x1C;
inline constexpr int se_railgun_fire = 0x1D;
inline constexpr int se_melee_hit = 0x1E;
inline constexpr int se_drill_spin = 0x1F; // weapon_melee_controller, stopped by code
inline constexpr int se_pile_bunker = 0x21;
inline constexpr int se_blade_swing = 0x23;
inline constexpr int se_punch_swing = 0x24;
inline constexpr int se_weapon_broken_alarm = 0x25; // actor_sounds_update, every 5 s
inline constexpr int se_item_pickup = 0x26;         // pickup_apply
inline constexpr int se_low_armour_alarm = 0x27;
inline constexpr int se_lock_warning = 0x2A;
inline constexpr int se_gun_fire = 0x2C;       // weapon_flash_model_39e/39f
inline constexpr int se_shell_casing_a = 0x2D; // weapon_eject_casing
inline constexpr int se_shell_casing_b = 0x2E;
inline constexpr int se_laser_beam = 0x2F; // weapon_laser_controller, stopped by code
inline constexpr int se_missile_launch = 0x30;
inline constexpr int se_gate_motor = 0x31; // gate_touch, gate_opening_update
inline constexpr int se_gate_open = 0x32;
