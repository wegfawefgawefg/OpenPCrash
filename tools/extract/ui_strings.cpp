#include "ui_strings.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {

// a text field of a table row: a char * at offset, or the text itself held in the row
struct StringField {
    std::uint32_t offset;
    const char* name;
    bool held = false;
};

// the tables whose rows hold text (include/tables.h, include/payloads.h, data/symbols.txt)
struct StringTable {
    const char* name;
    std::uint32_t va;
    int count;
    std::uint32_t stride;
    StringField fields[6];
};

const StringTable string_tables[] = {
    {"tournament", 0x24B2A0, 56, 0x6C, {{0, "name"}}},                       // tournaments
    {"tournament-rule", 0x24D020, 28, 0x2C, {{0, "name"}, {4, "subtitle"}}}, // tournament_rules
    {"body", 0x1B5B88, 16, 0x40, {{0, "name"}}},                             // sv_body_table
    {"leg", 0x1B5F88, 43, 0xC0, {{0, "name"}}},                              // sv_leg_table
    {"chip", 0x1B7FC8, 96, 0x7C, {{0, "name"}}},                             // ChipRecord
    {"arm", 0x1BAE48, 76, 0xC8, {{0, "name"}}},                              // sv_arm_table
    {"back", 0x1BE9A8, 69, 0xC8, {{0, "name"}}},                             // sv_back_table
    {"option", 0x1C1F90, 107, 0x2C, {{0, "name"}}},                          // OptionRecord
    {"color", 0x1C31F8, 57, 0xC, {{0, "name"}}},                             // PaintRecord
    {"sticker", 0x1C34A8, 206, 0xC, {{0, "name"}}},                          // PaintRecord
    {"scoobee", 0x19F920, 1190, 0x3C, {{0x38, "name"}}},              // rumble_scoobee_loadouts
    {"identity", 0x1C5AF8, 990, 0x10, {{0, "name"}, {4, "name-en"}}}, // rumble_identities
    {"team", 0x20C1F0, 84, 4, {{0, "name"}}},                         // rumble_team_names
    {"help", 0x1DE000, 35, 0xC, {{4, "label"}}},                      // pepper_talk_items
    {"save-dialog",
     0x21CB48,
     4,
     0x4C,
     {{0, "line0"}, {4, "line1"}, {8, "line2"}, {12, "line3"}, {16, "line4"}, {20, "line5"}}},
    {"jp-tutorial", // MsgBox rows nothing points at (docs/findings.md "Japanese leftovers")
     0x244728,
     73,
     0x4C,
     {{0, "line0"}, {4, "line1"}, {8, "line2"}, {12, "line3"}, {16, "line4"}, {20, "line5"}}},
    {"music", // MusicRecord, text held in the row
     0x1EFCC8,
     81,
     0x1AC,
     {{4, "title", true},
      {0x69, "artist", true},
      {0xCE, "lyricist", true},
      {0x133, "composer", true}}},
    {"ending-subtitle", 0x17CF88, 13, 8, {{4, "text"}}}, // ending_subtitles, Subtitle rows
    {"dealer", 0x1FDC8C, 1, 0x34, {{0, "help"}}},        // dealer_screen_rows
};

