#pragma once
// Which build this is: the game's version (cmake/release.cmake, bumped by hand) and, in a release
// (scripts/release.sh), the commit and the day it was built from. The run log's first lines and
// the first-run screen show it.
#include <string>

struct BuildInfo {
    const char* version; // "0.1.0"
    const char* commit;  // "f88b9ec", "-dirty" on the end if built with changes not committed
    const char* date;    // "2026-11-12"; both empty outside a release
    bool release;        // a release bundle (game_paths.hpp)
};

const BuildInfo& build_info();
// "OpenPCrash 0.1.0 2026-11-12 f88b9ec", or "OpenPCrash 0.1.0 dev"
std::string build_label();
