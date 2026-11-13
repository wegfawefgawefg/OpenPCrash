// The PC's fight music (fight_music.hpp): the modes' picks, NEXT, BACK and a picked song.
#include "screens/shops/fight_music.hpp"

#include "audio/audio.hpp"
#include "screens/ui/ui.hpp"

#include <algorithm>
#include <chrono>

namespace {

// rand_crt's step on FightMusic's own seed, seeded from the clock on its first draw so each
// session shuffles differently (the world never sees it)
float fight_rand(FightMusic& f, float range) {
    if (f.seed == 1) {
        f.seed = static_cast<std::uint32_t>(
                     std::chrono::steady_clock::now().time_since_epoch().count()) |
                 2u;
    }
    f.seed = f.seed * 0x343FDu + 0x269EC3u;
    return static_cast<float>((f.seed >> 16) & 0x7FFF) / 32768.0f * range;
}

int last_played(const FightMusic& f) {
    return f.history_count > 0 ? f.history[f.history_count - 1] : -1;
}

// a song started unlooped, as battle_playlist_play starts the list's; the gap follows it
void song_start(MusicPlayer& music, Ui& ui, int id) {
    const MusicRecord* record = music_record(music, id);
    if (!record || record->wave < 0) {
        return;
    }
    ui_music(ui, record->wave, false);
    fight_music_played(music, id);
}

// the deck: every owned song once in a shuffled order, the song just played not first
void deal(MusicPlayer& music, const Story& story) {
    FightMusic& f = music.fight;
    int owned[all_song_ids];
    const int n = fight_music_owned(music, story, owned);
    for (int i = n - 1; i > 0; --i) {
        const int k = static_cast<int>(fight_rand(f, static_cast<float>(i + 1)));
        std::swap(owned[i], owned[k]);
    }
    if (n > 1 && owned[0] == last_played(f)) {
        std::swap(owned[0], owned[n - 1]);
    }
    std::copy(owned, owned + n, f.deck);
    f.deck_size = n;
    f.deck_at = 0;
    f.dealt_owned = n;
}

int shuffle_next(MusicPlayer& music, const Story& story) {
    FightMusic& f = music.fight;
    int owned[all_song_ids];
    if (f.dealt_owned != fight_music_owned(music, story, owned) || f.deck_at >= f.deck_size) {
        deal(music, story);
    }
    return f.deck_at < f.deck_size ? f.deck[f.deck_at++] : -1;
}

// ONE SONG REPEAT's song: the one chosen, else the selected playlist's first owned, else the
// first owned song
int repeat_song(MusicPlayer& music, const Story& story, const Music& lists) {
    FightMusic& f = music.fight;
    if (music_record_owned(music, story, f.repeat_id) && f.repeat_id < all_song_ids) {
        return f.repeat_id;
    }
    const int selected = std::clamp(lists.playlist_selected, 0, playlist_count - 1);
    for (const int id : lists.playlists[selected]) {
        if (id >= 0 && id < all_song_ids && music_record_owned(music, story, id)) {
            return f.repeat_id = id;
        }
    }
    int owned[all_song_ids];
    return f.repeat_id = fight_music_owned(music, story, owned) > 0 ? owned[0] : -1;
}

// ONE SONG REPEAT's NEXT and BACK: the owned song after or before it in the catalogue
int repeat_step(MusicPlayer& music, const Story& story, const Music& lists, int step) {
    int owned[all_song_ids];
    const int n = fight_music_owned(music, story, owned);
    if (n == 0) {
        return -1;
    }
    const int now = repeat_song(music, story, lists);
    int at = 0;
    while (at < n && owned[at] != now) {
        ++at;
    }
    return owned[(at + step + n) % n];
}

} // namespace

int fight_music_mode(const Story& story, int setting) {
    if (setting > fight_music_default && setting < fight_music_modes) {
        return setting;
    }
    int owned = 0;
    for (int id = 0; id < all_song_ids; ++id) {
        owned += story.music_owned[id] != 0 ? 1 : 0;
    }
    return owned > playlist_length ? fight_music_shuffle : fight_music_playlist;
}

int fight_music_owned(const MusicPlayer& music, const Story& story, int out[all_song_ids]) {
    int n = 0;
    for (int id = 0; id < all_song_ids; ++id) {
        const MusicRecord* record = music_record_owned(music, story, id);
        if (record && record->wave >= 0) {
            out[n++] = id;
        }
    }
    return n;
}

void fight_music_played(MusicPlayer& music, int id) {
    FightMusic& f = music.fight;
    if (id < 0 || id >= all_song_ids || last_played(f) == id) {
        return;
    }
    if (f.history_count == fight_music_history) {
        std::copy(f.history + 1, f.history + fight_music_history, f.history);
        --f.history_count;
    }
    f.history[f.history_count++] = id;
}

void fight_music_play_next(MusicPlayer& music, Ui& ui, const Story& story, const Music& lists,
                           int mode) {
    const int id =
        mode == fight_music_repeat ? repeat_song(music, story, lists) : shuffle_next(music, story);
    song_start(music, ui, id);
}

int fight_music_now(const MusicPlayer& music, Ui& ui) {
    const MusicRecord* record = music_record_by_stream(music, ui_music_current(ui));
    return record ? record->id : -1;
}

void fight_music_skip(MusicPlayer& music, Ui& ui, const Story& story, const Music& lists, int mode,
                      int step) {
    FightMusic& f = music.fight;
    BattlePlaylist& list = music.battle;
    if (mode == fight_music_repeat) {
        // ONE SONG REPEAT: the next or previous owned song becomes the one repeated
        f.repeat_id = repeat_step(music, story, lists, step);
        song_start(music, ui, f.repeat_id);
    } else if (step < 0) {
        // BACK: the song before this one, or this one again from its start
        const int now = fight_music_now(music, ui);
        if (f.history_count >= 2 && last_played(f) == now) {
            --f.history_count;
        }
        const int id = last_played(f) >= 0 ? last_played(f) : now;
        const MusicRecord* record = music_record(music, id);
        if (record && record->wave >= 0) {
            ui_music(ui, record->wave, false);
            // the list goes on from that song's slot
            for (int i = 0; i < playlist_length; ++i) {
                if (list.wave[i] == record->wave) {
                    list.current = i;
                }
            }
        }
    } else if (mode == fight_music_shuffle) {
        song_start(music, ui, shuffle_next(music, story));
    } else {
        // PLAYLIST: battle_music_update's pick at once, drawn from the panel's generator
        for (int pass = 0; pass < 2; ++pass) {
            const int slot = music_playlist_slot(list, lists, fight_rand(f, 1.0f));
            if (slot != -1) {
                battle_playlist_play(music, ui, slot);
                break;
            }
            for (bool& played : list.played) {
                played = false;
            }
        }
    }
    // the gap after it, as after any song the list plays
    list.countdown = -1;
}

void fight_music_pick(MusicPlayer& music, Ui& ui, int id) {
    FightMusic& f = music.fight;
    f.repeat_id = id;
    // ALL SONGS SHUFFLE: a song picked counts as played this round
    for (int i = f.deck_at; i < f.deck_size; ++i) {
        if (f.deck[i] == id) {
            std::swap(f.deck[i], f.deck[f.deck_at++]);
            break;
        }
    }
    song_start(music, ui, id);
    music.battle.countdown = -1;
}

void fight_music_mode_changed(MusicPlayer& music, Ui& ui, int mode) {
    const int now = fight_music_now(music, ui);
    if (mode == fight_music_repeat && now >= 0 && now < all_song_ids) {
        music.fight.repeat_id = now;
    }
}
