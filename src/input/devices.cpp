#include "input/devices.hpp"

#include "input/bindings.hpp"
#include "input/pad_calibration.hpp"
#include "input/pad_read.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cmath>
#include <ginput/ginput.hpp>
#include <gubsy/input/profile_settings.hpp>
#include <gubsy/input/types.hpp>
#include <gubsy/lobby/state.hpp>
#include <gubsy/profiles/user_profile.hpp>
#include <gubsy/settings/game_settings.hpp>
// gubsy's engine, for its profile settings pool (as Mans In My House reads its settings)
#include "src/engine_state.hpp"

namespace {

using K = ginput::DeviceKind;

bool player_has(const GubsyLobbyPlayer& player, K kind, int device) {
    for (const GubsyLobbyDeviceAssignment& d : player.devices) {
        if (kind == K::Keyboard && d.type == InputSourceType::Keyboard) {
            return true;
        }
        if (kind == K::Mouse && d.type == InputSourceType::Mouse) {
            return true;
        }
        if (kind == K::Gamepad && d.type == InputSourceType::Gamepad && d.device_id == device) {
            return true;
        }
    }
    return false;
}

// a press latched since the last read on one of the player's devices, bound to the action
bool tapped(const InputDevices& devices, const BindsProfile& profile,
            const GubsyLobbyPlayer& player, int action) {
    for (const ginput::ButtonBind& bind : ginput::button_binds_for_action(profile, action)) {
        DeviceCode code;
        if (!device_code_of(bind.device_button, code)) {
            continue;
        }
        for (const DeviceCode& tap : devices.taps) {
            if (tap.kind == code.kind && tap.code == code.code &&
                (code.device == ginput::any_device_id || code.device == tap.device) &&
                player_has(player, tap.kind, tap.device)) {
                return true;
            }
        }
    }
    return false;
}

bool down(GubsyRuntime& runtime, const InputDevices& devices, const BindsProfile& profile,
          const GubsyLobbyPlayer& player, int index, PadAction action) {
    const int id = static_cast<int>(action);
    return gubsy_lobby_player_action_down(runtime, index, id) ||
           tapped(devices, profile, player, id);
}

std::vector<SDL_Gamepad*> player_pads(const GubsyLobbyPlayer& player) {
    std::vector<SDL_Gamepad*> pads;
    for (const GubsyLobbyDeviceAssignment& d : player.devices) {
        if (d.type == InputSourceType::Gamepad) {
            if (SDL_Gamepad* pad = SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(d.device_id))) {
                pads.push_back(pad);
            }
        }
    }
    return pads;
}

bool profile_binds_2d(const BindsProfile& profile, int axis, Gubsy2DAnalog device) {
    for (const ginput::Axis2DBind& bind : ginput::binds_for_axis_2d(profile, axis)) {
        if (bind.device_stick == static_cast<int>(device)) {
            return true;
        }
    }
    return false;
}

// the player's dead zones in raw units: each stick's inner one, the outer one (docs/input.md)
struct StickZones {
    int inner[2] = {original_stick_dead_zone, original_stick_dead_zone};
    int outer = 0;
};

// a pad's raw axis, through its calibration when one is stored for its kind (pad_calibration.hpp)
int raw_axis(SDL_Gamepad* pad, const PadCalibration* calibration, SDL_GamepadAxis axis, int stick,
             int index) {
    const int raw = SDL_GetGamepadAxis(pad, axis);
    return calibration ? calibrated_axis(calibration->stick[stick][index], raw) : raw;
}

// a pad stick for one of the two sticks: the first of the player's pads whose bound stick is off
// centre after the dead zone (pads_stick_read), XInput's sense (y up). The zone is the
// pad stick's own (LEFT DEAD ZONE for the left stick whichever axis it drives)
bool pad_stick(const std::vector<SDL_Gamepad*>& pads, const PadCalibrations& calibrations,
               const BindsProfile& profile, int axis, const StickZones& zones,
               std::int16_t out[2]) {
    for (SDL_Gamepad* pad : pads) {
        for (const ginput::Axis2DBind& bind : ginput::binds_for_axis_2d(profile, axis)) {
            SDL_GamepadAxis x = SDL_GAMEPAD_AXIS_LEFTX, y = SDL_GAMEPAD_AXIS_LEFTY;
            int stick = 0;
            if (bind.device_stick == static_cast<int>(Gubsy2DAnalog::GP_RIGHT_STICK)) {
                x = SDL_GAMEPAD_AXIS_RIGHTX;
                y = SDL_GAMEPAD_AXIS_RIGHTY;
                stick = 1;
            } else if (bind.device_stick != static_cast<int>(Gubsy2DAnalog::GP_LEFT_STICK)) {
                continue;
            }
            const PadCalibration* calibration =
                pad_calibration_of(calibrations, SDL_GetGamepadID(pad));
            const int dead = zones.inner[stick];
            out[0] = pad_stick_axis(raw_axis(pad, calibration, x, stick, 0), dead, zones.outer);
            out[1] = pad_stick_axis(-raw_axis(pad, calibration, y, stick, 1), dead, zones.outer);
            if (out[0] != 0 || out[1] != 0) {
                return true;
            }
        }
    }
    out[0] = out[1] = 0;
    return false;
}

