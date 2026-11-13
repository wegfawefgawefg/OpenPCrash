#pragma once
// The MUSIC panel (port-only, docs/settings.md "Music", docs/audio.md "Fight music"): PC CONFIG's
// pages with a column of their own (MUSIC, PLAYLIST, EXIT), opened over a fight's pause by its
// MUSIC item or in the menus from SOUND's MUSIC row.
//
// MUSIC: NOW PLAYING (left BACK, right or accept NEXT), PICK A SONG among the owned songs (accept
// plays it), MODE (PLAYLIST, ALL SONGS SHUFFLE, ONE SONG REPEAT; a PC setting) and EDIT PLAYLIST.
// PLAYLIST: the media player's playlists edited in place: which one the fights play, in order or
// at random, and its twelve songs, kept packed from the top as the media player's editor keeps
// them. In a fight its copy for the battle (music_playlist_compact) is made again at once.
//
// The song controls work in a fight that plays the list (a tournament's, VS); the four
// tournaments with a theme of their own, the test drive and free play keep theirs. In the menus a
// song picked plays as the media player's preview does, and the screen's music comes back on
// leaving.
//
// Nothing here writes the world during a fight: the playlists edited over the pause are a copy
// (FightMusic::lists) the fight plays from and the world takes when the fight ends, so the fight's
// record, its replay and its hashes don't change.
#include "screens/pc/pc_settings_screen.hpp"

struct FrontEnd;
struct Ui;
struct Music;

// the playlists the fights play and the panel edits: the world's, or the copy held over a fight
const Music& fight_lists(const FrontEnd& fe);
// every update of a fight that plays the list (battle_enter_next2 and 3, the replay's update)
void fight_music_update(FrontEnd& fe, Ui& ui);
// the fight's end: the playlists edited over its pause written to the world
void fight_lists_commit(FrontEnd& fe);
// the playlists to edit: the world's, or in a fight its copy (made on the first edit)
Music& fight_lists_edit(FrontEnd& fe);
// after an edit: the battle's copy made again (music_playlist_compact)
void fight_lists_changed(FrontEnd& fe);

// the panel opened over the pause (from_sound false) or from SOUND's MUSIC row
void music_panel_open(FrontEnd& fe, Ui& ui, bool from_sound);
// its EXIT when it came from SOUND: back to the MUSIC row, the screen's music again after a preview
void music_panel_leave(FrontEnd& fe, Ui& ui);
// once a frame before the rows are read or drawn: the song playing
void music_panel_frame(FrontEnd& fe, Ui& ui);

// MUSIC's and PLAYLIST's rows (music_panel_rows.cpp)
int music_rows_count(int page);
PcRowView music_row_view(FrontEnd& fe, int page, int row);
// left or right: true when something changed
bool music_row_step(FrontEnd& fe, Ui& ui, int page, int row, int step);
// accept: a song played, EDIT PLAYLIST opened; true when it did something
bool music_row_accept(FrontEnd& fe, Ui& ui, int page, int row);
std::string music_page_help(const FrontEnd& fe, int page);
// the song controls work now (a fight that plays the list), else why not
bool music_controls(const FrontEnd& fe, const char** why = nullptr);
