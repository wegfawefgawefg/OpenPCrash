// The MUSIC panel's rows (music_panel.hpp): MUSIC's song controls and mode, PLAYLIST's editor.
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"
#include "screens/shops/fight_music.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <string>

namespace {

// MUSIC's rows, then PLAYLIST's
constexpr int row_now = 0, row_pick = 1, row_mode = 2, row_edit = 3, music_rows = 4;
constexpr int row_list = 0, row_order = 1, row_first_song = 2,
              playlist_rows = row_first_song + playlist_length;

constexpr const char* mode_words[fight_music_modes] = {"PLAYLIST", "ALL SONGS SHUFFLE",
                                                       "ONE SONG REPEAT"};

// a song's title without the table's leading ideographic space
std::string song_title(const FrontEnd& fe, int id) {
    const MusicRecord* record = music_record(fe.music, id);
    if (!record) {
        return "";
    }
    std::string title = record->title;
    while (title.compare(0, 3, "\xE3\x80\x80") == 0) {
        title.erase(0, 3);
    }
    return title;
}

// "title by artist" for the help line
std::string song_line(const FrontEnd& fe, int id) {
    const MusicRecord* record = music_record(fe.music, id);
    if (!record) {
        return "";
    }
    std::string artist = record->artist;
    while (artist.compare(0, 3, "\xE3\x80\x80") == 0) {
        artist.erase(0, 3);
    }
    return song_title(fe, id) + " by " + artist + ".";
}

int mode_now(const FrontEnd& fe) {
    return fight_music_mode(fe.world->story, fe.pc ? fe.pc->settings.fight_music : -1);
}

// the songs a playlist holds from the top (the editor keeps them packed)
int list_length(const Music& lists) {
    const int* list = lists.playlists[std::clamp(lists.playlist_selected, 0, playlist_count - 1)];
    int n = 0;
    while (n < playlist_length && list[n] >= 0) {
        ++n;
    }
    return n;
}

// a song played: in a fight that plays the list at once (and ONE SONG REPEAT's), in the menus
// looped as the media player's preview
bool song_play(FrontEnd& fe, Ui& ui, int id) {
    const MusicRecord* record = music_record(fe.music, id);
    if (!record || record->wave < 0) {
        return false;
    }
    if (music_controls(fe)) {
        fight_music_pick(fe.music, ui, id);
        return true;
    }
    if (fe.campaign.battle_running) {
        return false; // a fight with its own track
    }
    ui_music(ui, record->wave, true);
    fe.screen.pc.previewed = true;
    return true;
}

PcRowView music_view(FrontEnd& fe, int row) {
    const PcSettingsScreen& s = fe.screen.pc;
    PcRowView view;
    view.wide = true;
    const char* why = nullptr;
    const bool controls = music_controls(fe, &why);
    int owned[all_song_ids];
    const int count = fight_music_owned(fe.music, fe.world->story, owned);
    switch (row) {
    case row_now:
        view.label = "NOW PLAYING";
        view.title = s.music_now >= 0 ? song_title(fe, s.music_now) : "";
        view.value = s.music_now >= 0 ? "" : "NOTHING";
        view.arrows = controls;
        view.dim = !controls;
        view.help = controls ? "Left goes BACK to the last song, right or A skips to the NEXT. "
                             : std::string(why) + " ";
        view.help += s.music_now >= 0 ? song_line(fe, s.music_now) : "";
        break;
    case row_pick:
        view.label = "PICK A SONG";
        view.title = song_title(fe, s.music_pick);
        view.value = s.music_pick >= 0 ? "" : "NO SONGS";
        view.arrows = count > 1;
        view.dim = fe.campaign.battle_running && !controls;
        view.help = "Left and right go through the " + std::to_string(count) +
                    " songs you own, A plays it. " + song_line(fe, s.music_pick);
        break;
    case row_mode:
        view.label = "MODE";
        view.value = mode_words[mode_now(fe)];
        view.help = mode_now(fe) == fight_music_playlist
                        ? "The fights play the playlist, as the original does."
                    : mode_now(fe) == fight_music_shuffle
                        ? "The fights play every song you own, shuffled, none twice until all "
                          "have played."
                        : "The fights play one song over and over: the one playing when this "
                          "was set, or the one picked.";
        if (fe.pc && fe.pc->settings.fight_music < 0) {
            view.help += " Until set, all songs once you own more than 12, else the playlist.";
        }
        break;
    default:
        view.label = "EDIT PLAYLIST";
        view.value = "OPEN";
        view.arrows = false;
        view.help = "Choose the playlist the fights play and its twelve songs.";
        break;
    }
    return view;
}

PcRowView playlist_view(FrontEnd& fe, int row) {
    const Music& lists = fight_lists(fe);
    const int selected = std::clamp(lists.playlist_selected, 0, playlist_count - 1);
    const int length = list_length(lists);
    PcRowView view;
    view.wide = true;
    if (row == row_list) {
        view.label = "PLAYLIST";
        view.glyphs = glyph_name_text(lists.playlist_names[selected], 8);
        bool named = false;
        for (const std::int16_t g : view.glyphs) {
            named = named || g > 0;
        }
        if (!named) {
            view.glyphs.clear();
            view.value = "LIST " + std::to_string(selected + 1);
        }
        view.help = "The playlist the fights play, one of the media player's eight. " +
                    std::to_string(length) + " songs.";
        return view;
    }
    if (row == row_order) {
        view.label = "ORDER";
        view.value = lists.playlist_random ? "RANDOM" : "IN ORDER";
        view.help = "IN ORDER plays the list from the top. RANDOM plays its songs in any order, "
                    "none twice until all have played.";
        return view;
    }
    const int slot = row - row_first_song;
    view.label = "SONG " + std::to_string(slot + 1);
    if (slot < length) {
        const int id = lists.playlists[selected][slot];
        view.title = song_title(fe, id);
        view.help =
            "Left and right change the song, EMPTY takes it off. A plays it. " + song_line(fe, id);
    } else if (slot == length) {
        view.value = "EMPTY";
        view.help = "Left and right add one of the songs you own.";
    } else {
        view.value = "";
        view.arrows = false;
        view.dim = true;
        view.help = "The songs go in from the top.";
    }
    return view;
}

// a playlist slot stepped through EMPTY and the owned songs; EMPTY takes the song off and the
// ones under it move up
bool slot_step(FrontEnd& fe, int slot, int step) {
    Music& lists = fight_lists_edit(fe);
    int* list = lists.playlists[std::clamp(lists.playlist_selected, 0, playlist_count - 1)];
    const int length = list_length(lists);
    if (slot > length || slot >= playlist_length) {
        return false;
    }
    int owned[all_song_ids];
    const int count = fight_music_owned(fe.music, fe.world->story, owned);
    // the choices: EMPTY (0), then the owned songs
    int at = 0;
    if (slot < length) {
        for (int i = 0; i < count; ++i) {
            if (owned[i] == list[slot]) {
                at = i + 1;
            }
        }
    }
    at = (at + step + count + 1) % (count + 1);
    if (at == 0) {
        if (slot == length) {
            return false;
        }
        std::copy(list + slot + 1, list + playlist_length, list + slot);
        list[playlist_length - 1] = -1;
    } else {
        list[slot] = owned[at - 1];
    }
    fight_lists_changed(fe);
    return true;
}

} // namespace

