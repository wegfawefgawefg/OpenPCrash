#pragma once
// Reading a retail save: phantomcrash.sav out of the "Game Records" save game on an Xbox, three
// 0x60E8-byte slots, each the first 0x60C4 bytes of GameState as raw x86 memory (decomp
// include/game_state.h; docs/saves.md, "The retail file"). The XCalculateSignature digest is keyed
// to the Xbox title and isn't checked; the CRC-32 before it is.
#include "save/save.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

inline constexpr std::size_t xbox_save_size = 0x122B8; // save_read_file wants exactly this
inline constexpr std::size_t xbox_slot_size = 0x60E8;
inline constexpr std::size_t xbox_game_size = 0x60C4;

// Shift-JIS pairs to UTF-8, for the roster names (RumbleContext.names): from the font's sjis pairs
// (data/text/font.sexp)
using SjisTable = std::map<int, std::string>;
bool load_sjis_table(SjisTable& table, const std::string& data_dir, std::string& error);

struct XboxSlot {
    bool crc_ok = false;
    bool present = false;
    SaveImage image;
    std::vector<std::string> notes; // anything unexpected read (unused fields that aren't zero...)
};

// the whole file: false (with error) when its size is wrong, as retail's read fails then
bool xbox_save_read(const std::vector<std::uint8_t>& file, const SjisTable& sjis,
                    XboxSlot slots[3], std::string& error);
// save_checksum: CRC-32 (the zlib one), over a slot up to its checksum word
std::uint32_t xbox_save_checksum(const std::uint8_t* data, std::size_t size);
