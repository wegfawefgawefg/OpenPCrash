#include "event_scripts.hpp"

#include "addresses.hpp"

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

// each conversation state's command array (decomp src/data/event/s_XX.c, event_state_XX in
// data/symbols.txt); the state picks the package in its talk spawner, so only code says which
struct TalkPackage {
    int state;
    std::uint32_t va;
};
const TalkPackage talk_packages[] = {
    {0x30, 0xDB4918}, {0x31, 0xDB9548}, {0x32, 0xDBBA70}, {0x33, 0xE19580}, {0x34, 0xE1DDA8},
    {0x35, 0xE22A78}, {0x36, 0xE27680}, {0x37, 0xE2CD20}, {0x38, 0xE31080}, {0x39, 0xE35448},
    {0x3A, 0xE398D0}, {0x3B, 0xE3DF00}, {0x3C, 0xE3FF78}, {0x3D, 0xE41778}, {0x3E, 0xE44C00},
    {0x3F, 0xE459E0}, {0x40, 0xE47BC0}, {0x41, 0xD4D180}, {0x42, 0xD506D0}, {0x43, 0xD53D08},
    {0x44, 0xD57108}, {0x45, 0xD5B650}, {0x46, 0xD61D10}, {0x47, 0xD64CC0}, {0x48, 0xD67668},
    {0x49, 0xD6CA58}, {0x4A, 0xD6E098}, {0x4B, 0xDD0FB8}, {0x4C, 0xDD6458}, {0x4D, 0xDDD950},
    {0x4E, 0xDE7228}, {0x4F, 0xDEC090}, {0x50, 0xDF04A0}, {0x51, 0xDF4E48}, {0x52, 0xDFC400},
    {0x53, 0xE03A00}, {0x54, 0xE07DF0}, {0x55, 0xE0A650}, {0x56, 0xE0E800}, {0x57, 0xE10F40},
    {0x58, 0xE11F30}, {0x59, 0xE14990}, {0x5A, 0xE57388}, {0x5B, 0xE5B0C0}, {0x5C, 0xE62990},
    {0x5D, 0xE66DA8}, {0x5E, 0xE69640}, {0x5F, 0xE6CBB0}, {0x60, 0xE6FF40}, {0x61, 0xE70FC8},
    {0x62, 0xE724A0}, {0x63, 0xE77F20}, {0x64, 0xE7E108}, {0x65, 0xE80D28}, {0x66, 0xE85758},
    {0x67, 0xE8B480}, {0x68, 0xE8F5E8}, {0x69, 0xEA5BE8}, {0x6A, 0xEA7AA0}, {0x6B, 0xEA8E00},
    {0x6C, 0xEAA840}, {0x6D, 0xEAD498}, {0x6E, 0xEBF100}, {0x6F, 0xEC0520}, {0x70, 0xEC1538},
    {0x71, 0xEC3378}, {0x72, 0xEAF828}, {0x73, 0xEB1938}, {0x74, 0xEB34C0}, {0x75, 0xEB4A00},
    {0x76, 0xEA01C0}, {0x77, 0xEA2008}, {0x78, 0xEA3CE0}, {0x79, 0xE93340}, {0x7A, 0xE95E68},
    {0x7B, 0xE98AF0}, {0x7C, 0xEB6218}, {0x7D, 0xEB9A68}, {0x7E, 0xEBCF48}, {0x7F, 0xE9C2F8},
    {0x80, 0xE9ECD0}, {0x81, 0xD82470}, {0x82, 0xD86798}, {0x83, 0xD8A828}, {0x84, 0xD8DD88},
    {0x85, 0xDA86E0}, {0x86, 0xDABD20}, {0x87, 0xDCA8B0}, {0x88, 0xDCE148}, {0x89, 0xE49F10},
    {0x8A, 0xE4D840}, {0x8B, 0xDA0748}, {0x8C, 0xDA4188}, {0x8D, 0xDC2338}, {0x8E, 0xDC6600},
    {0x8F, 0xDAE0D8}, {0x90, 0xDB19C8}, {0x91, 0xD99958}, {0x92, 0xD9DA58}, {0x93, 0xD72098},
    {0x94, 0xD76E00}, {0x95, 0xD7ADF8}, {0x96, 0xD7F308}, {0x97, 0xD90178}, {0x98, 0xD94D50},
    {0x99, 0xE90F68}, {0x9A, 0xE546D0}, {0x9B, 0xE510E8}, {0x9C, 0xE4E9C0}, {0x9D, 0xEC4D90},
};

// a section's bytes by address; words past the raw data read as zero up to the virtual size
struct SectionView {
    const XbeSection* section = nullptr;
    const std::uint8_t* raw = nullptr;
};

