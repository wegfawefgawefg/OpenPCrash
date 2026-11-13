#include "input/bindings.hpp"

#include <algorithm>
#include <ginput/ginput.hpp>
#include <gubsy/input/sources.hpp>
#include <gubsy/input/types.hpp>
#include <gubsy/lobby/state.hpp>
#include <gubsy/profiles/user_profile.hpp>
#include <gubsy/settings/catalog.hpp>
#include <gubsy/settings/game_settings.hpp>
#include <gubsy/settings/schema.hpp>
#include <gubsy/settings/top_level_settings.hpp>
// gubsy's engine, for its profile settings pool (as devices.cpp reads its settings)
#include "src/engine_state.hpp"

namespace {

using B = GubsyButton;
using A = PadAction;

int id(PadAction action) {
    return static_cast<int>(action);
}

void bind(BindsProfile& profile, GubsyButton button, PadAction action) {
    (void)ginput::add_button_bind(profile, {static_cast<int>(button), id(action)});
}

void schema_actions(BindsSchema& schema) {
    // the pad's buttons, labelled by what they do in a fight with the original's default button
    // config (D_001DD490), and in menus
    const char* pad = "Pad buttons";
    schema.add_action(id(A::A), "A: jump, menu accept", pad);
    schema.add_action(id(A::B), "B: fire back weapon A, menu cancel", pad);
    schema.add_action(id(A::X), "X: camouflage", pad);
    schema.add_action(id(A::Y), "Y: fire back weapon B", pad);
    schema.add_action(id(A::Black), "Black: step right", pad);
    schema.add_action(id(A::White), "White: step left", pad);
    schema.add_action(id(A::LeftTrigger), "Left trigger: fire arm B", pad);
    schema.add_action(id(A::RightTrigger), "Right trigger: fire arm A", pad);
    schema.add_action(id(A::LeftThumb), "Left stick click: dash", pad);
    schema.add_action(id(A::RightThumb), "Right stick click: recentre view", pad);
    schema.add_action(id(A::Start), "Start: pause, menu accept", pad);
    schema.add_action(id(A::Back), "Back: menu cancel", pad);
    schema.add_action(id(A::DpadUp), "D-pad up: view nearer", pad);
    schema.add_action(id(A::DpadDown), "D-pad down: view farther", pad);
    schema.add_action(id(A::DpadLeft), "D-pad left: previous view", pad);
    schema.add_action(id(A::DpadRight), "D-pad right: next view", pad);
    const char* sticks = "Sticks on keys";
    schema.add_action(id(A::LeftStickUp), "Move forward", sticks);
    schema.add_action(id(A::LeftStickDown), "Move back", sticks);
    schema.add_action(id(A::LeftStickLeft), "Left stick left: turn or strafe", sticks);
    schema.add_action(id(A::LeftStickRight), "Left stick right: turn or strafe", sticks);
    schema.add_action(id(A::RightStickUp), "Right stick up", sticks);
    schema.add_action(id(A::RightStickDown), "Right stick down", sticks);
    schema.add_action(id(A::RightStickLeft), "Right stick left", sticks);
    schema.add_action(id(A::RightStickRight), "Right stick right", sticks);
    schema.add_action(id(A::Walk), "Walk (hold with the move keys)", sticks);
    schema.add_action(id(A::TextSkip), "Skip conversation text", "Other");
    schema.add_action(id(A::StandTurnLeft), "Turn the machine on a stand left", "Other");
    schema.add_action(id(A::StandTurnRight), "Turn the machine on a stand right", "Other");
    schema.add_axis_2d(axis_2d_left_stick, "Left stick: move", "Sticks");
    schema.add_axis_2d(axis_2d_right_stick, "Right stick: aim", "Sticks");
    schema.add_axis_2d(axis_2d_mouse_aim, "Mouse aim (upper body)", "Sticks");
    schema.add_axis_1d(axis_1d_left_trigger, "Left trigger", "Pad buttons");
    schema.add_axis_1d(axis_1d_right_trigger, "Right trigger", "Pad buttons");
    schema.add_axis_1d(axis_1d_wheel_dpad, "Wheel: view nearer and farther", "Other");
}

// An Xbox pad as the original's: every button where the Duke had it, the shoulders standing in
// for white and black, the default button config's left and right steps, so the left step is on
// the left
void pad_layout(BindsProfile& p) {
    bind(p, B::GP_A, A::A);
    bind(p, B::GP_B, A::B);
    bind(p, B::GP_X, A::X);
    bind(p, B::GP_Y, A::Y);
    bind(p, B::GP_LEFT_SHOULDER, A::White);
    bind(p, B::GP_RIGHT_SHOULDER, A::Black);
    bind(p, B::GP_START, A::Start);
    bind(p, B::GP_BACK, A::Back);
    bind(p, B::GP_LEFT_STICK_BUTTON, A::LeftThumb);
    bind(p, B::GP_RIGHT_STICK_BUTTON, A::RightThumb);
    bind(p, B::GP_DPAD_UP, A::DpadUp);
    bind(p, B::GP_DPAD_DOWN, A::DpadDown);
    bind(p, B::GP_DPAD_LEFT, A::DpadLeft);
    bind(p, B::GP_DPAD_RIGHT, A::DpadRight);
    (void)ginput::add_axis_2d_bind(
        p, {static_cast<int>(Gubsy2DAnalog::GP_LEFT_STICK), axis_2d_left_stick});
    (void)ginput::add_axis_2d_bind(
        p, {static_cast<int>(Gubsy2DAnalog::GP_RIGHT_STICK), axis_2d_right_stick});
    (void)ginput::add_axis_1d_bind(
        p, {static_cast<int>(Gubsy1DAnalog::GP_LEFT_TRIGGER), axis_1d_left_trigger});
    (void)ginput::add_axis_1d_bind(
        p, {static_cast<int>(Gubsy1DAnalog::GP_RIGHT_TRIGGER), axis_1d_right_trigger});
}

// the stand's turn keys, left and right under the left hand's first finger: free in every menu
// (WASD, Q, E, R, F, C, Z, X, Space and the number row are the pad's)
void stand_keys(BindsProfile& p) {
    bind(p, B::KB_V, A::StandTurnLeft);
    bind(p, B::KB_B, A::StandTurnRight);
}

// Keyboard and mouse: the left hand on WASD and everything a fight needs within its reach, the
// right hand on the mouse, which aims the upper body and fires. The arrows, Enter, Escape and
// the page keys are for menus only.
void keyboard_layout(BindsProfile& p) {
    // moving: WASD is the left stick (forward and back; turning under Preferred, strafing under
    // Standard), Ctrl walks, Shift dashes, Space jumps
    bind(p, B::KB_W, A::LeftStickUp);
    bind(p, B::KB_S, A::LeftStickDown);
    bind(p, B::KB_A, A::LeftStickLeft);
    bind(p, B::KB_D, A::LeftStickRight);
    bind(p, B::KB_LCTRL, A::Walk);
    bind(p, B::KB_LSHIFT, A::LeftThumb);
    bind(p, B::KB_SPACE, A::A);
    bind(p, B::KB_Q, A::White);
    bind(p, B::KB_E, A::Black);
    bind(p, B::KB_C, A::X);
    // weapons: arms on the main mouse buttons, backs on the side buttons and R / F
    bind(p, B::MOUSE_LEFT, A::RightTrigger);
    bind(p, B::MOUSE_RIGHT, A::LeftTrigger);
    bind(p, B::MOUSE_X2, A::B);
    bind(p, B::MOUSE_X1, A::Y);
    bind(p, B::KB_R, A::B);
    bind(p, B::KB_F, A::Y);
    // the view: middle button or Z recentres, the wheel or 3 / 4 nearer and farther, Tab or
    // 1 / 2 cycle the outside views
    bind(p, B::MOUSE_MIDDLE, A::RightThumb);
    bind(p, B::KB_Z, A::RightThumb);
    bind(p, B::KB_3, A::DpadUp);
    bind(p, B::KB_4, A::DpadDown);
    bind(p, B::KB_1, A::DpadLeft);
    bind(p, B::KB_2, A::DpadRight);
    bind(p, B::KB_TAB, A::DpadRight);
    (void)ginput::add_axis_1d_bind(
        p, {static_cast<int>(Gubsy1DAnalog::MOUSE_WHEEL), axis_1d_wheel_dpad});
    (void)ginput::add_axis_2d_bind(p,
                                   {static_cast<int>(Gubsy2DAnalog::MOUSE_XY), axis_2d_mouse_aim});
    // menus: Enter is Start and Space A, so both confirm (accept is A or Start); Escape and
    // Backspace are Back (in a fight Escape is Start instead, devices.cpp); the arrows are the
    // d-pad and WASD the left stick, which the menus' cursors read too; the page keys are the
    // triggers (page tabs)
    bind(p, B::KB_ENTER, A::Start);
    bind(p, B::KB_ESCAPE, A::Back);
    bind(p, B::KB_BACKSPACE, A::Back);
    bind(p, B::KB_UP, A::DpadUp);
    bind(p, B::KB_DOWN, A::DpadDown);
    bind(p, B::KB_LEFT, A::DpadLeft);
    bind(p, B::KB_RIGHT, A::DpadRight);
    bind(p, B::KB_PAGEUP, A::LeftTrigger);
    bind(p, B::KB_PAGEDOWN, A::RightTrigger);
    bind(p, B::KB_X, A::TextSkip);
    // the garage screens' stands: V and B turn the machine (the mouse drags it too)
    stand_keys(p);
}

// the steps' old defaults (Q and the left shoulder on black, E and the right shoulder on white,
// from when black was taken for the left step) moved to the sides they step to; other binds stay
void steps_to_sides(BindsProfile& p) {
    struct Move {
        GubsyButton button;
        PadAction from, to;
    };
    constexpr Move moves[] = {{B::KB_Q, A::Black, A::White},
                              {B::KB_E, A::White, A::Black},
                              {B::GP_LEFT_SHOULDER, A::Black, A::White},
                              {B::GP_RIGHT_SHOULDER, A::White, A::Black}};
    for (const Move& m : moves) {
        if (ginput::remove_button_bind(p, {static_cast<int>(m.button), id(m.from)})) {
            bind(p, m.button, m.to);
        }
    }
}

BindsProfile made(int profile_id, const char* name) {
    BindsProfile profile;
    profile.id = profile_id;
    profile.name = name;
    return profile;
}

BindsProfile everything(int profile_id, const char* name) {
    BindsProfile profile = made(profile_id, name);
    keyboard_layout(profile);
    pad_layout(profile);
    return profile;
}

void install_premades(GubsyRuntime& runtime) {
    BindsProfile pad = made(premade_pad, "Xbox pad");
    pad_layout(pad);
    BindsProfile keys = made(premade_keyboard, "Keyboard and mouse");
    keyboard_layout(keys);
    for (const BindsProfile& profile :
         {everything(premade_everything, "Keyboard, mouse and pad"), pad, keys}) {
        (void)gubsy_replace_binds_profile(runtime, profile);
    }
}

void register_settings(GubsyRuntime& runtime) {
    SettingsSchema schema;
    // the sticks' dead zones, percent of each axis's travel (devices.cpp)
    const auto zone = [&schema](const char* key, const char* label, const char* text, int lo,
                                int hi, int fallback) {
        SettingMetadata meta;
        meta.scope = SettingScope::Profile;
        meta.key = key;
        meta.label = label;
        meta.description = text;
        meta.categories = {"Controls"};
        meta.widget.kind = SettingWidgetKind::Slider;
        meta.widget.min = static_cast<float>(lo);
        meta.widget.max = static_cast<float>(hi);
        meta.widget.step = 5.0f;
        meta.widget.display_precision = 0;
        meta.default_value = fallback;
        schema.add_setting(meta);
    };
    zone(left_dead_zone_key, "Left stick dead zone",
         "How far the left stick moves before it counts, per axis. The original is 50%.",
         dead_zone_min, dead_zone_max, dead_zone_default);
    zone(right_dead_zone_key, "Right stick dead zone",
         "How far the right stick moves before it counts, per axis. The original is 50%.",
         dead_zone_min, dead_zone_max, dead_zone_default);
    zone(outer_dead_zone_key, "Outer dead zone",
         "The end of each stick's travel that already counts as full. The original has none.", 0,
         outer_dead_zone_max, 0);

    // PC: conversations and battle barks shown or skipped (docs/story.md, "Skipping")
    const auto show_skip = [&schema](const char* key, const char* label, const char* text) {
        SettingMetadata meta;
        meta.scope = SettingScope::Profile;
        meta.key = key;
        meta.label = label;
        meta.description = text;
        meta.categories = {"Gameplay"};
        meta.widget.kind = SettingWidgetKind::Option;
        meta.widget.options = {{"show", "Show"}, {"skip", "Skip"}};
        meta.default_value = std::string("show");
        schema.add_setting(meta);
    };
    show_skip(story_talk_key, "Story conversations",
              "Skip: conversations never appear. The story goes on exactly as if you read them.");
    show_skip(battle_chatter_key, "Battle chatter",
              "Skip: the pilots' lines during fights never appear.");
    register_settings_schema(gubsy_runtime_engine(runtime), schema);
}

// player 1's settings profile (as devices.cpp reads the input settings)
const GameSettings* player_settings(GubsyRuntime& runtime) {
    EngineState& engine = gubsy_runtime_engine(runtime);
    const UserProfile* user = gubsy_lobby_user_profile(engine, 0);
    if (!user) {
        return nullptr;
    }
    for (const GameSettings& settings : engine.game_settings_pool) {
        if (settings.id == user->last_game_settings_profile_id) {
            return &settings;
        }
    }
    return nullptr;
}

bool setting_skips(GubsyRuntime& runtime, const char* key) {
    const GameSettings* settings = player_settings(runtime);
    return settings && get_game_setting_string(*settings, key, "show") == "skip";
}

// player 1 plays on the keyboard and mouse as well as any pad
void attach_keyboard_and_mouse(GubsyRuntime& runtime) {
    const auto& players = gubsy_get_lobby_state(runtime).local_players;
    if (players.empty()) {
        return;
    }
    for (InputSourceType type : {InputSourceType::Keyboard, InputSourceType::Mouse}) {
        const auto& devices = players.front().devices;
        const bool attached = std::any_of(devices.begin(), devices.end(),
                                          [type](const auto& d) { return d.type == type; });
        if (!attached) {
            gubsy_toggle_lobby_player_device(runtime, 0, {type, 0});
        }
    }
}

} // namespace

