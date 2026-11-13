#pragma once
// Names for the SE cues, read from the decomp's callers (which function plays a (bank, cue) and
// what for; decomp docs/port/audio.md has the event tables). Banks: 0 sys.bin, 1 battle.bin,
// 2 battle_old.bin, 3 battle_player.bin. Bank 3 is the human fighter's set, banks 1 and 2 the AI's
// ("_ai"); bank 3 cues 23..30 pair with bank 1 cues 31..38. A cue with no row has no caller that
// tells what it is (or no caller at all); its file is just its index. `looped`: some caller
// starts it with SoundStart flag 1 (the bank itself stores no loop).
#include <cstddef>

struct SeCueName {
    int bank;
    int cue;
    const char* name;
    bool looped;
};

inline constexpr SeCueName se_cue_names[] = {
    // bank 0, sys.bin: menus and screens
    {0, 0, "battle_intro_first", false},     // scene_intro_update, sprite 0xE80 at 90 frames left
    {0, 1, "battle_intro_second", false},    // scene_intro_update, sprite 0xE7F at 30 frames left
    {0, 3, "menu_cursor_vertical", false},   // pads_cursor_move, vertical menus
    {0, 4, "menu_cursor_horizontal", false}, // pads_cursor_move, lists, grids, yes/no
    {0, 5, "menu_accept", false},            // input_accept_pressed_sound
    {0, 8, "menu_cancel", false},            // input_cancel_pressed_sound and refused accepts
    {0, 9, "page_switch", false},            // name entry tabs, spec page flip
    {0, 12, "message_box", false},           // message box zoom-in, job box dismiss
    {0, 13, "pause", false},                 // pause_enter, pause_leave
    {0, 14, "press_start", false},           // title_return_screen_step3
    {0, 16, "text_tick", false},             // wide_text_sound(0), record rows
    {0, 18, "text_tick_alt", false},         // wide_text_sound(1), no caller passes 1
    {0, 19, "event_page_next", false},       // se_play, event_cmd_page_wait
    {0, 20, "event_text_done", false},       // event_text_sound
    {0, 21, "name_entry_backspace", false},  // name_entry_screen_input
    {0, 23, "carousel_turn", false},         // shop and garage carousels
    {0, 24, "town_map_cursor", false},       // town_map_screen_step3
    {0, 25, "shop_arrive", false},           // hub screens entered from the town map
    {0, 26, "shop_leave", false},            // accept on a hub screen's exit item
    {0, 27, "purchase", false},              // buy, sell, unlock confirmed
    {0, 28, "hangar_wheel_turn", false},     // hangar_select_update
    {0, 29, "garage_part_fit", false},       // garage_manual_select
    {0, 41, "garage_strip", false},          // garage_strip_confirm_update
    {0, 42, "hangar_message_close", false},  // hangar_message_not_ready
    {0, 43, "day_advance", false},           // day_advance_screen_update
    {0, 44, "panel_slide_in", false},        // sprite_slide_draw frame 1, dealer intro
    {0, 47, "ending_shot", false},           // scene_ending_enter, shot row 8
    // bank 1, battle.bin: AI fighters, impacts, props, stage ambience
    {1, 0, "roller_run_start_ai", false}, // legs_sound_roller
    {1, 1, "roller_drive_loop_ai", true},
    {1, 2, "footstep_biped_ai", false}, // legs_sound_two (step_cues_ai)
    {1, 3, "footstep_multileg_ai", false},
    {1, 4, "hover_start_ai", false},
    {1, 5, "hover_loop_ai", true},
    {1, 6, "jump_ai", false},         // actor_try_jump
    {1, 7, "dodge_ground_ai", false}, // fighter_leg_sound, dodge clips
    {1, 8, "landing_biped", false},   // leg_sound_table, every fighter
    {1, 9, "landing_multileg", false},
    {1, 10, "landing_roller", false},
    {1, 12, "cockpit_hum", false},      // actor_sounds_update, 2D, every 5 s
    {1, 14, "heavy_cannon_hit", false}, // battle_shots_update
    {1, 15, "missile_explosion", false},
    {1, 16, "explosion_large", false},     // props, fighter destroyed
    {1, 17, "surface_splash", false},      // actor_contact_sound, contact bit 0x2000
    {1, 18, "beam_hit", false},            // shot_hit_fighters
    {1, 19, "money_tick", false},          // score up, town map money spin
    {1, 20, "shinjuku_ambience_a", false}, // city_ambience_*
    {1, 21, "shinjuku_ambience_b", false},
    {1, 23, "shinjuku_ambience_c", false},
    {1, 24, "shibuya_room_overhead_loop", true}, // shibuya_ambience_update
    {1, 25, "shibuya_room_point_loop", true},
    {1, 26, "aqualine_ambience_a", false}, // aqua_ambience_update
    {1, 27, "aqualine_ambience_b", false},
    {1, 28, "aqualine_ambience_c", false},
    {1, 29, "aqualine_ambience_loop", true}, // aqua_ambience_enter
    {1, 30, "prop_fire_loop", true},         // burning props
    {1, 31, "red_lotus_fire_ai", false},     // weapon_fire_sound_17
    {1, 32, "grenade_launch_ai", false},     // weapon_flash_model_3f3
    {1, 33, "back_cannon_fire_ai", false},   // weapon_flash_model_39e
    {1, 35, "back_heavy_cannon_fire_ai", false},
    {1, 36, "gatling_fire_ai", false},
    {1, 37, "railgun_fire_ai", false},
    {1, 38, "melee_hit_ai", false},   // impact_profile_dispatch profile 4
    {1, 40, "pile_bunker_ai", false}, // weapon_melee_controller
    {1, 42, "blade_swing_ai", false},
    {1, 43, "punch_swing_ai", false},
    {1, 45, "radio_bark", false},              // battle_barks_update, 2D
    {1, 46, "bullet_hit", false},              // battle_shots_update
    {1, 47, "supply_craft_engine_loop", true}, // stage_route_choose
    // bank 2, battle_old.bin: AI versions of 3/44, 3/47, 3/48
    {2, 0, "gun_fire_ai", false},
    {2, 1, "laser_beam_ai", false}, // weapon_laser_controller, stopped by code
    {2, 2, "missile_launch_ai", false},
    // bank 3, battle_player.bin: the human fighter
    {3, 0, "roller_run_start", false},
    {3, 1, "roller_drive_loop", true},
    {3, 2, "footstep_biped", false},
    {3, 3, "footstep_multileg", false},
    {3, 4, "hover_start", false},
    {3, 5, "hover_loop", true},
    {3, 6, "jump", false},
    {3, 7, "dodge_ground", false},
    {3, 8, "landing_biped_player", false},    // leg_sound_table rows never indexed
    {3, 9, "landing_multileg_player", false}, // (unused)
    {3, 10, "landing_roller_player", false},  // (unused)
    {3, 11, "lock_complete", false},          // hud_locks_update, 2D
    {3, 12, "camo_on", false},                // actor_camo_sound
    {3, 13, "camo_break", false},
    {3, 14, "turn_start", false}, // fighter_turn_sound
    {3, 15, "turn_stop", false},
    {3, 16, "camo_off", false},
    {3, 17, "landing_surface_a", false}, // actor_landing_sound, bits 0x1400
    {3, 18, "landing_surface_b", false}, // bit 0x800
    {3, 19, "dodge_air", false},
    {3, 20, "dive", false},        // actor_air_control
    {3, 21, "weapon_open", false}, // weapon_anim_update
    {3, 22, "fall_start", false},  // actor_phase_fall_update
    {3, 23, "red_lotus_fire", false},
    {3, 24, "grenade_launch", false},
    {3, 25, "back_cannon_fire", false},
    {3, 27, "back_heavy_cannon_fire", false},
    {3, 28, "gatling_fire", false},
    {3, 29, "railgun_fire", false},
    {3, 30, "melee_hit", false},
    {3, 31, "drill_spin", false}, // weapon_melee_controller, stopped by code
    {3, 33, "pile_bunker", false},
    {3, 35, "blade_swing", false},
    {3, 36, "punch_swing", false},
    {3, 37, "weapon_broken_alarm", false}, // actor_sounds_update, every 5 s
    {3, 38, "item_pickup", false},         // pickup_apply
    {3, 39, "low_armour_alarm", false},
    {3, 42, "lock_warning", false},
    {3, 44, "gun_fire", false},       // weapon_flash_model_39e/39f
    {3, 45, "shell_casing_a", false}, // weapon_eject_casing
    {3, 46, "shell_casing_b", false},
    {3, 47, "laser_beam", false}, // weapon_laser_controller, stopped by code
    {3, 48, "missile_launch", false},
    {3, 49, "gate_motor", false}, // gate_touch, gate_opening_update
    {3, 50, "gate_open", false},
};

inline const SeCueName* se_cue_name(int bank, int cue) {
    for (const SeCueName& row : se_cue_names) {
        if (row.bank == bank && row.cue == cue) {
            return &row;
        }
    }
    return nullptr;
}
