#pragma once
// The media player's steps and the pieces media_player.cpp and media_edit.cpp share.
#include "screens/flow/extra_screens.hpp"

enum MediaStep : int {
    step_load = 0,
    step_fade_in,
    step_menu_enter,
    step_menu,
    step_list_enter,
    step_list,
    step_list_leave,
    step_edit_enter,
    step_edit_select,
    step_edit_leave,
    step_edit,
    step_preview,
    step_random,
    step_rename,
    step_exit,
    step_done,
};

// the blank help line's own task (text_strip_task_spawn with options_help)
void media_help_spawn(Ui& ui, MediaScreen& m);
// media_track_text_set
void media_track_text_set(FrontEnd& fe, Ui& ui, MediaScreen& m);
// the eight list rows of the list panel
void media_list_rects(Ui& ui, PointerRect out[8]);
// media_edit.cpp: media_edit_select_update, media_edit_update
void media_edit_select_update(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_edit_update(FrontEnd& fe, Ui& ui, MediaScreen& m);
