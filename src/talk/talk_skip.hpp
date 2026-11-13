#pragma once
// PC only: skipping conversations (docs/story.md, "Skipping"). The original has no skip, only the
// debug chord that races through a page a frame. A skip here runs the script to its end unseen:
// every command handled in order with the chord held and accept pressed, nothing drawn or heard,
// so it ends exactly as a full play does. No command touches the world (progression.md, "What a
// conversation can do"), and none asks the player anything: the only input a script reads is
// accept and the chord, which only end waits and finish lines.
#include "talk/talk_interp.hpp"
#include "world/pad_record.hpp"

// frames an unseen run may take before it gives up (a negative delay never ends: only bio and
// maker scripts have one, and those aren't skipped)
inline constexpr int talk_finish_frame_cap = 1 << 20;
// updates the skip button stays held for a skip to take the rest of the chain too
inline constexpr int talk_skip_hold_updates = 24;

// event_interp_step with the chord and accept every frame until the script ends; the frames
// taken, or -1 when it hadn't ended by the cap
int talk_interp_finish(TalkInterp& interp, const TalkScript& script, const TalkContext& context);
// a conversation run unseen from its start: talk_interp_start, then talk_interp_finish
int talk_script_finish(const TalkScript& script, const TalkContext& context,
                       std::uint32_t script_flags);

// the skip binds over one conversation: a new press of Back (Escape, Backspace, a pad's Back) or
// a right click skips it; held on past talk_skip_hold_updates, or pressed with the left stick
// click held (Shift on the keyboard), the skip takes the rest of the chain too
struct TalkSkip {
    bool skipped = false;    // this conversation was skipped
    bool group = false;      // and every conversation chained after it
    std::uint16_t hold = 0;  // the pad bit that pressed it (a right click holds the left trigger)
    int held = 0;            // updates it has stayed held since
    bool more = false;       // the chain has more after this one (the hint names "skip all")
};
// one update; true on the update the skip starts
bool talk_skip_update(TalkSkip& skip, const PadRecord& pad, bool right_click);

// the corner hint while a conversation is up, in the conversations' font, small and dim
void talk_skip_hint_draw(const TalkContext& context, bool more, Draw2D& draw);