// a trigger's level 0..255 through the original's dead zone, the strongest of the player's pads,
// each through its calibration
int pad_trigger(const std::vector<SDL_Gamepad*>& pads, const PadCalibrations& calibrations,
                const BindsProfile& profile, int axis) {
    int level = 0;
    for (SDL_Gamepad* pad : pads) {
        for (const ginput::Axis1DBind& bind : ginput::binds_for_axis_1d(profile, axis)) {
            SDL_GamepadAxis trigger = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
            if (bind.device_axis == static_cast<int>(Gubsy1DAnalog::GP_RIGHT_TRIGGER)) {
                trigger = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            } else if (bind.device_axis != static_cast<int>(Gubsy1DAnalog::GP_LEFT_TRIGGER)) {
                continue;
            }
            int raw = std::max(0, static_cast<int>(SDL_GetGamepadAxis(pad, trigger)));
            if (const PadCalibration* calibration =
                    pad_calibration_of(calibrations, SDL_GetGamepadID(pad))) {
                raw = calibrated_trigger(*calibration,
                                         trigger == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : 0, raw);
            }
            level = std::max(level, pad_analog_level(raw * 255 / 32767));
        }
    }
    return level;
}

bool profile_binds_wheel(const BindsProfile& profile) {
    for (const ginput::Axis1DBind& bind : ginput::binds_for_axis_1d(profile, axis_1d_wheel_dpad)) {
        if (bind.device_axis == static_cast<int>(Gubsy1DAnalog::MOUSE_WHEEL)) {
            return true;
        }
    }
    return false;
}

// a player's integer setting from their settings profile, or the default
int player_setting(GubsyRuntime& runtime, int index, const char* key, int fallback) {
    EngineState& engine = gubsy_runtime_engine(runtime);
    const UserProfile* user = gubsy_lobby_user_profile(engine, index);
    if (!user) {
        return fallback;
    }
    for (const GameSettings& settings : engine.game_settings_pool) {
        if (settings.id == user->last_game_settings_profile_id) {
            return get_game_setting_int(settings, key, fallback);
        }
    }
    return fallback;
}

// the player's dead zone settings in raw units
StickZones stick_zones(GubsyRuntime& runtime, int index) {
    StickZones zones;
    const char* keys[2] = {left_dead_zone_key, right_dead_zone_key};
    for (int s = 0; s < 2; ++s) {
        zones.inner[s] =
            dead_zone_raw(std::clamp(player_setting(runtime, index, keys[s], dead_zone_default),
                                     dead_zone_min, dead_zone_max));
    }
    zones.outer = dead_zone_raw(
        std::clamp(player_setting(runtime, index, outer_dead_zone_key, 0), 0, outer_dead_zone_max));
    return zones;
}

// Mouse aim (owner, 2026-11-11): the counts moved move a target angle at once; the fighter turns
// and tilts toward it at its own rates, exactly as a full stick would, and stops on it. Each
// step's axis is what is left over the rate, held to -1..1 and taken in the world's Q16.16, and
// the target keeps what that axis doesn't turn until it is reached.
MouseAim mouse_aim(InputDevices& devices, int index, float twist, float pitch,
                   const AimRates& rates) {
    float& owed_twist = devices.owed_twist[static_cast<std::size_t>(index)];
    float& owed_pitch = devices.owed_pitch[static_cast<std::size_t>(index)];
    owed_twist = std::clamp(owed_twist + twist, rates.twist_low, rates.twist_high);
    owed_pitch += pitch;
    const auto axis = [](float& owed, float rate) {
        if (rate <= 0.0f) {
            owed = 0.0f;
            return Fx{};
        }
        const float value = std::clamp(owed / rate, -1.0f, 1.0f);
        const Fx step = fx_raw(static_cast<std::int32_t>(std::lround(value * 65536.0f)));
        owed -= fx_float(step) * rate;
        // the last sliver: reached
        if (std::fabs(owed) < rate * 1e-4f) {
            owed = 0.0f;
        }
        return step;
    };
    MouseAim aim;
    aim.twist = axis(owed_twist, rates.twist);
    aim.pitch = axis(owed_pitch, rates.pitch);
    return aim;
}

