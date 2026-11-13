#pragma once
// The game's event scripts: every EventCommand array (include/event.h), found by walking the
// places they live. EVENTSEG holds the 110 conversations, one per conversation state; .data holds
// the Sonic Amp artist notes (bios), the battle barks and tutorial, and the dealer's maker talks.
// Shared by the text family (the lines) and the scripts family (the commands).
#include "xbe.hpp"

#include <cstdint>
#include <string>
#include <vector>

// one 0x34-byte command: type, then twelve words read per type by event_cmd_handlers 0x291228
struct EventRecord {
    std::int32_t type = 0;
    std::int32_t arg[12] = {};
};

struct EventScript {
    std::string name;  // talk_30 (by state), bio_1de3f8, bark_2c4b60, maker_40e258 (by address)
    std::string group; // talk, bio, bark, maker
    std::uint32_t va = 0;
    int state = -1;                    // the conversation state for talk
    std::vector<EventRecord> commands; // through the type 13 end
    std::vector<std::string> used_by;  // forms naming what points at it, e.g. (music 12)
};

// a line as event_cmd_text draws it: the record at an even text slot is the furigana and the
// type 1 record after it the line; with no type 1 after it, the record is the line itself
struct EventLine {
    int command = -1;  // the line's record
    int furigana = -1; // the furigana record, -1 for none
};

// every script, talks in state order then the .data runs by address; false if a known
// conversation isn't where the table says
bool find_event_scripts(const Xbe& xbe, std::vector<EventScript>& scripts, std::string& error);

// the lines of a script in command order, and how many type 1 records no line draws
std::vector<EventLine> event_lines(const EventScript& script, int& undrawn);

// "talk_30.3": a script's text by the index of the command that draws it
std::string event_text_id(const EventScript& script, int command);
