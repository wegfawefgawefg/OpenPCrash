// The conversation task (decomp src/o1/u_0d0e28.c, u_0ca1ce.c; screen fades src/od/u_09f1d0.c).
#include "talk/talk_task.hpp"

#include "talk/talk_skip.hpp"

namespace {

// talk_policy_apply's fixed load list D_001D0D88: scr_common (1680) and talk_tex (1601)
constexpr int talk_texture_list[2] = {1680, 1601};
// the font, resident from boot (ascii_font_sys_tex, slot 518)
constexpr int font_asset = 1684;
// D_001D0DA0: the background sets by location, club-wired, garage, plus-tech, sonic-amp,
// town-map, wild-machines
constexpr int background_assets[6] = {1602, 1603, 1604, 1605, 1606, 1607};
// D_001D0DD0: per location a full-screen picture (layer 0) and its bottom band at 0, 296
// (layer 0x101), registry ids 22 + 2 * location and the one after
constexpr int background_first_sprite = 22;
constexpr float band_y = 296.0f;
// portraits load at 0x216 + portrait_slot_bias(cast slot)
constexpr int portrait_slot = 0x216;
// the fade quad's layer (pass 15) and its colour's full alpha (D_0017AAF0)
constexpr int fade_layer = 0x7FFF;

int half_cadence(const TalkContext& context) {
    return static_cast<int>(static_cast<float>(context.frame_cadence) * 0.5f);
}

// screen_fade_out_start / screen_fade_in_start: only from idle, and only from black
void fade_out_start(ScreenFade& fade, int frames, std::uint32_t color) {
    if (fade.state == 0) {
        fade.length = frames != 0 ? frames : 1;
        fade.timer = frames;
        fade.color = color;
        fade.state = 1;
    }
}

void fade_in_start(ScreenFade& fade, int frames) {
    if (fade.state == 2) {
        fade.length = frames != 0 ? frames : 1;
        fade.timer = frames;
        fade.state = 3;
    }
}

// screen_fade_frame: the quad for this frame, then the timer
void fade_frame(ScreenFade& fade, Draw2D& draw) {
    float alpha = 0.0f;
    bool drawn = true;
    switch (fade.state) {
    case 1:
        alpha = 1.0f - static_cast<float>(fade.timer) / static_cast<float>(fade.length);
        if (fade.timer <= 0) {
            fade.state = 2;
        }
        break;
    case 2:
        alpha = 1.0f;
        break;
    case 3:
        alpha = static_cast<float>(fade.timer) / static_cast<float>(fade.length);
        if (fade.timer <= 0) {
            fade.state = 0;
        }
        break;
    default:
        drawn = false;
        break;
    }
    if (drawn) {
        // over the whole window (quad_full_window): 672 x 496, where retail's 640 x 480 placed
        // from the safe area's corner stops at (608, 464)
        Quad2D quad;
        quad.w = 672.0f;
        quad.h = 496.0f;
        quad.flags = quad_full_window;
        alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
        quad.color = (fade.color & 0xFFFFFFu) |
                     static_cast<std::uint32_t>(static_cast<int>(255.0f * alpha)) << 24;
        quad.layer = fade_layer;
        submit_quad(draw, quad);
    }
    if (fade.timer > 0) {
        fade.timer -= 1; // video_rate_nominal / frame_cadence: 1 at 60
    }
}

// talk_task_draw_background / talk_background_draw. Not drawn yet: the garage
// banner (selected_garage_banner_draw at 399, layer 0x102) and the day and money (day_draw,
// money_draw at layer 0x109), which belong to the screens' shared widgets
// decomp: talk_task_draw_background 0xD1470, talk_background_draw 0xD0F02
void draw_background(const TalkTask& task, const TalkContext& context, Draw2D& draw) {
    if (task.background < 0 || task.background >= 6) {
        return;
    }
    const int picture = background_first_sprite + task.background * 2;
    submit_sprite(draw, *context.registry, picture + 1, 0.0f, band_y, 0x101, 0xFFFFFFFF, 1.0f);
    submit_sprite(draw, *context.registry, picture, 0.0f, 0.0f, 0, 0xFFFFFFFF, 1.0f);
}

void step_interp(TalkTask& task, const TalkScript& script, TalkContext& context,
                 const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds) {
    context.input_age = task.input_age;
    talk_interp_step(task.interp, script, context, pads, draw, sounds);
}

} // namespace

