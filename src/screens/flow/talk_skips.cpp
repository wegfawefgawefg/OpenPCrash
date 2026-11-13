// PC only: conversations passed over unseen by a group skip or "Story conversations: Skip"
// (src/talk/talk_skip.hpp, docs/story.md "Skipping"), and two debug hooks for checking that a skip
// leaves the world exactly as a full play does.
#include "audio/audio.hpp"
#include "save/save.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

bool is_trigger_talk(const FrontEnd& fe, int state) {
    return state >= 0x30 && state < 0x9e && state_is_trigger_talk(fe.story_tables, state);
}

// a trigger conversation played unseen: its script run to its end as a full play runs it, minus
// the drawing and waiting. The resolver has set its done bit already. False when the script didn't
// end (no shipped conversation can stall: none has a negative wait or delay)
bool talk_run_unseen(FrontEnd& fe, Ui& ui, int state) {
    char name[16];
    std::snprintf(name, sizeof name, "talk_%02x", state);
    TalkScript script;
    std::string error;
    if (!load_talk_script(script, ui.data.font, ui.data.data_dir, name, error)) {
        std::fprintf(stderr, "talk: %s\n", error.c_str());
        return true; // a missing script ends at once when shown too (talk_state_spawn)
    }
    const int frames = talk_script_finish(script, fe.talk_context, 0);
    std::printf("state 0x%x %s skipped (%d frames unseen, update %d)\n", state,
                fe.states[static_cast<std::size_t>(state)].name.c_str(), frames, fe.updates);
    return frames >= 0;
}

} // namespace

int talk_chain_skip(FrontEnd& fe, Ui& ui, int state) {
    const StoryTables& tables = fe.story_tables;
    int music_row = trigger_row_count;
    bool skipped = false;
    while ((fe.talk_chain_skip || fe.story_talk_skipped) && is_trigger_talk(fe, state)) {
        if (!talk_run_unseen(fe, ui, state)) {
            break; // shown instead
        }
        const int row = trigger_row_of_state(tables, state);
        if (row < trigger_row_count && tables.triggers[static_cast<std::size_t>(row)].music >= 0) {
            music_row = row;
        }
        skipped = true;
        fe.state = state; // as if entered: the next resolve resumes the chain from it
        state = app_transition_resolve(fe.world->story, tables, state, app_none);
    }
    fe.talk_chain_skip = false;
    if (!skipped) {
        return state;
    }
    // the last policy's track, as talk_state_spawn starts it (202 needs the shuffle)
    if (music_row < trigger_row_count && ui.audio) {
        const TriggerRow& row = tables.triggers[static_cast<std::size_t>(music_row)];
        if (row.music < 202) {
            play_music(*ui.audio, row.music, true,
                       static_cast<float>(row.volume_percent) / 100.0f);
        }
    }
    // talk_state_destroy: the talk faded out to black, the next screen fades in from there
    ui.fade.state = 2;
    ui.fade.timer = 0;
    return state;
}

bool talk_chain_continues(const FrontEnd& fe) {
    // the resolve this talk's 0x9F will make, on a copy
    Story story = fe.world->story;
    return is_trigger_talk(fe, app_transition_resolve(story, fe.story_tables, fe.state, app_none));
}

void story_dump(const FrontEnd& fe) {
    const char* path = std::getenv("PHANTOMCRASH_STORY_DUMP");
    if (!path || !fe.world) {
        return;
    }
    std::FILE* file = std::fopen(path, "w");
    if (!file) {
        return;
    }
    const Story& story = fe.world->story;
    std::fprintf(file, "state 0x%x previous 0x%x\n", fe.state, fe.previous);
    std::fprintf(file, "talk_src 0x%x talk_dst 0x%x class_wins_at_entry %d ranker_wins_at_entry %d\n",
                 story.talk_src, story.talk_dst, story.class_wins_at_entry,
                 story.ranker_wins_at_entry);
    std::fprintf(file, "done rows");
    for (int row = 0; row < trigger_row_count; ++row) {
        if (event_done(story, row)) {
            std::fprintf(file, " %d", row);
        }
    }
    std::fprintf(file, "\n");
    SaveImage image;
    save_image_take(image, *fe.world, *fe.machine, 0);
    std::fputs(save_text(image).c_str(), file);
    std::fclose(file);
}

void talk_chain_test(FrontEnd& fe, Ui& ui) {
    const char* test = std::getenv("PHANTOMCRASH_TALK_CHAIN");
    unsigned src_bits = 0, dst_bits = 0;
    int won_first = 0, won_last = -1, done_first = 0, done_last = -1;
    if (!test || std::sscanf(test, "%x %x won %d-%d done %d-%d", &src_bits, &dst_bits,
                             &won_first, &won_last, &done_first, &done_last) != 6) {
        return;
    }
    const int src = static_cast<int>(src_bits), dst = static_cast<int>(dst_bits);
    Story& story = fe.world->story;
    story_new_game(story, fe.story_tables, 0);
    for (int t = won_first; t <= won_last && t < tournament_count; ++t) {
        story.tournament_won[t] = 1;
    }
    for (int row = done_first; row <= done_last && row < trigger_row_count; ++row) {
        story.events_done[row / 32] |= 1u << (row % 32);
    }
    fe.state = src;
    fe.entering = -1;
    ui.fade.state = 2;
    menu_set_state(fe, ui, dst);
}