// one player's devices, read now, as one pad
void read_player(InputDevices& devices, GubsyRuntime& runtime, const ReadOptions& options,
                 int index, const GubsyLobbyPlayer& player, const BindsProfile& profile,
                 PadSample& sample, MouseAim& aim, std::uint32_t& pc_held) {
    const std::vector<SDL_Gamepad*> pads = player_pads(player);
    sample.connected = !pads.empty() || player_has(player, K::Keyboard, 0);
    if (!sample.connected) {
        return;
    }

    // buttons: the digital byte, and the analog buttons full on when pressed
    const auto is_down = [&](PadAction action) {
        return down(runtime, devices, profile, player, index, action);
    };
    for (int bit = 0; bit < 16; ++bit) {
        if (!is_down(static_cast<PadAction>(bit))) {
            continue;
        }
        if (bit < pad_analog_first_bit) {
            sample.buttons |= static_cast<std::uint16_t>(1u << bit);
        } else {
            sample.analog[bit - pad_analog_first_bit] = 0xFF;
        }
    }
    // Escape by context (ReadOptions::fight): the profile binds it to Back; in a fight it is
    // Start instead, and Back only from what else is bound to Back (Backspace, a pad's Back)
    if (options.fight && player_has(player, K::Keyboard, 0)) {
        bool escape = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE];
        for (const DeviceCode& tap : devices.taps) {
            escape = escape || (tap.kind == K::Keyboard && tap.code == SDL_SCANCODE_ESCAPE);
        }
        if (escape) {
            sample.buttons |= pad_start;
            bool other_back = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_BACKSPACE];
            for (const DeviceCode& tap : devices.taps) {
                other_back = other_back ||
                             (tap.kind == K::Keyboard && tap.code == SDL_SCANCODE_BACKSPACE);
            }
            for (SDL_Gamepad* pad : pads) {
                other_back = other_back || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
            }
            if (!other_back) {
                sample.buttons &= static_cast<std::uint16_t>(~pad_back);
            }
        }
    }
    // the PC's own menu actions, beside the pad
    pc_held = (is_down(PadAction::StandTurnLeft) ? pc_stand_left : 0u) |
              (is_down(PadAction::StandTurnRight) ? pc_stand_right : 0u);
    if (is_down(PadAction::TextSkip)) {
        sample.buttons |= pad_left_thumb | pad_right_thumb;
        sample.analog[6] = sample.analog[7] = 0xFF;
    }
    // the pads' triggers as levels, and the wheel as d-pad presses
    const PadCalibrations& calibrations = devices.calibrations;
    sample.analog[6] = static_cast<std::uint8_t>(std::max<int>(
        sample.analog[6], pad_trigger(pads, calibrations, profile, axis_1d_left_trigger)));
    sample.analog[7] = static_cast<std::uint8_t>(std::max<int>(
        sample.analog[7], pad_trigger(pads, calibrations, profile, axis_1d_right_trigger)));
    const bool mouse = player_has(player, K::Mouse, 0);
    if (mouse && profile_binds_wheel(profile)) {
        if (devices.wheel >= 0.5f) {
            sample.buttons |= pad_dpad_up;
        } else if (devices.wheel <= -0.5f) {
            sample.buttons |= pad_dpad_down;
        }
    }

    // sticks: a pad's off centre first, else the keys (full lean, or the walk lean)
    const int lean = is_down(PadAction::Walk) ? walk_lean : 0x7FFF;
    const StickZones zones = stick_zones(runtime, index);
    const auto keys = [&](PadAction plus, PadAction minus, int full) {
        return static_cast<std::int16_t>((is_down(plus) - is_down(minus)) * full);
    };
    if (!pad_stick(pads, calibrations, profile, axis_2d_left_stick, zones, sample.stick[0])) {
        sample.stick[0][0] = keys(PadAction::LeftStickRight, PadAction::LeftStickLeft, lean);
        sample.stick[0][1] = keys(PadAction::LeftStickUp, PadAction::LeftStickDown, lean);
    }
    if (!pad_stick(pads, calibrations, profile, axis_2d_right_stick, zones, sample.stick[1])) {
        sample.stick[1][0] = keys(PadAction::RightStickRight, PadAction::RightStickLeft, 0x7FFF);
        sample.stick[1][1] = keys(PadAction::RightStickUp, PadAction::RightStickDown, 0x7FFF);
    }

    // the mouse aims the upper body, while captured, for a profile that binds it
    const bool aiming = mouse && options.mouse_captured &&
                        profile_binds_2d(profile, axis_2d_mouse_aim, Gubsy2DAnalog::MOUSE_XY);
    const InputSettingsProfile* settings =
        gubsy_lobby_input_settings_profile(gubsy_runtime_engine(runtime), index);
    const float scale = mouse_radians_per_count * (settings ? settings->mouse_sensitivity : 1.0f);
    const float sx = settings && settings->mouse_invert_x ? -scale : scale;
    const float sy = settings && settings->mouse_invert_y ? -scale : scale;
    // mouse right turns right; mouse up (y falling) looks up
    aim = mouse_aim(devices, index, aiming ? devices.mouse_x * sx : 0.0f,
                    aiming ? -devices.mouse_y * sy : 0.0f,
                    options.rates[static_cast<std::size_t>(index)]);
}

} // namespace