SectionView section_view(const Xbe& xbe, const char* name) {
    for (const XbeSection& section : xbe.sections) {
        if (section.name == name) {
            return {&section, xbe.bytes.data() + section.raw};
        }
    }
    return {};
}

bool holds(const SectionView& view, std::uint32_t va, std::uint32_t n) {
    return va >= view.section->va && va + n <= view.section->va + view.section->vsize;
}

std::int32_t word_at(const SectionView& view, std::uint32_t va, int bytes) {
    std::int32_t value = 0;
    const std::uint32_t offset = va - view.section->va;
    if (offset + bytes <= view.section->rawsize) {
        std::memcpy(&value, view.raw + offset, bytes);
    }
    return bytes == 2 ? static_cast<std::int16_t>(value) : value;
}

// a -1-ended run of glyphs and controls inside the section
bool tokens_valid(const SectionView& view, std::uint32_t va) {
    for (std::uint32_t at = va; at < va + 0x4000; at += 2) {
        if (!holds(view, at, 2) || at + 2 > view.section->va + view.section->rawsize) {
            return false;
        }
        const std::int32_t token = word_at(view, at, 2);
        if (token == -1) {
            return true;
        }
        if (token < -18 || token >= xbe_at::font_glyph_count) {
            return false;
        }
    }
    return false;
}

// one record as the types the shipped scripts use write it (EV_* in include/event.h): unused
// words zero, sprite and portrait scales 1.0; anything else is not the start of an array
bool record_valid(const SectionView& view, const EventRecord& record) {
    const std::int32_t* a = record.arg;
    auto zero_from = [&](int first) {
        for (int i = first; i < 12; ++i) {
            if (a[i] != 0) {
                return false;
            }
        }
        return true;
    };
    switch (record.type) {
    case 1:
        return a[9] == 0 && a[10] == 0 && a[11] == 0 &&
               holds(view, static_cast<std::uint32_t>(a[0]), 2) &&
               tokens_valid(view, static_cast<std::uint32_t>(a[0]));
    case 2:
        return a[0] == 0 && a[1] == 0 && zero_from(3);
    case 3:
    case 4:
        return a[10] == 0x3F800000 && a[11] == 0x3F800000;
    case 5:
        return zero_from(1);
    case 10:
    case 13:
        return zero_from(0);
    default:
        return false;
    }
}

// the array starting at va, through its type 13; empty if va doesn't start one
std::vector<EventRecord> parse_array(const SectionView& view, std::uint32_t va) {
    std::vector<EventRecord> records;
    bool has_text = false;
    for (std::uint32_t at = va; holds(view, at, 0x34); at += 0x34) {
        EventRecord record;
        record.type = word_at(view, at, 4);
        for (int i = 0; i < 12; ++i) {
            record.arg[i] = word_at(view, at + 4 + i * 4, 4);
        }
        if (!record_valid(view, record)) {
            return {};
        }
        has_text = has_text || record.type == 1;
        records.push_back(record);
        if (record.type == 13) {
            return has_text ? records : std::vector<EventRecord>{};
        }
    }
    return {};
}

// every array in [begin, end), token pools stepped over a word at a time
void walk_arrays(const SectionView& view, std::uint32_t begin, std::uint32_t end, const char* group,
                 std::vector<EventScript>& out) {
    for (std::uint32_t at = begin; at + 0x34 <= end;) {
        std::vector<EventRecord> records = parse_array(view, at);
        if (records.empty()) {
            at += 4;
            continue;
        }
        EventScript script;
        char name[32];
        std::snprintf(name, sizeof name, "%s_%06x", group, at);
        script.name = name;
        script.group = group;
        script.va = at;
        script.commands = std::move(records);
        at += static_cast<std::uint32_t>(script.commands.size()) * 0x34;
        out.push_back(std::move(script));
    }
}

// the 35 bark kinds a reaction profile has a script for (BARK_* in the decomp's include/actor.h)
const char* const bark_kinds[35] = {"boss-defeated",
                                    "killed-new-boss",
                                    "lost-to-boss",
                                    "armor-25-by-boss",
                                    "final-boss-defeated",
                                    "lost",
                                    "weapon-broken-last",
                                    "rear-hit",
                                    "kill-at-25",
                                    "door-at-25",
                                    "shield",
                                    "armor-pickup-at-25",
                                    "weapon-broken-at-25",
                                    "ammo-50-at-25",
                                    "armor-25",
                                    "weapon-broken",
                                    "camo-ending",
                                    "crit",
                                    "enemy-lost",
                                    "ammo-pickup-low",
                                    "ammo-50",
                                    "kill",
                                    "hit-flying",
                                    "locked-on",
                                    "armor-50",
                                    "hit",
                                    "laser-hit",
                                    "blast-taken",
                                    "door",
                                    "armor-pickup",
                                    "ammo-pickup",
                                    "hit-flammable",
                                    "fired",
                                    "idle",
                                    "camo-on"};