const XbeSection* find_section(const Xbe& xbe, const char* name) {
    for (const XbeSection& section : xbe.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

bool double_byte_lead(std::uint8_t b) {
    return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
}

// the whole-pair string at offset of a section's raw bytes: its length in bytes, or 0 when it
// isn't one (a byte that can't start or end a pair, or no zero within the section)
std::size_t pair_string_length(const std::uint8_t* raw, std::size_t size, std::size_t at) {
    std::size_t end = at;
    while (end + 1 < size && raw[end] != 0) {
        const std::uint8_t trail = raw[end + 1];
        if (!double_byte_lead(raw[end]) || trail < 0x40 || trail == 0x7F || trail > 0xFC) {
            return 0;
        }
        end += 2;
    }
    return end < size && raw[end] == 0 ? end - at : 0;
}

std::string form_at(const char* kind, std::uint32_t va) {
    char text[48];
    std::snprintf(text, sizeof text, "(%s 0x%x)", kind, va);
    return text;
}

} // namespace

bool collect_ui_strings(const Xbe& xbe, const GlyphTable& glyphs, std::vector<UiString>& out,
                        std::string& error) {
    out.clear();
    const XbeSection* sections[2] = {find_section(xbe, ".rdata"), find_section(xbe, ".data")};
    if (!sections[0] || !sections[1]) {
        error = "the XBE has no .rdata or .data";
        return false;
    }

    // the tables, row by row; their strings are left out of the loose ones
    std::unordered_set<std::uint32_t> claimed;
    for (const StringTable& table : string_tables) {
        for (int row = 0; row < table.count; ++row) {
            for (const StringField& field : table.fields) {
                if (!field.name) {
                    break;
                }
                const std::uint32_t slot = table.va + row * table.stride + field.offset;
                const std::uint32_t va = field.held ? slot : xbe_u32(xbe, slot);
                const std::string raw = va ? xbe_cstring(xbe, va) : "";
                if (raw.empty()) {
                    continue;
                }
                UiString string;
                string.id = std::string(table.name) + "." + std::to_string(row) + "." + field.name;
                string.table = table.name;
                string.va = va;
                string.text = sjis_to_utf8(glyphs, raw, string.missing);
                out.push_back(std::move(string));
                // every aligned address inside it, so its tail isn't read as a loose string
                for (std::uint32_t at = va; at <= va + raw.size(); ++at) {
                    claimed.insert(at);
                }
            }
        }
    }

    // loose candidates: 4-aligned starts (cl places each literal on 4), whole pairs
    std::vector<UiString> loose;
    std::vector<int> pair_counts;
    std::vector<bool> after_zero;
    std::unordered_map<std::uint32_t, std::size_t> by_va;
    for (const XbeSection* section : sections) {
        const std::uint8_t* raw = xbe.bytes.data() + section->raw;
        for (std::size_t at = 0; at < section->rawsize; at += 4) {
            if (raw[at] == 0) {
                continue;
            }
            const std::size_t length = pair_string_length(raw, section->rawsize, at);
            const auto va = static_cast<std::uint32_t>(section->va + at);
            if (length == 0 || claimed.count(va)) {
                continue;
            }
            UiString string;
            string.va = va;
            string.text =
                sjis_to_utf8(glyphs, std::string(reinterpret_cast<const char*>(raw + at), length),
                             string.missing);
            by_va.emplace(va, loose.size());
            loose.push_back(std::move(string));
            pair_counts.push_back(static_cast<int>(length / 2));
            after_zero.push_back(at == 0 || raw[at - 1] == 0);
            at = (at + length) & ~static_cast<std::size_t>(3);
        }
    }

    // what points at them: aligned words of the data sections, any offset of code
    for (const XbeSection& section : xbe.sections) {
        const bool code = section.name == ".text";
        if (!code && section.name != ".rdata" && section.name != ".data" &&
            section.name != "EVENTSEG") {
            continue;
        }
        const std::uint32_t step = code ? 1 : 4;
        for (std::uint32_t offset = 0; offset + 4 <= section.rawsize; offset += step) {
            std::uint32_t value = 0;
            std::memcpy(&value, xbe.bytes.data() + section.raw + offset, 4);
            const auto found = by_va.find(value);
            if (found == by_va.end()) {
                continue;
            }
            // in code, a short string's address must follow push imm32 (68) or mov reg, imm32
            // (B8..BF): two-pair strings turn up inside call offsets by chance
            const std::uint8_t before = offset > 0 ? xbe.bytes[section.raw + offset - 1] : 0;
            if (code && pair_counts[found->second] < 3 && before != 0x68 &&
                (before < 0xB8 || before > 0xBF)) {
                continue;
            }
            loose[found->second].used_by.push_back(
                form_at(code ? "code" : "data", section.va + offset));
        }
    }

    // keep what something points at (a quarter of its pairs missing at most), or what nothing
    // points at if it follows a zero and is three pairs or more that all draw: shorter
    // unreferenced runs are floats and indices that happen to read as pairs
    for (std::size_t i = 0; i < loose.size(); ++i) {
        UiString& string = loose[i];
        const int pairs = pair_counts[i];
        const bool kept = !string.used_by.empty()
                              ? string.missing == 0 || (pairs >= 4 && string.missing * 4 <= pairs)
                              : string.missing == 0 && pairs >= 3 && after_zero[i];
        if (!kept) {
            continue;
        }
        char id[24];
        std::snprintf(id, sizeof id, "str.%06x", string.va);
        string.id = id;
        out.push_back(std::move(string));
    }
    return true;
}
