#pragma once
// The PC's fight music (port-only; docs/audio.md "Fight music", docs/settings.md "Music"): beside
// retail's PLAYLIST, ALL SONGS SHUFFLE (every owned song in a shuffled deck, none again until all
// have played) and ONE SONG REPEAT, after the same 3 s gap battle_music_update keeps; and what the
// music panel (src/screens/pc/music_panel.cpp) does to the fight's music: NEXT, BACK, a song
// picked. Presentation only: the draws come from FightMusic's own CRT-style generator, never the
// world's, and leave retail's CRT draws as retail makes them.
#include "screens/shops/music_player.hpp"

// the mode a fight plays: the setting, or by default ALL SONGS SHUFFLE once more than a
// playlist's twelve songs are owned, else PLAYLIST
int fight_music_mode(const Story& story, int setting);
// the owned songs (ids 0..77) in catalogue order; their count
int fight_music_owned(const MusicPlayer& music, const Story& story, int out[all_song_ids]);
// a song a fight played, kept for BACK
void fight_music_played(MusicPlayer& music, int id);
// ALL SONGS SHUFFLE's or ONE SONG REPEAT's song at the gap's end (battle_music_update)
void fight_music_play_next(MusicPlayer& music, Ui& ui, const Story& story, const Music& lists,
                           int mode);
// the catalogue record playing (a song or one of the game's own themes), -1 none
int fight_music_now(const MusicPlayer& music, Ui& ui);
// NEXT (+1): the mode's next song at once; BACK (-1): the song before this one again (this one
// from its start when there is none). The gap follows the song as after any other.
void fight_music_skip(MusicPlayer& music, Ui& ui, const Story& story, const Music& lists, int mode,
                      int step);
// a song picked: played at once and ONE SONG REPEAT's from now on
void fight_music_pick(MusicPlayer& music, Ui& ui, int id);
// the mode changed: ONE SONG REPEAT keeps the song playing
void fight_music_mode_changed(MusicPlayer& music, Ui& ui, int mode);