bool story_talk_skipped(GubsyRuntime& runtime) {
    return setting_skips(runtime, story_talk_key);
}

bool battle_chatter_skipped(GubsyRuntime& runtime) {
    return setting_skips(runtime, battle_chatter_key);
}

void story_talk_settings_write(GubsyRuntime& runtime, bool skip) {
    // the catalog makes player 1's settings profile if it has none yet
    const SettingsCatalog catalog = build_settings_catalog(gubsy_runtime_engine(runtime), 0);
    if (!catalog.profile_settings) {
        return;
    }
    const char* value = skip ? "skip" : "show";
    set_game_setting_string(*catalog.profile_settings, story_talk_key, value);
    set_game_setting_string(*catalog.profile_settings, battle_chatter_key, value);
    (void)save_game_settings(*catalog.profile_settings);
}

void register_input(GubsyRuntime& runtime) {
    BindsSchema schema;
    schema_actions(schema);
    gubsy_register_binds_schema(runtime, schema);
    install_premades(runtime);
    if (!gubsy_find_binds_profile(runtime, player_profile_first)) {
        (void)gubsy_replace_binds_profile(runtime, everything(player_profile_first, "Player 1"));
    }
    // the players' own profiles from before the stand's keys: given them while neither is bound,
    // once (the install's stand_keys_key says it's done), so a player who clears both rows later
    // keeps them cleared; every profile made after it starts from one with the rows
    TopLevelGameSettings& top = gubsy_runtime_engine(runtime).top_level_game_settings;
    if (get_top_level_setting_int(top, stand_keys_key, 0) == 0) {
        for (int i = 0; i < 4; ++i) {
            const BindsProfile* own = gubsy_find_binds_profile(runtime, player_profile_first + i);
            if (own && ginput::button_binds_for_action(*own, id(A::StandTurnLeft)).empty() &&
                ginput::button_binds_for_action(*own, id(A::StandTurnRight)).empty()) {
                BindsProfile changed = *own;
                stand_keys(changed);
                (void)gubsy_replace_binds_profile(runtime, changed);
            }
        }
        set_top_level_setting_int(top, stand_keys_key, 1);
        (void)save_top_level_game_settings(top);
    }
    // the players' own profiles from before the steps were put right: Q and E, and the
    // shoulders, moved to the step their side names, once (steps_sided_key)
    if (get_top_level_setting_int(top, steps_sided_key, 0) == 0) {
        for (int i = 0; i < 4; ++i) {
            const BindsProfile* own = gubsy_find_binds_profile(runtime, player_profile_first + i);
            if (own) {
                BindsProfile changed = *own;
                steps_to_sides(changed);
                (void)gubsy_replace_binds_profile(runtime, changed);
            }
        }
        set_top_level_setting_int(top, steps_sided_key, 1);
        (void)save_top_level_game_settings(top);
    }
    register_settings(runtime);
    gubsy_lobby_ensure_ready(gubsy_runtime_engine(runtime));
    (void)gubsy_set_lobby_player_binds_profile(runtime, 0, player_profile_first);
    attach_keyboard_and_mouse(runtime);
}
