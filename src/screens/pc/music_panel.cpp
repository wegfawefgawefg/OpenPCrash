// The MUSIC panel (music_panel.hpp): the fights' music as the panel sets it, the playlists it
// edits, opening and leaving it.
#include "screens/pc/music_panel.hpp"

#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings_layout.hpp"
#include "screens/shops/fight_music.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <algorithm>

namespace {

// the tournament the fight belongs to (rumble_context_by_selector's)
int fight_tournament(const FrontEnd& fe) {
    const Story& story = fe.world->story;
    return story.lineup[rumble_slot_of_selector(story.rumble_selected)];
}

} // namespace

const Music& fight_lists(const FrontEnd& fe) {
    const FightMusic& f = fe.music.fight;
    return f.lists_held ? f.lists : fe.world->profile.music;
}

Music& fight_lists_edit(FrontEnd& fe) {
    FightMusic& f = fe.music.fight;
    if (fe.campaign.battle_running && !f.lists_held) {
        f.lists = fe.world->profile.music;
        f.lists_held = true;
    }
    return f.lists_held ? f.lists : fe.world->profile.music;
}

void fight_lists_changed(FrontEnd& fe) {
    // what the media screen's destroy does, at once
    music_playlist_compact(fe.music, fe.world->story, fight_lists(fe));
}

void fight_music_update(FrontEnd& fe, Ui& ui) {
    const int setting = fe.pc ? fe.pc->settings.fight_music : fight_music_default;
    const Story& story = fe.world->story;
    battle_music_update(fe.music, ui, story, fight_lists(fe), fight_tournament(fe),
                        fight_music_mode(story, setting));
}

void fight_lists_commit(FrontEnd& fe) {
    FightMusic& f = fe.music.fight;
    if (!f.lists_held) {
        return;
    }
    f.lists_held = false;
    Music& music = fe.world->profile.music;
    std::copy(&f.lists.playlists[0][0], &f.lists.playlists[0][0] + playlist_count * playlist_length,
              &music.playlists[0][0]);
    music.playlist_random = f.lists.playlist_random;
    music.playlist_selected = f.lists.playlist_selected;
}

bool music_controls(const FrontEnd& fe, const char** why) {
    const char* reason = nullptr;
    if (!fe.campaign.battle_running) {
        reason = "In a fight's pause left plays the last song again and right skips to the next.";
    } else if (fe.state != app_battle) {
        reason = "The test drive and free play keep their own track.";
    } else if (tournament_has_theme(fight_tournament(fe))) {
        reason = "This tournament plays its own theme.";
    }
    if (why) {
        *why = reason;
    }
    return reason == nullptr;
}

void music_panel_open(FrontEnd& fe, Ui& ui, bool from_sound) {
    PcSettingsScreen& s = fe.screen.pc;
    s.music = true;
    s.music_from_sound = from_sound;
    s.page = page_music;
    s.in_rows = true;
    s.row = s.top = 0;
    s.note.clear();
    s.music_pick = -1;
    music_panel_frame(fe, ui);
    menu_input_mark(ui);
}

void music_panel_leave(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    s.music = false;
    s.music_from_sound = false;
    s.page = page_sound;
    s.in_rows = true;
    s.row = pc_music_row();
    s.top = 0;
    if (s.previewed) {
        s.previewed = false;
        state_music_play(fe, ui, app_options); // PC CONFIG's music (the options')
    }
    menu_input_mark(ui);
}

void music_panel_frame(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    s.music_now = fight_music_now(fe.music, ui);
    if (s.music_pick < 0) {
        // PICK A SONG starts on the song playing, else the first owned
        int owned[all_song_ids];
        const int n = fight_music_owned(fe.music, fe.world->story, owned);
        s.music_pick = s.music_now >= 0 && s.music_now < all_song_ids ? s.music_now
                       : n > 0                                        ? owned[0]
                                                                      : -1;
    }
}
