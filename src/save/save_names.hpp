#pragma once
// The words the save text uses for small enumerations, and back. A value without a word is written
// as its number, and a number is always read, so nothing is lost on odd values.
#include <cstdlib>
#include <string>
#include <string_view>

namespace save_names {

// music_owned: 1 bought, 2 heard (acknowledged)
inline constexpr std::string_view song_states[] = {"none", "bought", "heard"};
// InventoryItem.kind
inline constexpr std::string_view part_kinds[] = {"body", "leg", "arm", "back", "chip", "option"};
// OpponentTicket.role
inline constexpr std::string_view ticket_roles[] = {"alternate", "primary", "fixed"};
// PlayerConfig.keys order: the actions of the button config's rows 3..10
inline constexpr std::string_view button_actions[] = {
    "arm-a", "arm-b", "back-a", "back-b", "left-step", "right-step", "jump", "camouflage"};
// the pad buttons a key index picks (D_001DD470: 0x100, 0x200, 0x400, 0x800, 0x4000, 0x8000,
// 0x2000, 0x1000)
inline constexpr std::string_view pad_buttons[] = {
    "a", "b", "x", "y", "left-trigger", "right-trigger", "white", "black"};

template <std::size_t N> std::string word(const std::string_view (&words)[N], int value) {
    if (value >= 0 && value < static_cast<int>(N)) {
        return std::string(words[value]);
    }
    return std::to_string(value);
}

// a word back to its value, or the number written; false when it is neither
template <std::size_t N>
bool value(const std::string_view (&words)[N], std::string_view text, int& out) {
    for (std::size_t i = 0; i < N; ++i) {
        if (words[i] == text) {
            out = static_cast<int>(i);
            return true;
        }
    }
    const std::string number(text);
    char* end = nullptr;
    const long parsed = std::strtol(number.c_str(), &end, 0);
    if (number.empty() || *end != '\0') {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

} // namespace save_names

inline std::string song_state_name(int value) {
    return save_names::word(save_names::song_states, value);
}
inline std::string part_kind_name(int value) {
    return save_names::word(save_names::part_kinds, value);
}
inline std::string ticket_role_name(int value) {
    return save_names::word(save_names::ticket_roles, value);
}
inline std::string button_action_name(int action) {
    return save_names::word(save_names::button_actions, action);
}
inline std::string pad_button_name(int value) {
    return save_names::word(save_names::pad_buttons, value);
}
