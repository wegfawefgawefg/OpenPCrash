#pragma once
// The disc: the asset registry (read from the XBE) and reads of one asset's bytes from its cabinet
// or loose file (docs/port/data.md sections 3 and 4). The disc is an extracted folder or an Xbox
// disc image (disc_image.hpp).
#include "disc_image.hpp"
#include "xbe.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct AssetRecord {
    std::string path; // D:\Media\... for the 130 loose files, else empty
    std::uint32_t size = 0;
    std::uint32_t kind = 0;
    std::vector<std::int32_t> descriptor; // 3 words for kind 0, 2 for kind 3, else empty
    std::int32_t cabinet = -1;
    std::uint32_t offset = 0;
};

struct Disc {
    std::string root; // the folder holding Media/, or the image
    DiscSource source;
    Xbe xbe;
    std::vector<AssetRecord> assets;
    std::array<std::string, 16> cabinet_names; // a00_holymodel...
};

// the disc at path (a folder or an image) and its XBE: xbe_path if given, else the disc's
// default.xbe, else xbe_fallback (an extracted folder kept without it). Checks it is the retail
// Phantom Crash the port reads; problem says why not
bool open_disc(Disc& disc, const std::string& path, const std::string& xbe_path,
               const std::string& xbe_fallback, DiscProblem& problem, std::string& error);
// the asset's exact bytes; empty on a failed read
std::vector<std::uint8_t> read_asset(const Disc& disc, int id);
// where it came from, for the manifest: "b00_shinjuku+0x356800" or "Media/model/choco_mdl.bin"
std::string asset_source(const Disc& disc, int id);