void talk_task_begin(TalkTask& task, const TriggerRow* row, const SpriteRegistry& registry,
                     TalkContext& context, std::uint32_t script_flags, std::uint32_t seed) {
    task = TalkTask{};
    task.script_flags = script_flags;
    task.interp.random = seed;
    bind_texture_set(task.slots, registry, font_asset);
    for (int& slot : context.cast_slot) {
        slot = -1;
    }
    if (!row) {
        // a bark, bio or dealer talk: its starter's screen is already up
        task.bare = true;
        task.fade.state = 0;
        for (const int asset : talk_texture_list) {
            bind_texture_set(task.slots, registry, asset);
        }
        talk_interp_start(task.interp, script_flags, seed);
        task.phase = TalkPhase::Running;
        return;
    }

    // the policy: load lists, the background, the cast into slots 534 + bias, the music
    for (const int asset : talk_texture_list) {
        bind_texture_set(task.slots, registry, asset);
    }
    task.background = row->background;
    if (task.background >= 0 && task.background < 6) {
        bind_texture_set(task.slots, registry, background_assets[task.background]);
    }
    for (int i = 0; i < 6; ++i) {
        const int actor = row->cast[i];
        if (actor < 0 || actor >= 28) {
            break;
        }
        context.cast_slot[actor] = i;
        bind_texture_set(task.slots, registry,
                         registry.actors[static_cast<std::size_t>(actor)].texture_asset,
                         portrait_slot + registry.bias[i]);
    }
    task.music = row->music;
    task.volume_percent = row->volume_percent;

    task.phase = TalkPhase::Start;
}

int talk_task_skip(TalkTask& task, const TalkScript& script, TalkContext& context) {
    if (task.phase == TalkPhase::Close || task.phase == TalkPhase::Done) {
        return 0;
    }
    // the script from wherever it is to its end, unseen (talk_skip.hpp)
    if (task.phase != TalkPhase::Running) {
        talk_interp_start(task.interp, task.script_flags, task.interp.random);
    }
    const int frames = task.interp.running ? talk_interp_finish(task.interp, script, context) : 0;
    task.interp.running = false;
    if (task.bare) {
        task.phase = TalkPhase::Done;
        return frames;
    }
    // the half-second fade out; one still fading in turns round from the same alpha, and a
    // screen still black stays black
    task.counter = half_cadence(context);
    if (task.fade.state == 3) {
        // black is timer / length fading in and 1 - timer / length fading out
        const int timer = task.counter * (task.fade.length - task.fade.timer) / task.fade.length;
        task.fade.state = 0;
        fade_out_start(task.fade, task.counter, 0xFF000000);
        task.fade.timer = timer;
    } else if (task.fade.state == 0) {
        fade_out_start(task.fade, task.counter, 0xFF000000);
    }
    task.phase = TalkPhase::Close;
    return frames;
}

void talk_task_frame(TalkTask& task, const TalkScript& script, TalkContext& context,
                     const TalkPads& pads_in, Draw2D& draw, std::vector<int>& sounds) {
    // a running fade masks the pads; the blink clock restarts on input (menu_input_mark)
    const TalkPads pads = task.fade.state != 0 ? TalkPads{} : pads_in;
    task.input_age = pads.accept ? 0 : task.input_age + 1;

    // the task's update, then its child the interpreter (parent first)
    switch (task.phase) {
    case TalkPhase::Start:
        // talk_NN_start, talk_task_fade_in: half a second from black, counted down before the
        // script starts (no background this frame)
        task.counter = half_cadence(context);
        fade_in_start(task.fade, task.counter);
        task.phase = TalkPhase::Open;
        break;
    case TalkPhase::Open:
        draw_background(task, context, draw);
        task.counter -= 1;
        if (task.counter < 0) {
            talk_interp_start(task.interp, task.script_flags, task.interp.random);
            task.phase = TalkPhase::Running;
            step_interp(task, script, context, pads, draw, sounds);
        }
        break;
    case TalkPhase::Running:
        if (!task.bare) {
            draw_background(task, context, draw);
        }
        if (!task.interp.running) {
            if (task.bare) {
                task.phase = TalkPhase::Done;
                break;
            }
            task.counter = half_cadence(context);
            fade_out_start(task.fade, task.counter, 0xFF000000);
            task.phase = TalkPhase::Close;
            break;
        }
        step_interp(task, script, context, pads, draw, sounds);
        break;
    case TalkPhase::Close:
        draw_background(task, context, draw);
        task.counter -= 1;
        if (task.counter < 0) {
            task.phase = TalkPhase::Done; // result 0: the state asks for 0x9F
        }
        break;
    case TalkPhase::Done:
        break;
    }
    fade_frame(task.fade, draw);
}