// what a reference at va is, as a form for the script's header
std::string referrer_form(std::uint32_t at, bool code) {
    char text[80];
    const std::uint32_t profiles_end =
        xbe_at::reaction_profiles + xbe_at::reaction_profile_count * 0x8C;
    const std::uint32_t music_end = xbe_at::music_records + xbe_at::music_record_count * 0x1AC;
    if (code) {
        std::snprintf(text, sizeof text, "(code 0x%x)", at);
    } else if (at >= xbe_at::reaction_profiles && at < profiles_end) {
        const std::uint32_t offset = at - xbe_at::reaction_profiles;
        std::snprintf(text, sizeof text, "(reaction-profile %u %u %s)", offset / 0x8C,
                      offset % 0x8C / 4, bark_kinds[offset % 0x8C / 4]);
    } else if (at >= xbe_at::music_records && at < music_end &&
               (at - xbe_at::music_records) % 0x1AC == 0x1A0) {
        std::snprintf(text, sizeof text, "(music %u)", (at - xbe_at::music_records) / 0x1AC);
    } else if (at >= xbe_at::dealer_screen_rows + 0x1C && at < xbe_at::dealer_screen_rows + 0x28) {
        std::snprintf(text, sizeof text, "(dealer-talk %u)",
                      (at - xbe_at::dealer_screen_rows - 0x1C) / 4);
    } else {
        std::snprintf(text, sizeof text, "(data 0x%x)", at);
    }
    return text;
}

// every pointer to a script: aligned words in the data sections, any offset in .text
void find_referrers(const Xbe& xbe, std::vector<EventScript>& scripts) {
    std::unordered_map<std::uint32_t, std::size_t> by_va;
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        by_va.emplace(scripts[i].va, i);
    }
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
            if (found != by_va.end()) {
                scripts[found->second].used_by.push_back(referrer_form(section.va + offset, code));
            }
        }
    }
}

} // namespace

bool find_event_scripts(const Xbe& xbe, std::vector<EventScript>& scripts, std::string& error) {
    scripts.clear();
    const SectionView eventseg = section_view(xbe, "EVENTSEG");
    const SectionView data = section_view(xbe, ".data");
    if (!eventseg.section || !data.section) {
        error = "the XBE has no EVENTSEG or .data section";
        return false;
    }

    // conversations: walk the section, then name each by its state
    std::vector<EventScript> found;
    walk_arrays(eventseg, eventseg.section->va, eventseg.section->va + eventseg.section->vsize,
                "talk", found);
    std::unordered_map<std::uint32_t, std::size_t> by_va;
    for (std::size_t i = 0; i < found.size(); ++i) {
        by_va.emplace(found[i].va, i);
    }
    for (const TalkPackage& package : talk_packages) {
        const auto at = by_va.find(package.va);
        if (at == by_va.end()) {
            char text[96];
            std::snprintf(text, sizeof text, "no command array at 0x%x for state 0x%x", package.va,
                          package.state);
            error = text;
            return false;
        }
        EventScript script = found[at->second];
        char name[16];
        std::snprintf(name, sizeof name, "talk_%02x", package.state);
        script.name = name;
        script.state = package.state;
        scripts.push_back(std::move(script));
    }
    if (found.size() != scripts.size()) {
        error = "EVENTSEG holds " + std::to_string(found.size()) + " arrays, not " +
                std::to_string(scripts.size());
        return false;
    }

    // the .data runs
    walk_arrays(data, xbe_at::bios_begin, xbe_at::bios_end, "bio", scripts);
    walk_arrays(data, xbe_at::barks_begin, xbe_at::barks_end, "bark", scripts);
    walk_arrays(data, xbe_at::makers_begin, xbe_at::makers_end, "maker", scripts);
    find_referrers(xbe, scripts);
    return true;
}

std::vector<EventLine> event_lines(const EventScript& script, int& undrawn) {
    // event_cmd_text 0xC5AFD: text_slot counts every type 1 record; an even slot draws
    std::vector<EventLine> lines;
    undrawn = 0;
    int slot = 0;
    const int count = static_cast<int>(script.commands.size());
    for (int i = 0; i < count; ++i) {
        if (script.commands[i].type != 1) {
            continue;
        }
        if (slot % 2 == 0) {
            EventLine line;
            if (i + 1 < count && script.commands[i + 1].type == 1) {
                line.furigana = i;
                line.command = i + 1;
            } else {
                line.command = i;
            }
            lines.push_back(line);
        } else if (lines.empty() || lines.back().command != i) {
            ++undrawn;
        }
        ++slot;
    }
    return lines;
}

std::string event_text_id(const EventScript& script, int command) {
    return script.name + "." + std::to_string(command);
}
