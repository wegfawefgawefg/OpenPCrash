#include "input/pad_calibration.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <gubsy/runtime.hpp>
#include <gubsy/settings/top_level_settings.hpp>
// gubsy's engine, for its install settings (as pc_settings.cpp writes them)
#include "src/engine_state.hpp"

namespace {

// "lx: centre low high, ly, rx, ry, then the triggers' rest and full", 16 numbers
std::string calibration_text(const PadCalibration& c) {
    std::string text;
    char number[16];
    for (const auto& stick : c.stick) {
        for (const AxisCalibration& axis : stick) {
            for (const int value : {axis.centre, axis.low, axis.high}) {
                std::snprintf(number, sizeof number, "%d ", value);
                text += number;
            }
        }
    }
    for (int t = 0; t < 2; ++t) {
        std::snprintf(number, sizeof number, "%d %d ", c.trigger_rest[t], c.trigger_full[t]);
        text += number;
    }
    text.pop_back();
    return text;
}

// the text back; false when it isn't 16 numbers that make sense
bool parse_calibration(const std::string& text, PadCalibration& out) {
    int v[16];
    if (std::sscanf(text.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &v[0], &v[1],
                    &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12],
                    &v[13], &v[14], &v[15]) != 16) {
        return false;
    }
    for (int s = 0; s < 2; ++s) {
        for (int a = 0; a < 2; ++a) {
            const int* at = v + (s * 2 + a) * 3;
            AxisCalibration& axis = out.stick[s][a];
            axis = {at[0], at[1], at[2]};
            if (!(axis.low < axis.centre && axis.centre < axis.high)) {
                return false;
            }
        }
    }
    for (int t = 0; t < 2; ++t) {
        out.trigger_rest[t] = v[12 + t * 2];
        out.trigger_full[t] = v[13 + t * 2];
        if (out.trigger_full[t] <= out.trigger_rest[t]) {
            return false;
        }
    }
    return true;
}

// letters, digits and dots, dashes and underscores kept; the rest '_'
std::string key_part(const char* text) {
    std::string out = text ? text : "";
    for (char& ch : out) {
        const bool kept = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
        ch = kept ? ch : '_';
    }
    return out;
}

std::string guid_text(SDL_JoystickID id) {
    char text[64] = {};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), text, sizeof text);
    return text;
}

// the pad's serial number, "" when SDL reads none (the pad must be open: gubsy opens every pad)
std::string serial_text(SDL_JoystickID id) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(id);
    const char* serial = pad ? SDL_GetGamepadSerial(pad) : nullptr;
    return serial ? key_part(serial) : std::string();
}

// the pad's keys: "<GUID>.serial.<serial>", else "<GUID>.path.<path>" then "<GUID>"; a pad with
// a serial has no kind's key of its own to fall back to, but reads one kept before this
const std::vector<std::string>& pad_keys(const PadCalibrations& all, SDL_JoystickID id) {
    const auto at = all.keys_of.find(id);
    if (at != all.keys_of.end()) {
        return at->second;
    }
    std::vector<std::string> keys;
    const std::string guid = guid_text(id);
    const std::string serial = serial_text(id);
    const char* path = SDL_GetGamepadPathForID(id);
    if (!serial.empty()) {
        keys.push_back(guid + ".serial." + serial);
    } else if (path && *path) {
        keys.push_back(guid + ".path." + key_part(path));
    }
    keys.push_back(guid);
    return all.keys_of[id] = keys;
}

} // namespace

void pad_calibrations_load(PadCalibrations& out, GubsyRuntime& runtime) {
    out.by_key.clear();
    out.keys_of.clear();
    const TopLevelGameSettings& top = gubsy_runtime_engine(runtime).top_level_game_settings;
    const std::size_t prefix = std::strlen(calibration_key_prefix);
    for (const auto& [key, value] : top.settings) {
        const std::string* text = std::get_if<std::string>(&value);
        PadCalibration calibration;
        if (text && key.compare(0, prefix, calibration_key_prefix) == 0 &&
            parse_calibration(*text, calibration)) {
            out.by_key[key.substr(prefix)] = calibration;
        }
    }
}

void pad_calibration_store(PadCalibrations& all, GubsyRuntime& runtime, SDL_JoystickID pad,
                           const PadCalibration* calibration) {
    const std::vector<std::string> keys = pad_keys(all, pad);
    TopLevelGameSettings& top = gubsy_runtime_engine(runtime).top_level_game_settings;
    // kept under the pad's own key, and its kind's too when it has no serial (its path may
    // change); taken out from every one, so the pad reads as it came
    const bool serial = keys.front().find(".serial.") != std::string::npos;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string key = calibration_key_prefix + keys[i];
        if (calibration && (i == 0 || !serial)) {
            all.by_key[keys[i]] = *calibration;
            set_top_level_setting_string(top, key, calibration_text(*calibration));
        } else if (!calibration) {
            all.by_key.erase(keys[i]);
            top.settings.erase(key);
        }
    }
    if (!save_top_level_game_settings(top)) {
        std::fprintf(stderr, "settings: couldn't write the pad's calibration\n");
    }
}

const PadCalibration* pad_calibration_of(const PadCalibrations& all, SDL_JoystickID id) {
    if (all.by_key.empty()) {
        return nullptr;
    }
    for (const std::string& key : pad_keys(all, id)) {
        const auto at = all.by_key.find(key);
        if (at != all.by_key.end()) {
            return &at->second;
        }
    }
    return nullptr;
}

int calibrated_axis(const AxisCalibration& axis, int raw) {
    const int offset = raw - axis.centre;
    const int reach = offset >= 0 ? axis.high - axis.centre : axis.centre - axis.low;
    if (reach <= 0) {
        return 0;
    }
    return std::clamp(offset * 32767 / reach, -32768, 32767);
}

int calibrated_trigger(const PadCalibration& calibration, int trigger, int raw) {
    const int rest = calibration.trigger_rest[trigger], full = calibration.trigger_full[trigger];
    if (full <= rest) {
        return raw;
    }
    return std::clamp((raw - rest) * 32767 / (full - rest), 0, 32767);
}
