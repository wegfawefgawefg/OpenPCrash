#pragma once
// TerminalScene.state: the row of scene_table a fighter's scene runs (enter, update, exit)
enum SceneState {
    scene_play = 0x00,         // sv_motion_dispatch: no scene
    scene_intro_first = 0x01,  // scene_intro_*: 1..4 the four camera moves
    scene_intro_boss = 0x05,   // an unbeaten fixed opponent's entrance
    scene_door = 0x06,         // scene_door_*
    scene_player_death = 0x07, // scene_player_death_*
    scene_ending_first = 0x08, // scene_ending_*: the ending's shots, 8..0x12
    scene_ending_last = 0x12,
    scene_boss_defeat = 0x13,       // scene_boss_defeat_*: a boss down, before its enter
    scene_boss_defeat_area = 0x14,  // + area 0..2: an Area Ranker
    scene_boss_defeat_first = 0x17, // the First Ranker: the battle ends into the ending
    // free play's lessons (scene_tutorial_18 .. scene_tutorial_2e): lesson 0, the moves
    scene_lesson_hello = 0x18,
    scene_lesson_forward_back = 0x19,
    scene_lesson_turn_a = 0x1a,
    scene_lesson_turn_b = 0x1b,
    scene_lesson_run = 0x1c,
    scene_lesson_jump = 0x1d,
    scene_lesson_jump_high = 0x1e,
    scene_lesson_side_steps = 0x1f,
    scene_lesson_twist = 0x20,
    scene_lesson_recentre = 0x21,
    scene_lesson_moves_end = 0x22,
    // lesson 1, the weapons
    scene_lesson_weapons_hello = 0x23,
    scene_lesson_arm_a = 0x24, // then arm B, back A, back B, all four (0x24..0x28)
    scene_lesson_all_weapons = 0x28,
    scene_lesson_weapons_end = 0x29,
    // lesson 2, the camouflage
    scene_lesson_camo_hello = 0x2a,
    scene_lesson_camo_hide = 0x2b,
    scene_lesson_camo_end = 0x2c,
    // the Standard type's strafe, in place of the turns
    scene_lesson_strafe_a = 0x2d,
    scene_lesson_strafe_b = 0x2e,
    scene_lesson_first = scene_lesson_hello,
    scene_lesson_last = scene_lesson_strafe_b,
};

inline bool scene_is_lesson(int state) {
    return state >= scene_lesson_first && state <= scene_lesson_last;
}
