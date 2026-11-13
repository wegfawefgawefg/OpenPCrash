#pragma once
// A conversation state's task (talk_task_spawn and the talk_NN_* updates, decomp
// docs/port/progression.md "Running a conversation"): apply the trigger row's policy (texture
// lists, background, cast, music), fade in, run the interpreter, draw the background each frame,
// fade out, and end. Bark, bio and dealer scripts run the interpreter alone (talk_bare).
#include "story/story_tables.hpp"
#include "talk/talk_interp.hpp"

// screen_fade_frame: 0 idle, 1 fading out, 2 black, 3 fading in
struct ScreenFade {
    int state = 2; // a conversation is entered from a faded-out state
    int timer = 0;
    int length = 1;
    std::uint32_t color = 0; // ARGB, its alpha replaced by the fade
};

enum class TalkPhase : std::uint8_t { Start, Open, Running, Close, Done };

struct TalkTask {
    TalkPhase phase = TalkPhase::Start;
    int counter = 0;    // the fade's frames (payload +0x10)
    int background = 6; // 0..5, 6 none
    int music = -1;     // the policy's track: -1 keep, 202 the shuffled catalogue
    int volume_percent = 100;
    bool bare = false; // a script with no conversation state: no policy, no fades
    std::uint32_t script_flags = 0;
    TalkInterp interp;
    ScreenFade fade;
    SlotBindings slots;
    int input_age = 0; // menu_input_age: frames since the last accept
};

// talk_task_spawn + talk_policy_apply: binds the texture slots and the cast into context
// (cast_slot) and starts the fade; row is the trigger row playing the state, or null for bare
void talk_task_begin(TalkTask& task, const TriggerRow* row, const SpriteRegistry& registry,
                     TalkContext& context, std::uint32_t script_flags, std::uint32_t seed);
// PC: skip the conversation (talk_skip.hpp): the script runs to its end unseen, then the task
// fades out as when it ends. Returns the frames the unseen run took.
int talk_task_skip(TalkTask& task, const TalkScript& script, TalkContext& context);
// one 60 Hz frame of the task and its interpreter: what to draw and which sounds to start; pads
// are masked while a fade runs (screen_fade_frame sets the read mask to 0)
void talk_task_frame(TalkTask& task, const TalkScript& script, TalkContext& context,
                     const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds);
