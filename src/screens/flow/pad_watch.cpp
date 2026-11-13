// The lost controller (decomp docs/port/screens.md "Lost controller"): outside VS only player 0's
// pad is watched, the one that pressed START on the title return (title_return_screen_step3
// puts its port in GameState.input_mask); pulling any other pad does nothing. Over a menu
// state the reconnect box comes up (pause_pad_missing_update), in a fight the battle
// pause shows it (battle_screen.cpp). VS watches each seat's own device (pads_missing_now).
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "input/seats.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

// "Please reconnect the controller ♯" and the port as a
// full-width digit, line 4 "and press START to continue." with flag 1
// decomp: pad_missing_message_set 0xB18B1
void pad_missing_lines(FrontEnd& fe, Ui& ui, MsgBox& box, int flag) {
    if (!box.valid) {
        box = msg_box_from(ui, "pause", "pad_missing_boxes", 0);
        box.valid = true;
    }
    std::string line = text_string(ui, "str.25b358"); // pad_missing_text
    // the lowest missing seat as a full-width digit ("１".."４"): player 0 outside VS
    int port = 0;
    const unsigned missing = flag ? fe.campaign.pads_missing : 0;
    while (port < 3 && missing != 0 && !(missing & (1u << port))) {
        ++port;
    }
    line += "\xef\xbc";
    line += static_cast<char>(0x91 + port);
    line += "\xe3\x80\x80";
    for (auto& l : box.row.lines) {
        l.clear();
    }
    box.row.lines[2] = utf8_to_glyphs(ui.data, line);
    box.row.lines[3] = utf8_to_glyphs(ui.data, "\xe3\x80\x80");
    if (flag) {
        box.row.lines[4] = utf8_to_glyphs(ui.data, text_string(ui, "str.25b278"));
    }
}

bool pad_here(const Seats& seats, int pad) {
    return std::find(seats.pads_here.begin(), seats.pads_here.end(), pad) != seats.pads_here.end();
}

// the watched pad gone, or back: a pad plugged in after it went is taken as it (SDL gives a pad
// plugged in again a new id, where retail's port stays the same)
void watch_follow(PadWatch& w, const Seats& seats) {
    if (w.watched < 0) {
        w.lost = false;
        return;
    }
    if (pad_here(seats, w.watched)) {
        w.lost = false;
        return;
    }
    if (!w.lost) {
        w.lost = true;
        w.pads_left = seats.pads_here;
        return;
    }
    for (const int pad : seats.pads_here) {
        if (std::find(w.pads_left.begin(), w.pads_left.end(), pad) == w.pads_left.end()) {
            w.watched = pad;
            w.lost = false;
            return;
        }
    }
}

} // namespace

void pad_watch_title_start(FrontEnd& fe) {
    // the lowest of the pads that pressed START or accept this update; none for the keyboard
    PadWatch& w = fe.extra.pads;
    w.watched = -1;
    w.lost = false;
    if (!fe.seats) {
        return;
    }
    for (const SeatPress& press : fe.seats->presses) {
        if (press.accept && press.device.pad >= 0 &&
            (w.watched < 0 || press.device.pad < w.watched)) {
            w.watched = press.device.pad;
        }
    }
}

bool pad_watch_lost(const FrontEnd& fe) {
    return fe.extra.pads.lost;
}

void pad_watch_keyboard(FrontEnd& fe) {
    fe.extra.pads.watched = -1;
    fe.extra.pads.lost = false;
}

void pad_missing_box_draw(FrontEnd& fe, Ui& ui, int flag) {
    PadWatch& w = fe.extra.pads;
    // the battle pause's box (flag 1) is drawn only while a watched pad is gone (the battle
    // screen's call, Campaign::pads_missing)
    pad_missing_lines(fe, ui, w.box, flag);
    // re-armed whenever it wasn't up the update before: retail re-arms it only in pause_leave
    // and the menu box's release, so a second pull inside one battle pause showed it full size
    // at once and silent
    if (w.drawn != fe.updates - 1) {
        msg_box_open(w.box);
    }
    w.drawn = fe.updates;
    msg_box_update(ui, w.box);
}

bool pad_watch_frame(FrontEnd& fe, Ui& ui) {
    // port-only, for headless captures: PHANTOMCRASH_DEV_SONGS=N owns songs 0..N-1 (bought, not
    // yet seen by the media player)
    static bool seeded = false;
    if (!seeded) {
        seeded = true;
        if (const char* songs = std::getenv("PHANTOMCRASH_DEV_SONGS")) {
            const int n = std::atoi(songs);
            for (int i = 0; i < n && i < all_songs; ++i) {
                fe.world->story.music_owned[i] = 1;
            }
        }
    }
    PadWatch& w = fe.extra.pads;
    if (fe.seats && !fe.vs_mode) {
        watch_follow(w, *fe.seats);
    }
    const StateInfo& info =
        fe.states[static_cast<std::size_t>(fe.state < app_state_count ? fe.state : 0)];
    if (!w.up) {
        // app_pad_watch: only while no fade runs, in a state whose pad_pause is 1 (the battle
        // states' 2 is the pause's own watch).
        // PHANTOMCRASH_DEV_PAD_LOST=U (port-only, headless captures) pulls the pad at update U
        static const int lost_at = std::getenv("PHANTOMCRASH_DEV_PAD_LOST")
                                       ? std::atoi(std::getenv("PHANTOMCRASH_DEV_PAD_LOST"))
                                       : -1;
        w.forced = w.forced || fe.updates == lost_at;
        const bool pulled = w.lost || w.forced;
        if (pulled && !fe.vs_mode && info.pad_pause == 1 && fade_is_idle(ui)) {
            // pause_enter, kind 1: the music and effects pause, 0xD, the box (no dimming)
            w.up = true;
            if (ui.audio) {
                pause_music(*ui.audio, 0);
                pause_sounds(*ui.audio, 0);
            }
            ui_sound(ui, bank_sys, se_pause);
        }
        return false;
    }
    // pause_pad_missing_update: released once the pad is back. The port lets the keyboard take
    // over too (it is a controller here): START or accept on it closes the box, so the box never
    // traps a player who put the pad away
    const bool keyboard = menu_start_pressed(ui.pad) || menu_accept_pressed(ui.pad);
    if ((!w.lost && !w.forced) || keyboard || info.pad_pause != 1) {
        if (keyboard && w.lost) {
            pad_watch_keyboard(fe);
        }
        w.up = w.forced = false;
        if (ui.audio) {
            pause_music(*ui.audio, 1);
            pause_sounds(*ui.audio, 1);
        }
        ui_sound(ui, bank_sys, se_pause);
        ui.pad = PadRecord{};
        return true;
    }
    // pads_read reads no pad while the reason is set
    ui.pad = PadRecord{};
    ui.pointer.left = ui.pointer.right = false;
    ui.pointer.wheel = 0;
    return true;
}

void pad_watch_draw(FrontEnd& fe, Ui& ui) {
    if (fe.extra.pads.up) {
        pad_missing_box_draw(fe, ui, 0);
    }
}