int music_rows_count(int page) {
    return page == page_music ? music_rows : playlist_rows;
}

PcRowView music_row_view(FrontEnd& fe, int page, int row) {
    return page == page_music ? music_view(fe, row) : playlist_view(fe, row);
}

bool music_row_step(FrontEnd& fe, Ui& ui, int page, int row, int step) {
    PcSettingsScreen& s = fe.screen.pc;
    if (page == page_playlist) {
        Music& lists = fight_lists_edit(fe);
        if (row == row_list) {
            lists.playlist_selected =
                (lists.playlist_selected + step + playlist_count) % playlist_count;
        } else if (row == row_order) {
            lists.playlist_random = !lists.playlist_random;
        } else {
            return slot_step(fe, row - row_first_song, step);
        }
        fight_lists_changed(fe);
        return true;
    }
    switch (row) {
    case row_now:
        if (!music_controls(fe)) {
            return false;
        }
        fight_music_skip(fe.music, ui, fe.world->story, fight_lists(fe), mode_now(fe), step);
        return true;
    case row_pick: {
        int owned[all_song_ids];
        const int count = fight_music_owned(fe.music, fe.world->story, owned);
        if (count < 2) {
            return false;
        }
        int at = 0;
        for (int i = 0; i < count; ++i) {
            if (owned[i] == s.music_pick) {
                at = i;
            }
        }
        s.music_pick = owned[(at + step + count) % count];
        return true;
    }
    case row_mode: {
        if (!fe.pc) {
            return false;
        }
        const int mode = (mode_now(fe) + step + fight_music_modes) % fight_music_modes;
        fe.pc->settings.fight_music = mode;
        pc_settings_save(*fe.pc);
        fight_music_mode_changed(fe.music, ui, mode);
        return true;
    }
    default:
        return false;
    }
}

bool music_row_accept(FrontEnd& fe, Ui& ui, int page, int row) {
    PcSettingsScreen& s = fe.screen.pc;
    if (page == page_playlist) {
        const Music& lists = fight_lists(fe);
        const int slot = row - row_first_song;
        if (slot < 0 || slot >= list_length(lists)) {
            return false;
        }
        return song_play(fe, ui, lists.playlists[lists.playlist_selected][slot]);
    }
    switch (row) {
    case row_now:
        if (!music_controls(fe)) {
            return false;
        }
        fight_music_skip(fe.music, ui, fe.world->story, fight_lists(fe), mode_now(fe), 1);
        return true;
    case row_pick:
        return song_play(fe, ui, s.music_pick);
    case row_edit:
        s.page = page_playlist;
        s.row = row_first_song;
        s.top = 0;
        return true;
    default:
        return false;
    }
}

std::string music_page_help(const FrontEnd& fe, int page) {
    if (page == page_playlist) {
        return "Edit the media player's playlists: the one the fights play, its order and songs.";
    }
    return fe.campaign.battle_running ? "The fight's music: the song playing, a song of your "
                                        "choice, and how the next one is chosen."
                                      : "The fights' music: how the songs are chosen, and the "
                                        "playlists.";
}
