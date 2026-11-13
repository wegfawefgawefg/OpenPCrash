#pragma once
// The game's music besides the states' own tracks (decomp docs/port/screens.md "How playlists then
// play", "The nightly stock"; audio.md): the Sonic Amp catalogue (music_sonic_amp_catalogue),
// the battle's copy of the selected playlist (Playlist at GameState +0x6124) and its
// advance, the menus' shuffled pool, and Sonic Amp's stock. Presentation: everything here draws
// from CRT rand (rand_crt), never the world's generator, as retail does.
#include "world/game_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct Ui;
struct UiData;

// MusicRecord (0x1AC bytes), tables/music.sexp
struct MusicRecord {
    int id = 0;
    int wave = -1; // row of the music file table
    std::string title, artist;
    int jacket_sprite = -1; // +0x198: the art
    int chart_sprite = -1;  // +0x19C: the song's tile in the grids
    std::string bio;        // the artist's bio script (bio_<address>)
    int listen_sprite_a = -1; // +0x1A4, +0x1A8 (f_1a4, f_1a8): the listen view's artist pictures
    int listen_sprite_b = -1;
};
inline constexpr int music_record_count = 81; // ids 0..80; 78..80 are the game's own themes
inline constexpr int all_song_ids = 78;       // 0..77 are listed and sold
inline constexpr int music_shuffle_size = 16;

// the battle's copy (Playlist): the countdown, the slot playing, per slot played and the wave
struct BattlePlaylist {
    int countdown = 0;
    int current = -1;
    bool played[12] = {};
    int wave[12] = {};
};

// the menus' shuffled pool (music_shuffle_pool, slots music_shuffle_slots, played
// music_shuffle_played, the wait music_shuffle_wait)
struct MenuShuffle {
    bool dealt = false;
    int pool[music_shuffle_size] = {};
    bool played[music_shuffle_size] = {};
    int wait = 0;
};

// The PC's fight music (port-only, src/screens/shops/fight_music.cpp, docs/audio.md "Fight
// music"): the mode a fight plays (the PC setting, src/screens/pc/pc_settings.hpp), ALL SONGS
// SHUFFLE's deck, the songs played for BACK, ONE SONG REPEAT's song, and the playlists edited
// over a fight's pause, kept here until the fight ends (the world's copy is part of its hash)
enum FightMusicMode {
    fight_music_default = -1, // ALL SONGS SHUFFLE past 12 owned songs, else PLAYLIST
    fight_music_playlist,     // retail's: the selected media playlist
    fight_music_shuffle,      // every owned song, shuffled, none again until all have played
    fight_music_repeat,       // one song over and over
    fight_music_modes
};
inline constexpr int fight_music_history = 32;

struct FightMusic {
    std::uint32_t seed = 1; // its own CRT-style generator: retail's CRT draws stay retail's
    int deck[all_song_ids] = {};
    int deck_size = 0, deck_at = 0;
    int dealt_owned = -1; // the owned songs the deck was dealt from (a new one deals again)
    int history[fight_music_history] = {}; // catalogue ids the fights played, newest last
    int history_count = 0;
    int repeat_id = -1; // ONE SONG REPEAT's song
    bool lists_held = false;
    Music lists; // the playlists edited in a fight, written to the world when it ends
};

struct MusicPlayer {
    std::vector<MusicRecord> catalogue;
    BattlePlaylist battle;
    MenuShuffle shuffle;
    FightMusic fight;
    std::uint32_t crt = 1; // the CRT's seed: 1 until srand, which retail never calls
    int pumped_update = -1; // the front end update the pool last ran in
};

bool load_music_catalogue(MusicPlayer& music, UiData& data, std::string& error);
// rand_float_crt: rand() / 32768 * range
float rand_float_crt(MusicPlayer& music, float range);

// music_record (retail takes 81 on an 81-row table: docs/bugs.md); null outside
const MusicRecord* music_record(const MusicPlayer& music, int id);
// music_record_owned: the record when the song is owned
const MusicRecord* music_record_owned(const MusicPlayer& music, const Story& story, int id);
// music_record_by_stream: the record playing a wave
const MusicRecord* music_record_by_stream(const MusicPlayer& music, int wave);
// "《title　：artist　》　　　　　" (title_quote_open, _sep, _close)
std::string music_title_text(const MusicRecord& record);

// music_state_new's stock roll and the boot's battle_playlist_reset: what game_state_new leaves
// to the music player (every new game and the boot)
void music_state_new(MusicPlayer& music, Story& story, Music& player_music);
// music_shop_generate_stock: tonight's ten songs at Sonic Amp (the boot and each night)
void music_shop_generate_stock(MusicPlayer& music, Music& player_music);
// music_playlist_compact: the battle's copy of the selected playlist, owned songs only
void music_playlist_compact(MusicPlayer& music, const Story& story, const Music& player_music);
// battle_playlist_reset (once at boot)
void battle_playlist_reset(MusicPlayer& music, const Story& story, const Music& player_music);
// music_playlist_advance: the next slot, -1 none
int music_playlist_advance(MusicPlayer& music, const Music& player_music);
// its pick from a roll 0..1: the next slot in order, or an unplayed one at random
int music_playlist_slot(const BattlePlaylist& list, const Music& player_music, float roll);
// battle_playlist_play: the slot's song, unlooped, marked played
void battle_playlist_play(MusicPlayer& music, Ui& ui, int slot);
// tournament_theme_play: the four tournaments' themes; false for any other tournament
bool tournament_theme_play(Ui& ui, int tournament);
bool tournament_has_theme(int tournament);
// battle_music_update, every battle update: the 3 s gap, the tournament themes, the list; the
// PC's other modes (FightMusicMode) pick from the owned songs instead of the list
void battle_music_update(MusicPlayer& music, Ui& ui, const Story& story, const Music& player_music,
                         int tournament, int mode);

// music_shuffle_catalogue: sixteen songs dealt (Club Wired's entry deals again)
void music_shuffle_catalogue(MusicPlayer& music);
// music_pick_from_shuffled_pool at a volume: called by every screen running the pool,
// once an update whatever the number of callers (retail's help menu pumped it twice)
void music_shuffle_pump(MusicPlayer& music, Ui& ui, float volume, int update);