void seat_aim_rates(const World& world, ReadOptions& options) {
    for (AimRates& rates : options.rates) {
        rates = AimRates{};
    }
    for (const Fighter& f : world.fighters) {
        if (!f.present || f.controller < 0 || f.controller >= max_pad_players) {
            continue;
        }
        // the mouse's radians a step for an axis of 1: turn_deg a step, the pitch's 3 degrees an
        // update
        AimRates& rates = options.rates[static_cast<std::size_t>(f.controller)];
        rates.twist = fx_float(angle_radians(f.turn_step));
        rates.pitch = 0.05235988f / 2.0f;
        // Preferred: the aim lands within 45 degrees of the legs. A positive axis lowers aim_yaw
        // (the original's sense), so the target's room is aim_yaw - 45 .. aim_yaw + 45 degrees
        if (!(f.control_type & control_type_standard)) {
            const float aim = fx_float(angle_radians(f.aim_yaw));
            rates.twist_low = aim - 0.78539816f;
            rates.twist_high = aim + 0.78539816f;
        }
    }
}

void input_event(InputDevices& devices, GubsyRuntime& runtime, const SDL_Event& event) {
    gubsy_process_sdl_event(runtime, event);
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat) {
            devices.taps.push_back(
                {K::Keyboard, ginput::any_device_id, static_cast<int>(event.key.scancode)});
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        devices.taps.push_back({K::Mouse, ginput::any_device_id, event.button.button});
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        devices.taps.push_back(
            {K::Gamepad, static_cast<int>(event.gbutton.which), event.gbutton.button});
        break;
    case SDL_EVENT_MOUSE_MOTION:
        devices.mouse_x += event.motion.xrel;
        devices.mouse_y += event.motion.yrel;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        devices.wheel +=
            event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        break;
    default:
        break;
    }
}

void read_pads(InputDevices& devices, GubsyRuntime& runtime, const ReadOptions& options) {
    gubsy_update_device_state(runtime);
    EngineState& engine = gubsy_runtime_engine(runtime);
    for (int index = 0; index < max_pad_players; ++index) {
        PadRecord& pad = devices.pads[static_cast<std::size_t>(index)];
        MouseAim& aim = devices.aims[static_cast<std::size_t>(index)];
        aim = MouseAim{};
        std::uint32_t& pc_held = devices.pc_held[static_cast<std::size_t>(index)];
        pc_held = 0;
        const GubsyLobbyPlayer* player = gubsy_lobby_player(engine, index);
        const BindsProfile* profile =
            player ? gubsy_find_binds_profile(runtime, player->binds_profile_id) : nullptr;
        if (!player || !profile) {
            pad_record_read(pad, PadSample{}, options.cadence);
            continue;
        }

        // not read during a fade or a lost-pad pause, nor outside the input masks: neutral
        // (pads_read: pad_record_clear), and nothing latched carries past it
        const bool read = !options.blank && (options.player_mask >> index & 1u) != 0;
        if (!read) {
            pad_record_clear(pad);
            pad.connected = !player_pads(*player).empty() || player_has(*player, K::Keyboard, 0);
            devices.owed_twist[static_cast<std::size_t>(index)] = 0.0f;
            devices.owed_pitch[static_cast<std::size_t>(index)] = 0.0f;
            continue;
        }
        PadSample sample;
        read_player(devices, runtime, options, index, *player, *profile, sample, aim, pc_held);
        pad_record_read(pad, sample, options.cadence);
    }
    devices.taps.clear();
    devices.mouse_x = devices.mouse_y = 0.0f;
    devices.wheel = 0.0f;
}
